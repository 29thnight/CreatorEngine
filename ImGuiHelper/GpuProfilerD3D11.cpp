#ifndef BUILD_FLAG
#include "GpuProfilerD3D11.h"
#include <cassert>

thread_local GpuProfilerD3D11::Tls GpuProfilerD3D11::s_tls;
GpuProfilerD3D11 gGpuProfiler;

bool GpuProfilerD3D11::Initialize(ID3D11Device* dev, ID3D11DeviceContext* imm,
    uint32_t history, uint32_t maxEvents) {
    m_device = dev; m_immediate = imm;
    m_historySize = history; m_maxEventsPerFrame = maxEvents;
    m_frames.clear();
    m_frames.resize(history);
    for (auto& f : m_frames) {
        f.qDisjoint = NewDisjointQuery();
        f.qFrameStart = NewTimestampQuery();
        f.tsPool.reserve(maxEvents * 2);
        f.pending.reserve(maxEvents);
        f.resolved.reserve(maxEvents);
        f.frameStartTicks = 0;
        f.disjoint = {};
        f.disjointEnded = false;
        f.nextTs = 0;
    }
    m_frameIndex = 0;
    return true;
}

void GpuProfilerD3D11::Shutdown() {
    m_frames.clear();
    m_device = nullptr; m_immediate = nullptr;
}

