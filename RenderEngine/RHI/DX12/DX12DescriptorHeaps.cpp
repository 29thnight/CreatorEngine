#ifndef DYNAMICCPP_EXPORTS
#include "DX12DescriptorHeaps.h"
#include "DX12PipelineLayoutTranslate.h"

#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string DescHeapHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    constexpr uint64_t kDescFnvOffset = 1469598103934665603ull;
    constexpr uint64_t kDescFnvPrime = 1099511628211ull;

    uint64_t DescHashBytes(const void* data, size_t size, uint64_t seed)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= kDescFnvPrime;
        }
        return hash;
    }
}

// ── DX12DescriptorRecycler ──

bool DX12DescriptorRecycler::Initialize(ID3D12Device* device,
    D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorsPerPage,
    uint32_t initialVersions, std::string& outError)
{
    if (nullptr == device || 0 == descriptorsPerPage || 0 == initialVersions)
    {
        outError = "디스크립터 recycler 인자가 잘못됐다";
        return false;
    }

    // 셰이더 가시 힙만 version 수명이 필요하다. CPU 전용 힙은 GPU가 draw 동안
    // 다시 읽지 않으므로 이 recycler에 넣는 것이 낭비다.
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        outError = "디스크립터 recycler는 CBV/SRV/UAV 힙 전용이다";
        return false;
    }

    Shutdown();
    m_device = device;
    m_type = type;
    m_descriptorsPerPage = descriptorsPerPage;
    m_initialVersions = initialVersions;
    m_incrementSize = device->GetDescriptorHandleIncrementSize(type);
    m_versions.Reset(initialVersions);
    m_pages.resize(initialVersions);

    for (uint32_t slot = 0; slot < initialVersions; ++slot)
    {
        if (!EnsurePage(slot, outError))
        {
            Shutdown();
            return false;
        }
    }

    m_stats.allocations.store(0, std::memory_order_relaxed);
    m_stats.descriptors.store(0, std::memory_order_relaxed);
    m_stats.overflows.store(0, std::memory_order_relaxed);
    m_stats.peakRecordingDescriptors.store(0, std::memory_order_relaxed);
    return true;
}

bool DX12DescriptorRecycler::EnsurePage(uint32_t slot, std::string& outError)
{
    if (!m_device || 0 == m_descriptorsPerPage) return false;
    if (slot >= m_pages.size()) m_pages.resize(static_cast<size_t>(slot) + 1);
    if (m_pages[slot]) return true;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = m_type;
    desc.NumDescriptors = m_descriptorsPerPage;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    auto page = std::make_unique<Page>();
    const HRESULT hr = m_device->CreateDescriptorHeap(
        &desc, IID_PPV_ARGS(&page->heap));
    if (FAILED(hr))
    {
        outError = "디스크립터 page 생성 실패 " + DescHeapHrToString(hr);
        return false;
    }

    page->cpuBase = page->heap->GetCPUDescriptorHandleForHeapStart();
    page->gpuBase = page->heap->GetGPUDescriptorHandleForHeapStart();
    page->cursor.store(0, std::memory_order_relaxed);
    const std::wstring name = L"DX12DescriptorPage." + std::to_wstring(slot);
    page->heap->SetName(name.c_str());
    m_pages[slot] = std::move(page);
    return true;
}

void DX12DescriptorRecycler::Shutdown()
{
    m_activePage = nullptr;
    m_activeVersion = {};
    m_pages.clear();
    m_versions.Reset();
    m_device.Reset();
    m_incrementSize = 0;
    m_descriptorsPerPage = 0;
    m_initialVersions = 0;
}

void DX12DescriptorRecycler::Collect(RHICompletionPoint completed)
{
    m_versions.Collect(completed);
}

bool DX12DescriptorRecycler::BeginRecording(uint64_t recordingId,
    std::string& outError)
{
    const RHIDescriptorVersionAcquire acquired =
        m_versions.BeginRecording(recordingId);
    if (!acquired.IsValid())
    {
        outError = "디스크립터 recording version 획득 실패";
        return false;
    }

    // policy의 같은 recordingId 재호출은 멱등이다. 이미 활성화된 page를
    // 되감으면 앞서 작성한 descriptor를 제자리에서 덮게 되므로 그대로 둔다.
    if (nullptr != m_activePage &&
        m_activeVersion.slot == acquired.handle.slot &&
        m_activeVersion.generation == acquired.handle.generation)
    {
        return true;
    }
    if (!EnsurePage(acquired.handle.slot, outError))
    {
        m_versions.AbortRecording(recordingId);
        return false;
    }

    m_activeVersion = acquired.handle;
    m_activePage = m_pages[acquired.handle.slot].get();
    m_activePage->cursor.store(0, std::memory_order_relaxed);
    return true;
}

