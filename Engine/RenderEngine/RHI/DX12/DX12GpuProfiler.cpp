#include "DX12GpuProfiler.h"
#include "DX12Encoder.h"

#include <algorithm>
#include <sstream>
#include <string>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string ProfilerHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }
}

bool DX12GpuProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
    uint32_t maxPassesPerFrame, uint32_t frameCount, std::string& outError)
{
    if (nullptr == device || nullptr == queue || 0 == maxPassesPerFrame || 0 == frameCount)
    {
        outError = "GPU 프로파일러 인자가 잘못됐다";
        return false;
    }

    // 큐마다 타임스탬프 주파수가 다를 수 있다. 여기서 한 번 받아 둔다 —
    // 나중에 다른 큐의 값으로 나누면 조용히 틀린 시간이 나온다.
    const HRESULT freqResult = queue->GetTimestampFrequency(&m_ticksPerSecond);
    if (FAILED(freqResult) || 0 == m_ticksPerSecond)
    {
        outError = "타임스탬프 주파수 조회 실패 " + ProfilerHrToString(freqResult);
        return false;
    }

    m_queue = queue;
    m_maxPassesPerFrame = maxPassesPerFrame;
    m_frameCount = frameCount;

    // 세션 시작에서 한 번 뜬다(§5.1). 실패해도 초기화를 세우지는 않는다 —
    // 통합 축이 없을 뿐 패스별 시간과 queue 상대시간은 그대로 쓸 수 있다.
    // ★ 사유는 삼키지 않되 outError 에 넣지 않는다. 초기화는 성공이고
    //   outError 를 채우면 성공한 호출이 실패처럼 읽힌다. 진단으로 따로 든다.
    m_calibrationError.clear();
    SampleClockCalibration(m_calibrationError);

    // 레코드 크기를 여기서 한 번만 잡는다. 프레임마다 늘리면 병렬 기록 중에
    // 재할당이 일어나고, 그때 다른 워커가 들고 있던 참조가 무효가 된다.
    m_records.assign(static_cast<size_t>(maxPassesPerFrame) * frameCount, PassRecord{});

    // 슬롯마다 사용 수와 표를 따로 둔다. 이것이 없어서 수집이 "지금 기록
    // 중인 슬롯" 을 읽었다(§0.5.10).
    m_slotUsedPasses = std::make_unique<std::atomic<uint32_t>[]>(frameCount);
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        m_slotUsedPasses[i].store(0, std::memory_order_relaxed);
    }
    m_slotTokens.assign(frameCount, GpuFrameToken{});

    const uint32_t queriesPerFrame = maxPassesPerFrame * 2;   // 패스마다 시작·끝
    const uint32_t totalQueries = queriesPerFrame * frameCount;

    D3D12_QUERY_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    heapDesc.Count = totalQueries;

    HRESULT hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_queryHeap));
    if (FAILED(hr))
    {
        outError = "질의 힙 생성 실패 " + ProfilerHrToString(hr);
        return false;
    }

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = static_cast<uint64_t>(totalQueries) * sizeof(uint64_t);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_readback));
    if (FAILED(hr))
    {
        outError = "질의 리드백 버퍼 생성 실패 " + ProfilerHrToString(hr);
        return false;
    }

    m_queryHeap->SetName(L"DX12GpuProfilerQueries");
    m_readback->SetName(L"DX12GpuProfilerReadback");
    return true;
}

void DX12GpuProfiler::Shutdown()
{
    m_queue.Reset();
    m_calibration = ClockCalibration{};
    m_records.clear();
    m_readback.Reset();
    m_queryHeap.Reset();
    m_maxPassesPerFrame = 0;
    m_frameCount = 0;
    m_recordingSlot = 0;
    m_slotUsedPasses.reset();
    m_slotTokens.clear();
}