GpuProfilerD3D11::ComPtr GpuProfilerD3D11::NewTimestampQuery() {
    D3D11_QUERY_DESC d{ D3D11_QUERY_TIMESTAMP, 0 };
    ComPtr q; m_device->CreateQuery(&d, q.GetAddressOf());
    return q;
}
GpuProfilerD3D11::ComPtr GpuProfilerD3D11::NewDisjointQuery() {
    D3D11_QUERY_DESC d{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
    ComPtr q; m_device->CreateQuery(&d, q.GetAddressOf());
    return q;
}
GpuProfilerD3D11::ComPtr GpuProfilerD3D11::AcquireTs(Frame& f) {
    if (f.nextTs < f.tsPool.size()) return f.tsPool[f.nextTs++];
    auto q = NewTimestampQuery();
    f.tsPool.emplace_back(q);
    f.nextTs = (uint32_t)f.tsPool.size();
    return q;
}

GpuProfilerD3D11::ComPtr GpuProfilerD3D11::AcquireTsTLS() {
    if (s_tls.nextTs < s_tls.tsPool.size()) return s_tls.tsPool[s_tls.nextTs++];
    D3D11_QUERY_DESC d{ D3D11_QUERY_TIMESTAMP, 0 };
    Microsoft::WRL::ComPtr<ID3D11Query> q;
    m_device->CreateQuery(&d, q.GetAddressOf());
    s_tls.tsPool.emplace_back(q);
    s_tls.nextTs = (uint32_t)s_tls.tsPool.size();
    return q;
}

void GpuProfilerD3D11::OnFrameStart(ID3D11DeviceContext* ctx) {
    if (!ctx) return;
    // 다음 히스토리 슬롯으로 이동
    m_frameIndex = (m_frameIndex + 1) % m_historySize;
    Frame& f = CurFrame();
    // 이전 데이터 초기화
    f.resolved.clear();
    f.pending.clear();
    f.nextTs = 0;
    f.disjointEnded = false;
    f.frameStartTicks = 0;
    f.disjoint = {};

    // 프레임 범위 쿼리 시작
    ctx->Begin(f.qDisjoint.Get());                    // disjoint Begin
    ctx->End(f.qFrameStart.Get());                    // frame start timestamp
}

void GpuProfilerD3D11::OnFrameEnd(ID3D11DeviceContext* ctx) {
    if (!ctx) return;
    Frame& f = CurFrame();
    if (!f.disjointEnded) {
        ctx->End(f.qDisjoint.Get());                  // disjoint End
        f.disjointEnded = true;
    }
}

uint32_t GpuProfilerD3D11::BeginEvent(ID3D11DeviceContext* ctx, const char* name) {
    auto qBegin = AcquireTsTLS();
    auto qEnd = AcquireTsTLS();
    if (ctx) ctx->End(qBegin.Get());                  // TIMESTAMP: 시작
    uint16_t depth = (uint16_t)s_tls.stack.size();
    s_tls.stack.push_back((uint32_t)s_tls.local.size());
    s_tls.local.push_back(Pending{ std::string(name), qBegin, qEnd, depth });
    return (uint32_t)s_tls.local.size() - 1;
}

void GpuProfilerD3D11::EndEvent(ID3D11DeviceContext* ctx, uint32_t token) {
    assert(token < s_tls.local.size());
    if (ctx) ctx->End(s_tls.local[token].qEnd.Get());   // TIMESTAMP: 끝
    if (!s_tls.stack.empty()) s_tls.stack.pop_back();
}


void GpuProfilerD3D11::StartDeferredStream() {
    s_tls.stack.clear();
    s_tls.local.clear();
    s_tls.nextTs = 0;
}

GpuProfilerD3D11::GpuStreamHandle GpuProfilerD3D11::EndDeferredStream() {
    GpuStreamHandle h;
    h.frameIndex = m_frameIndex; // assume same-frame submit
    h.events = std::move(s_tls.local);
    s_tls.local.clear();
    return h;
}

void GpuProfilerD3D11::AttachStream(GpuStreamHandle&& h) {
    // Attach to the recorded frame slot (avoid cross-frame attach)
    Frame& f = m_frames[h.frameIndex % m_historySize];
    // merge
    f.pending.reserve(f.pending.size() + h.events.size());
    for (auto& e : h.events) {
        f.pending.emplace_back(std::move(e));
    }
    h.events.clear();
}
void GpuProfilerD3D11::ResolvePending(uint32_t framesLatency) {
    if (!m_immediate) return;
    Frame& target = OldFrame(framesLatency);

    // disjoint / frameStart 먼저
    if (target.disjointEnded) {
        if (S_OK == m_immediate->GetData(target.qDisjoint.Get(), &target.disjoint, sizeof(target.disjoint),
            D3D11_ASYNC_GETDATA_DONOTFLUSH) &&
            !target.disjoint.Disjoint)
        {
            UINT64 tStart = 0;
            if (S_OK == m_immediate->GetData(target.qFrameStart.Get(), &tStart, sizeof(tStart),
                D3D11_ASYNC_GETDATA_DONOTFLUSH))
            {
                target.frameStartTicks = tStart;
            }
        }
    }

    // 이벤트들
    if (target.frameStartTicks != 0 && target.disjoint.Frequency > 0 && !target.disjoint.Disjoint) {
        const double toMs = 1000.0 / double(target.disjoint.Frequency);
        for (const auto& p : target.pending) {
            UINT64 tb = 0, te = 0;
            if (S_OK == m_immediate->GetData(p.qBegin.Get(), &tb, sizeof(tb), D3D11_ASYNC_GETDATA_DONOTFLUSH) &&
                S_OK == m_immediate->GetData(p.qEnd.Get(), &te, sizeof(te), D3D11_ASYNC_GETDATA_DONOTFLUSH) &&
                te >= tb)
            {
                ResolvedEvent e;
                e.name = p.name;
                e.depth = p.depth;
                e.beginMs = double(tb - target.frameStartTicks) * toMs;
                e.endMs = double(te - target.frameStartTicks) * toMs;
                target.resolved.emplace_back(std::move(e));
            }
        }
        target.pending.clear();
    }
}

const std::vector<GpuProfilerD3D11::ResolvedEvent>&
GpuProfilerD3D11::GetResolved(uint32_t historyIndex) const {
    uint32_t index = historyIndex % m_historySize;
    return m_frames[index].resolved;
}

#endif // BUILD_FLAG