void DX12DescriptorRecycler::OnSubmitted(uint64_t recordingId,
    RHICompletionPoint completion)
{
    if (!m_versions.OnSubmitted(recordingId, completion)) return;
    m_activePage = nullptr;
    m_activeVersion = {};
}

void DX12DescriptorRecycler::AbortRecording(uint64_t recordingId)
{
    if (!m_versions.AbortRecording(recordingId)) return;
    m_activePage = nullptr;
    m_activeVersion = {};
}

void DX12DescriptorRecycler::RecordPeak(uint32_t used)
{
    uint32_t peak = m_stats.peakRecordingDescriptors.load(std::memory_order_relaxed);
    while (used > peak &&
        !m_stats.peakRecordingDescriptors.compare_exchange_weak(
            peak, used, std::memory_order_relaxed))
    {
    }
}

DX12DescriptorRecycler::Allocation DX12DescriptorRecycler::Allocate(uint32_t count)
{
    Allocation allocation{};
    Page* const page = m_activePage;
    if (nullptr == page || !m_versions.IsCurrent(m_activeVersion) || 0 == count)
        return allocation;

    // page 하나를 worker들이 공유하므로 cursor 예약은 원자적이다.
    uint32_t current = page->cursor.load(std::memory_order_relaxed);
    uint32_t next = 0;

    do
    {
        if (count > m_descriptorsPerPage ||
            current > m_descriptorsPerPage - count)
        {
            m_stats.overflows.fetch_add(1, std::memory_order_relaxed);
            return allocation;
        }
        next = current + count;
    } while (!page->cursor.compare_exchange_weak(
        current, next, std::memory_order_relaxed));

    allocation.cpu = page->cpuBase;
    allocation.cpu.ptr += static_cast<SIZE_T>(current) * m_incrementSize;

    allocation.gpu = page->gpuBase;
    allocation.gpu.ptr += static_cast<UINT64>(current) * m_incrementSize;

    allocation.count = count;
    allocation.incrementSize = m_incrementSize;
    allocation.version = m_activeVersion.ToToken();

    m_stats.allocations.fetch_add(1, std::memory_order_relaxed);
    m_stats.descriptors.fetch_add(count, std::memory_order_relaxed);
    RecordPeak(next);

    return allocation;
}

// ── DX12TargetViewHeap ──

bool DX12TargetViewHeap::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t capacity, std::string& outError)
{
    if (nullptr == device || 0 == capacity)
    {
        outError = "타깃 뷰 힙 인자가 잘못됐다";
        return false;
    }

    // 받는 것은 '셰이더가 읽지 않는 디스크립터'다.
    //
    // 처음에는 RTV/DSV만 받았는데, R3에서 셋째가 생겼다 —
    // ClearUnorderedAccessViewFloat이 요구하는 **비셰이더 가시 UAV**다.
    // 성질은 RTV/DSV와 같다: 기록 시점에 소비되므로 프레임 구간을 나눌 필요가
    // 없다. 그래서 타입만 넓히고 구조는 그대로 둔다.
    //
    // 셰이더 가시 CBV/SRV/UAV는 DX12DescriptorRecycler 몫이다 — 그쪽은
    // GPU가 드로우 동안 읽으므로 프레임 구간과 펜스가 필요하다.
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV &&
        type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV &&
        type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        outError = "타깃 뷰 힙은 RTV/DSV/비가시 UAV 전용이다";
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = capacity;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr))
    {
        outError = "타깃 뷰 힙 생성 실패 " + DescHeapHrToString(hr);
        return false;
    }

    m_capacity = capacity;
    m_incrementSize = device->GetDescriptorHandleIncrementSize(type);
    m_cpuBase = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_cursor.store(0, std::memory_order_relaxed);
    return true;
}

void DX12TargetViewHeap::Shutdown()
{
    m_heap.Reset();
    m_cpuBase = {};
    m_incrementSize = 0;
    m_capacity = 0;
    m_cursor.store(0, std::memory_order_relaxed);
}

void DX12TargetViewHeap::BeginFrame()
{
    // 직전 프레임 사용량을 남긴다 — 용량을 정하는 유일한 근거다.
    const uint32_t used = m_cursor.load(std::memory_order_relaxed);
    uint32_t peak = m_peakFrameDescriptors.load(std::memory_order_relaxed);
    while (used > peak &&
        !m_peakFrameDescriptors.compare_exchange_weak(peak, used, std::memory_order_relaxed))
    {
    }

    m_cursor.store(0, std::memory_order_relaxed);
}