bool DX12GpuProfiler::SampleClockCalibration(std::string& outError)
{
    if (!m_queue)
    {
        outError = "clock calibration - 큐가 없다";
        return false;
    }

    uint64_t gpuTicks = 0;
    uint64_t cpuTicks = 0;
    const HRESULT hr = m_queue->GetClockCalibration(&gpuTicks, &cpuTicks);
    if (FAILED(hr))
    {
        // ★ 가진 표본을 버리지 않는다. 물러섬은 "통합 축을 끈다" 이지
        //   "맞춰 둔 것을 잃는다" 가 아니다.
        outError = "clock calibration 실패 " + ProfilerHrToString(hr);
        return false;
    }

    LARGE_INTEGER cpuFrequency{};
    if (!QueryPerformanceFrequency(&cpuFrequency) || 0 == cpuFrequency.QuadPart)
    {
        outError = "QPC 주파수 조회 실패";
        return false;
    }

    LARGE_INTEGER nowTick{};
    QueryPerformanceCounter(&nowTick);

    // ★ 덮어쓰기 **전에** 직전 표본으로 지금 GPU 틱을 예측해 본다. 덮어쓴 뒤에
    //   재면 자기 자신과 비교하게 되어 언제나 0 이 나온다.
    int64_t driftTicks = 0;
    if (m_calibration.valid)
    {
        const uint64_t predicted = GpuTickToCpuTick(gpuTicks);
        driftTicks = static_cast<int64_t>(cpuTicks) - static_cast<int64_t>(predicted);
    }

    const uint64_t previousCount = m_calibration.sampleCount;
    m_calibration.gpuTicks = gpuTicks;
    m_calibration.cpuTicks = cpuTicks;
    m_calibration.gpuTicksPerSecond = m_ticksPerSecond;
    m_calibration.cpuTicksPerSecond = static_cast<uint64_t>(cpuFrequency.QuadPart);
    m_calibration.lastSampleCpuTick = static_cast<uint64_t>(nowTick.QuadPart);
    m_calibration.sampleCount = previousCount + 1;
    m_calibration.lastDriftTicks = driftTicks;
    const int64_t absoluteDrift = (driftTicks < 0) ? -driftTicks : driftTicks;
    if (absoluteDrift > m_calibration.maxAbsoluteDriftTicks)
    {
        m_calibration.maxAbsoluteDriftTicks = absoluteDrift;
    }
    m_calibration.valid = (0 != m_ticksPerSecond);
    return m_calibration.valid;
}

void DX12GpuProfiler::RefreshClockCalibrationIfStale()
{
    if (!m_queue || 0 == m_calibration.cpuTicksPerSecond) return;

    LARGE_INTEGER nowTick{};
    QueryPerformanceCounter(&nowTick);
    const int64_t elapsed =
        nowTick.QuadPart - static_cast<int64_t>(m_calibration.lastSampleCpuTick);
    const int64_t interval = static_cast<int64_t>(
        static_cast<double>(m_calibration.cpuTicksPerSecond) * kCalibrationIntervalSeconds);
    if (elapsed < interval) return;

    m_calibrationError.clear();
    SampleClockCalibration(m_calibrationError);
}

uint64_t DX12GpuProfiler::GpuTickToCpuTick(uint64_t gpuTick) const
{
    if (!m_calibration.valid || 0 == m_calibration.gpuTicksPerSecond) return 0;

    const int64_t gpuDelta =
        static_cast<int64_t>(gpuTick) - static_cast<int64_t>(m_calibration.gpuTicks);
    const int64_t gpuFrequency = static_cast<int64_t>(m_calibration.gpuTicksPerSecond);
    const int64_t cpuFrequency = static_cast<int64_t>(m_calibration.cpuTicksPerSecond);

    // 몫과 나머지를 따로 옮긴다. 먼저 곱하면 몇 분만 지나도 64 비트를 넘고,
    // 먼저 나누면 초 미만이 통째로 잘린다.
    const int64_t wholeSeconds = gpuDelta / gpuFrequency;
    const int64_t remainder = gpuDelta % gpuFrequency;
    const int64_t cpuDelta = wholeSeconds * cpuFrequency + remainder * cpuFrequency / gpuFrequency;
    return static_cast<uint64_t>(static_cast<int64_t>(m_calibration.cpuTicks) + cpuDelta);
}

