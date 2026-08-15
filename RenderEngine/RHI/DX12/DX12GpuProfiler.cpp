#ifndef DYNAMICCPP_EXPORTS
#include "DX12GpuProfiler.h"
#include "DX12Encoder.h"

#include <algorithm>
#include <sstream>

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

    m_maxPassesPerFrame = maxPassesPerFrame;
    m_frameCount = frameCount;

    // 레코드 크기를 여기서 한 번만 잡는다. 프레임마다 늘리면 병렬 기록 중에
    // 재할당이 일어나고, 그때 다른 워커가 들고 있던 참조가 무효가 된다.
    m_records.assign(maxPassesPerFrame, PassRecord{});

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
    m_records.clear();
    m_readback.Reset();
    m_queryHeap.Reset();
    m_maxPassesPerFrame = 0;
    m_frameCount = 0;
    m_usedPasses.store(0, std::memory_order_relaxed);
}

void DX12GpuProfiler::BeginFrame(uint32_t frameIndex)
{
    if (0 == m_frameCount) return;
    m_frameIndex = frameIndex % m_frameCount;
    m_usedPasses.store(0, std::memory_order_relaxed);

    // 지우지 않고 표시만 되돌린다. clear/push_back은 재할당을 일으켜
    // 병렬 기록에서 성립하지 않는다 — 크기는 초기화 때 한 번만 잡는다.
    for (auto& record : m_records) record.used = false;
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
    const uint32_t slot = m_usedPasses.fetch_add(1, std::memory_order_relaxed);
    if (slot >= m_maxPassesPerFrame)
    {
        // 넘친 만큼 되돌리지 않는다. 되돌리면 다른 워커가 그 사이에 받은
        // 슬롯과 어긋난다 — 넘친 프레임은 그냥 일부를 잃는다.
        return kInvalidSlot;
    }

    const uint32_t base = (m_frameIndex * m_maxPassesPerFrame + slot) * 2;

    PassRecord& record = m_records[slot];
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
        m_records[slot].endQuery);
}

void DX12GpuProfiler::ResolveFrame(ID3D12GraphicsCommandList* commandList)
{
    const uint32_t used = (std::min)(m_usedPasses.load(std::memory_order_relaxed),
        m_maxPassesPerFrame);
    if (!m_queryHeap || nullptr == commandList || 0 == used) return;

    // 이 프레임 구간만 옮긴다. 전부 옮기면 다른 프레임이 쓰는 중인 영역까지 건드린다.
    const uint32_t base = m_frameIndex * m_maxPassesPerFrame * 2;
    const uint32_t count = used * 2;

    commandList->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
        base, count, m_readback.Get(), static_cast<uint64_t>(base) * sizeof(uint64_t));
}

bool DX12GpuProfiler::Collect(std::vector<PassTiming>& outTimings, std::string& outError)
{
    outTimings.clear();
    m_lastTotalMs = 0.0;

    if (!m_readback || m_records.empty()) return true;

    const uint32_t base = m_frameIndex * m_maxPassesPerFrame * 2;
    const size_t offset = static_cast<size_t>(base) * sizeof(uint64_t);
    const uint32_t used = (std::min)(m_usedPasses.load(std::memory_order_relaxed),
        m_maxPassesPerFrame);
    const size_t bytes = static_cast<size_t>(used) * 2 * sizeof(uint64_t);

    void* mapped = nullptr;
    D3D12_RANGE range{ offset, offset + bytes };
    const HRESULT hr = m_readback->Map(0, &range, &mapped);
    if (FAILED(hr))
    {
        outError = "질의 리드백 Map 실패 " + ProfilerHrToString(hr);
        return false;
    }

    const auto* timestamps = static_cast<const uint64_t*>(mapped);

    // 같은 이름이 여러 번 나올 수 있다.
    //
    // 분할 패스는 조각마다 구간을 찍으므로 한 패스가 여러 레코드가 된다.
    // 그것을 그대로 나열하면 "GBuffer가 여섯 번 있다"가 되어 읽을 수 없다.
    // 이름으로 묶어 처음 시작~마지막 끝으로 합친다 — GPU 타임라인은 하나이고
    // 조각들은 순서대로 실행되므로 그 구간이 곧 패스 전체 시간이다.
    struct Merged
    {
        uint64_t begin{ 0 };
        uint64_t end{ 0 };
        uint32_t slices{ 0 };
    };
    std::vector<std::pair<std::string, Merged>> merged;

    for (uint32_t i = 0; i < used; ++i)
    {
        const PassRecord& record = m_records[i];
        if (!record.used) continue;

        const uint64_t begin = timestamps[record.beginQuery];
        const uint64_t end = timestamps[record.endQuery];

        auto found = merged.end();
        for (auto it = merged.begin(); it != merged.end(); ++it)
        {
            if (it->first == record.name) { found = it; break; }
        }

        if (found == merged.end())
        {
            merged.emplace_back(record.name, Merged{ begin, end, 1 });
            continue;
        }

        found->second.begin = (std::min)(found->second.begin, begin);
        found->second.end = (std::max)(found->second.end, end);
        ++found->second.slices;
    }

    for (const auto& entry : merged)
    {
        PassTiming timing{};
        timing.name = (1 == entry.second.slices)
            ? entry.first
            : entry.first + "(x" + std::to_string(entry.second.slices) + ")";

        // 순서가 뒤집힌 값은 버린다. 큐가 비어 있거나 질의가 기록되지 않은
        // 경우인데, 그대로 빼면 거대한 음수가 나와 합계를 망친다.
        timing.milliseconds = (entry.second.end > entry.second.begin)
            ? (static_cast<double>(entry.second.end - entry.second.begin)
                / static_cast<double>(m_ticksPerSecond)) * 1000.0
            : 0.0;

        m_lastTotalMs += timing.milliseconds;
        outTimings.push_back(std::move(timing));
    }

    // 쓰지 않았으므로 빈 범위로 Unmap한다.
    const D3D12_RANGE emptyRange{ 0, 0 };
    m_readback->Unmap(0, &emptyRange);
    return true;
}

#endif