uint32_t DX12TargetViewHeap::Allocate(uint32_t count)
{
    if (!m_heap || 0 == count) return kInvalidIndex;

    uint32_t current = m_cursor.load(std::memory_order_relaxed);
    uint32_t next = 0;

    do
    {
        next = current + count;
        if (next > m_capacity)
        {
            m_overflows.fetch_add(1, std::memory_order_relaxed);
            return kInvalidIndex;
        }
    } while (!m_cursor.compare_exchange_weak(current, next, std::memory_order_relaxed));

    m_allocations.fetch_add(1, std::memory_order_relaxed);
    m_descriptors.fetch_add(count, std::memory_order_relaxed);
    return current;
}

DX12TargetViewHeap::Stats DX12TargetViewHeap::GetStats() const
{
    Stats stats{};
    stats.allocations = m_allocations.load(std::memory_order_relaxed);
    stats.descriptors = m_descriptors.load(std::memory_order_relaxed);
    stats.overflows = m_overflows.load(std::memory_order_relaxed);
    stats.peakFrameDescriptors = m_peakFrameDescriptors.load(std::memory_order_relaxed);
    return stats;
}

// ── DX12SamplerHeap ──

uint64_t DX12SamplerHeap::ComputeHash(const RHISamplerDesc& desc)
{
    // RHISamplerDesc는 포인터가 없는 POD라 통째로 해시해도 안전하다
    // (루트 시그니처 설명과 다른 점 — 그쪽은 span을 들고 있어 내용으로 훑어야 한다).
    return DescHashBytes(&desc, sizeof(desc), kDescFnvOffset);
}

bool DX12SamplerHeap::Initialize(ID3D12Device* device, uint32_t capacity, std::string& outError)
{
    if (nullptr == device || 0 == capacity)
    {
        outError = "샘플러 힙 인자가 잘못됐다";
        return false;
    }

    // 하드웨어 상한이 2048이다. 넘겨서 만들면 생성 자체가 실패하므로 여기서 자른다.
    constexpr uint32_t kMaxSamplers = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
    if (capacity > kMaxSamplers) capacity = kMaxSamplers;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    desc.NumDescriptors = capacity;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr))
    {
        outError = "샘플러 힙 생성 실패 " + DescHeapHrToString(hr);
        return false;
    }

    m_device = device;
    m_incrementSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    m_cpuBase = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_gpuBase = m_heap->GetGPUDescriptorHandleForHeapStart();
    m_capacity = capacity;
    m_used = 0;
    m_cache.clear();
    m_stats = Stats{};

    m_heap->SetName(L"DX12SamplerHeap");
    return true;
}

void DX12SamplerHeap::Shutdown()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_cache.clear();
    m_heap.Reset();
    m_device.Reset();
    m_capacity = 0;
    m_used = 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12SamplerHeap::GetOrCreate(const RHISamplerDesc& desc)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (!m_heap) return handle;

    const uint64_t hash = ComputeHash(desc);

    std::lock_guard<std::mutex> guard(m_mutex);

    const auto found = m_cache.find(hash);
    if (found != m_cache.end())
    {
        ++m_stats.hits;
        return found->second;
    }

    if (m_used >= m_capacity)
    {
        // 상한에 닿았다. 조용히 아무 샘플러나 돌려주면 화면이 미묘하게 틀리고
        // 원인을 찾기 어려우므로 무효를 돌려주고 센다.
        ++m_stats.overflows;
        return handle;
    }

    const D3D12_SAMPLER_DESC native = DX12Translate::ToD3D12(desc);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpuBase;
    cpu.ptr += static_cast<SIZE_T>(m_used) * m_incrementSize;
    m_device->CreateSampler(&native, cpu);

    handle = m_gpuBase;
    handle.ptr += static_cast<UINT64>(m_used) * m_incrementSize;

    ++m_used;
    ++m_stats.creates;
    m_cache.emplace(hash, handle);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12SamplerHeap::CreateRange(std::span<const RHISamplerDesc> descs)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (!m_heap || descs.empty()) return handle;

    const uint32_t count = static_cast<uint32_t>(descs.size());

    std::lock_guard<std::mutex> guard(m_mutex);

    if (m_used + count > m_capacity)
    {
        ++m_stats.overflows;
        return handle;
    }

    const uint32_t base = m_used;
    for (uint32_t i = 0; i < count; ++i)
    {
        const D3D12_SAMPLER_DESC native = DX12Translate::ToD3D12(descs[i]);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpuBase;
        cpu.ptr += static_cast<SIZE_T>(base + i) * m_incrementSize;
        m_device->CreateSampler(&native, cpu);
    }

    m_used += count;
    m_stats.creates += count;

    handle = m_gpuBase;
    handle.ptr += static_cast<UINT64>(base) * m_incrementSize;
    return handle;
}

DX12SamplerHeap::Stats DX12SamplerHeap::GetStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

size_t DX12SamplerHeap::GetCachedCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cache.size();
}

#endif