GpuFrameToken DX12GpuProfiler::BeginFrame(uint64_t engineFrameId,
    uint64_t submissionId, uint64_t renderViewId)
{
    if (0 == m_frameCount) return GpuFrameToken{};

    GpuFrameToken token;
    token.engineFrameId = engineFrameId;
    token.submissionId = submissionId;
    token.renderViewId = renderViewId;
    token.ringSlot = static_cast<uint32_t>(submissionId % m_frameCount);

    // 제출을 연 순간을 적는다. 이것이 없으면 변환한 GPU 시작이 "그 제출보다
    // 뒤인가" 를 물을 수 없고, 그러면 통합 축은 검산할 수 없는 숫자가 된다.
    // 제출을 열 때마다 묻되 간격이 막는다. 자기 자리에서 스스로 낡는 것을
    // 아는 편이, 부르는 쪽 어딘가에 타이머를 하나 더 두는 것보다 낫다.
    RefreshClockCalibrationIfStale();

    LARGE_INTEGER submitTick{};
    QueryPerformanceCounter(&submitTick);
    token.cpuSubmitTick = static_cast<uint64_t>(submitTick.QuadPart);

    m_recordingSlot = token.ringSlot;
    m_slotUsedPasses[token.ringSlot].store(0, std::memory_order_relaxed);
    m_slotTokens[token.ringSlot] = token;

    // 지우지 않고 표시만 되돌린다. clear/push_back은 재할당을 일으켜
    // 병렬 기록에서 성립하지 않는다 — 크기는 초기화 때 한 번만 잡는다.
    //
    // ★ **그 슬롯의 구간만** 되돌린다. 전부 되돌리면 인플라이트 제출의
    //   기록을 지우게 되고, 그것이 바로 이 페이즈가 고치는 결함이다.
    const size_t base = static_cast<size_t>(token.ringSlot) * m_maxPassesPerFrame;
    for (uint32_t i = 0; i < m_maxPassesPerFrame; ++i)
    {
        m_records[base + i].used = false;
    }
    return token;
}

GpuFrameToken DX12GpuProfiler::SlotToken(uint32_t ringSlot) const
{
    if (ringSlot >= m_slotTokens.size()) return GpuFrameToken{};
    return m_slotTokens[ringSlot];
}

uint32_t DX12GpuProfiler::BeginPass(RHIEncoder& encoder, const std::string& name)
{
    auto* const dx12Encoder = dynamic_cast<DX12Encoder*>(&encoder);
    return (nullptr != dx12Encoder)
        ? BeginPass(dx12Encoder->GetCommandList(), name) : kInvalidSlot;
}

void DX12GpuProfiler::EndPass(RHIEncoder& encoder, uint32_t slot)
{
    auto* const dx12Encoder = dynamic_cast<DX12Encoder*>(&encoder);
    if (nullptr != dx12Encoder) EndPass(dx12Encoder->GetCommandList(), slot);
}

uint32_t DX12GpuProfiler::BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name)
{
    if (!m_queryHeap || nullptr == commandList) return kInvalidSlot;

    // 슬롯을 원자적으로 예약한다. 병렬 기록에서는 여러 워커가 동시에 들어온다.
    const uint32_t slot =
        m_slotUsedPasses[m_recordingSlot].fetch_add(1, std::memory_order_relaxed);
    if (slot >= m_maxPassesPerFrame)
    {
        // 넘친 만큼 되돌리지 않는다. 되돌리면 다른 워커가 그 사이에 받은
        // 슬롯과 어긋난다 — 넘친 프레임은 그냥 일부를 잃는다.
        return kInvalidSlot;
    }

    const uint32_t base = (m_recordingSlot * m_maxPassesPerFrame + slot) * 2;

    PassRecord& record = m_records[RecordIndex(m_recordingSlot, slot)];
    record.name = name;
    record.beginQuery = base;
    record.endQuery = base + 1;
    record.used = true;

    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, base);
    return slot;
}

void DX12GpuProfiler::EndPass(ID3D12GraphicsCommandList* commandList, uint32_t slot)
{
    if (!m_queryHeap || nullptr == commandList || kInvalidSlot == slot) return;
    if (slot >= m_maxPassesPerFrame) return;

    commandList->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        m_records[RecordIndex(m_recordingSlot, slot)].endQuery);
}

void DX12GpuProfiler::ResolveFrame(ID3D12GraphicsCommandList* commandList,
    const GpuFrameToken& token)
{
    if (!token.IsValid() || token.ringSlot >= m_frameCount) return;

    const uint32_t used = (std::min)(
        m_slotUsedPasses[token.ringSlot].load(std::memory_order_relaxed),
        m_maxPassesPerFrame);
    if (!m_queryHeap || nullptr == commandList || 0 == used) return;

    // 이 제출 구간만 옮긴다. 전부 옮기면 다른 제출이 쓰는 중인 영역까지 건드린다.
    const uint32_t base = token.ringSlot * m_maxPassesPerFrame * 2;
    const uint32_t count = used * 2;

    commandList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        base, count, m_readback.Get(), static_cast<uint64_t>(base) * sizeof(uint64_t));
}

bool DX12GpuProfiler::Collect(const GpuFrameToken& token,
    FrameTimings& outTimings, std::string& outError)
{
    auto fail = [&outTimings](std::string& error, std::string text) -> bool
    {
        error = std::move(text);
        outTimings.slices.clear();
        outTimings.queueBeginTicks = 0;
        outTimings.queueEndTicks = 0;
        outTimings.busyTicks = 0;
        outTimings.droppedSlices = 0;
        outTimings.droppedSliceName.clear();
        outTimings.droppedSliceDeltaTicks = 0;
        outTimings.zeroLengthSlices = 0;
        outTimings.queueBeginCpuTicks = 0;
        outTimings.queueEndCpuTicks = 0;
        outTimings.cpuAligned = false;
        return false;
    };

    // ★ 그 슬롯이 아직 **그 제출의** 것인지 먼저 묻는다.
    //
    //   링이 짧고 제출은 인플라이트로 갈리므로, 펜스가 끝난 제출의 슬롯을
    //   뒤에 온 제출이 이미 다시 열었을 수 있다. 그때 숫자를 내면 그럴듯한데
    //   틀린 값이 된다 — 예전에 수집의 83% 가 그러고 있었다.
    if (!token.IsValid() || token.ringSlot >= m_frameCount)
    {
        return fail(outError, "GPU 수집 표가 비었다");
    }

    const GpuFrameToken& held = m_slotTokens[token.ringSlot];
    if (held.submissionId != token.submissionId)
    {
        return fail(outError, "GPU 수집 표가 낡았다 — 슬롯 " +
            std::to_string(token.ringSlot) + " 은 제출 " +
            std::to_string(held.submissionId) + " 의 것이고 물은 것은 " +
            std::to_string(token.submissionId) + " 이다");
    }

    outTimings.token = token;
    outTimings.ticksPerSecond = m_ticksPerSecond;
    outTimings.queueBeginTicks = 0;
    outTimings.queueEndTicks = 0;
    outTimings.busyTicks = 0;
    outTimings.droppedSlices = 0;
    outTimings.droppedSliceName.clear();
    outTimings.droppedSliceDeltaTicks = 0;
    outTimings.zeroLengthSlices = 0;
    outTimings.queueBeginCpuTicks = 0;
    outTimings.queueEndCpuTicks = 0;
    outTimings.cpuAligned = false;

    if (!m_readback || m_records.empty()) { outTimings.slices.clear(); return true; }

    const uint32_t base = token.ringSlot * m_maxPassesPerFrame * 2;
    const size_t offset = static_cast<size_t>(base) * sizeof(uint64_t);
    const uint32_t used = (std::min)(
        m_slotUsedPasses[token.ringSlot].load(std::memory_order_relaxed),
        m_maxPassesPerFrame);
    const size_t bytes = static_cast<size_t>(used) * 2 * sizeof(uint64_t);

    void* mapped = nullptr;
    D3D12_RANGE range{ offset, offset + bytes };
    const HRESULT hr = m_readback->Map(0, &range, &mapped);
    if (FAILED(hr))
    {
        return fail(outError, "질의 리드백 Map 실패 " + ProfilerHrToString(hr));
    }

    const auto* timestamps = static_cast<const uint64_t*>(mapped);
    const D3D12_RANGE emptyRange{ 0, 0 };

    // ★ 읽은 기록이 **그 슬롯의 것**인지 질의 인덱스로 검산한다.
    //
    //   질의 인덱스는 절대값이라 슬롯마다 구간이 갈라져 있다. 따라서 기록을
    //   엉뚱한 슬롯에서 집어 왔다면 그 인덱스가 이 구간 밖으로 나간다.
    //
    //   이 검산이 없으면 "표는 신선한데 기록을 다른 슬롯에서 읽는" 변이가
    //   조용히 살아남는다 — 신선도 검사와 기록 읽기가 서로 다른 것을 보기
    //   때문이다. 두 절이 같은 것을 물어야 한 절을 걷었을 때 드러난다.
    const uint32_t slotQueryBegin = base;
    const uint32_t slotQueryEnd = slotQueryBegin + m_maxPassesPerFrame * 2;

    // 크기를 먼저 맞춘다. 정상 상태에서는 패스 구성이 안 바뀜므로 같은 수가
    // 나오고, 그러면 resize 가 아무것도 하지 않아 문자열 버퍼가 그대로 살아있다.
    size_t sliceCount = 0;
    if (outTimings.slices.size() < used) outTimings.slices.resize(used);

    for (uint32_t i = 0; i < used; ++i)
    {
        const PassRecord& record = m_records[RecordIndex(token.ringSlot, i)];
        if (!record.used) continue;

        if (record.beginQuery < slotQueryBegin || record.endQuery >= slotQueryEnd)
        {
            m_readback->Unmap(0, &emptyRange);
            return fail(outError, "GPU 수집 기록이 슬롯 " +
                std::to_string(token.ringSlot) + " 의 질의 구간 밖을 가리킨다");
        }

        const uint64_t begin = timestamps[record.beginQuery];
        const uint64_t end = timestamps[record.endQuery];

        // 순서가 **뒤집힌** 값만 버린다. 큰 음수가 되어 합계를 망가뜨리기
        // 때문이고, 버렸다는 사실은 **센다** — 조용히 0 으로 바꾸면 수치가
        // 줄어든 것처럼 보인다.
        if (end < begin)
        {
            if (0 == outTimings.droppedSlices)
            {
                outTimings.droppedSliceName.assign(record.name);
                outTimings.droppedSliceDeltaTicks =
                    static_cast<int64_t>(end) - static_cast<int64_t>(begin);
            }
            ++outTimings.droppedSlices;
            continue;
        }

        // 길이 0 은 버리지 않는다. 그 패스는 그릴 것이 없어 일찍 빠져나갔을
        // 뿐이고, 없었던 것처럼 지우면 타임라인에서 그 패스가 통째로 사라진다.
        if (end == begin) ++outTimings.zeroLengthSlices;

        PassSlice& slice = outTimings.slices[sliceCount++];
        slice.name.assign(record.name);
        slice.beginTicks = begin;
        slice.endTicks = end;
    }

    outTimings.slices.resize(sliceCount);
    m_readback->Unmap(0, &emptyRange);

    if (0 == sliceCount) return true;

    // queue span 과 busy 를 가른다(§3.4).
    //
    //   queue span : 첫 timestamp 부터 마지막까지 — 그 제출이 큐를 잡고 있던 길이
    //   busy       : 겹치지 않는 실행 구간의 합 — 실제로 일한 길이
    //
    //   둘은 같지 않다. 간격이 있으면 busy 가 작고, 그 차이가 공백이다.
    //   조각을 단순히 더한 값은 세 번째 수이고, 겹침을 중복으로 센다.
    m_busyScratch.clear();
    m_busyScratch.reserve(sliceCount);
    uint64_t queueBegin = outTimings.slices[0].beginTicks;
    uint64_t queueEnd = outTimings.slices[0].endTicks;
    for (const PassSlice& slice : outTimings.slices)
    {
        queueBegin = (std::min)(queueBegin, slice.beginTicks);
        queueEnd = (std::max)(queueEnd, slice.endTicks);
        m_busyScratch.emplace_back(slice.beginTicks, slice.endTicks);
    }
    outTimings.queueBeginTicks = queueBegin;
    outTimings.queueEndTicks = queueEnd;

    std::sort(m_busyScratch.begin(), m_busyScratch.end());
    uint64_t busy = 0;
    uint64_t runBegin = m_busyScratch[0].first;
    uint64_t runEnd = m_busyScratch[0].second;
    for (size_t i = 1; i < m_busyScratch.size(); ++i)
    {
        const auto& span = m_busyScratch[i];
        if (span.first > runEnd)
        {
            busy += runEnd - runBegin;
            runBegin = span.first;
            runEnd = span.second;
            continue;
        }
        runEnd = (std::max)(runEnd, span.second);
    }
    busy += runEnd - runBegin;
    outTimings.busyTicks = busy;

    // queue span 을 CPU 축으로 옮긴다. 표본이 없으면 옮기지 않고 그렇다고 적는다 —
    // 0 을 그럴듯한 시각처럼 내보내면 정렬된 것과 구별되지 않는다.
    if (m_calibration.valid)
    {
        outTimings.queueBeginCpuTicks = GpuTickToCpuTick(queueBegin);
        outTimings.queueEndCpuTicks = GpuTickToCpuTick(queueEnd);
        outTimings.cpuAligned = true;
    }
    return true;
}

void DX12GpuProfiler::MergeSlices(const FrameTimings& timings,
    std::vector<PassTiming>& outTimings)
{
    m_lastTotalMs = 0.0;

    // 스크래치는 멤버다(선언부 주석) — clear 는 용량을 지우지 않는다.
    m_mergeScratch.clear();
    for (const PassSlice& slice : timings.slices)
    {
        auto found = m_mergeScratch.end();
        for (auto it = m_mergeScratch.begin(); it != m_mergeScratch.end(); ++it)
        {
            if (it->first == slice.name) { found = it; break; }
        }

        if (found == m_mergeScratch.end())
        {
            // 키는 slice.name 을 가리키는 view 다 — 사본을 만들지 않는다.
            m_mergeScratch.emplace_back(std::string_view{ slice.name },
                MergedSpan{ slice.beginTicks, slice.endTicks, 1 });
            continue;
        }

        found->second.begin = (std::min)(found->second.begin, slice.beginTicks);
        found->second.end = (std::max)(found->second.end, slice.endTicks);
        ++found->second.slices;
    }

    // clear + push_back 이 아니라 resize + 제자리 대입이다. 문자열이 이미 들고
    // 있는 버퍼를 재사용해야 프레임마다 새로 할당하지 않는다.
    outTimings.resize(m_mergeScratch.size());
    const double toMs = (timings.ticksPerSecond > 0)
        ? (1000.0 / static_cast<double>(timings.ticksPerSecond)) : 0.0;

    for (size_t i = 0; i < m_mergeScratch.size(); ++i)
    {
        const auto& entry = m_mergeScratch[i];
        PassTiming& timing = outTimings[i];

        timing.name.assign(entry.first);
        if (1 != entry.second.slices)
        {
            // std::to_string 은 할당한다 — 스택 버퍼에 찍어 붙인다.
            char suffix[24]{};
            const int len = std::snprintf(suffix, sizeof(suffix), "(x%u)", entry.second.slices);
            if (len > 0) { timing.name.append(suffix, static_cast<size_t>(len)); }
        }

        timing.milliseconds = (entry.second.end > entry.second.begin)
            ? static_cast<double>(entry.second.end - entry.second.begin) * toMs : 0.0;
        m_lastTotalMs += timing.milliseconds;
    }
}

