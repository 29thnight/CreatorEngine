#ifndef DYNAMICCPP_EXPORTS
#include "DX12DescriptorHeaps.h"

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

// ── DX12DescriptorRing ──

bool DX12DescriptorRing::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
    uint32_t descriptorsPerFrame, uint32_t frameCount, std::string& outError)
{
    if (nullptr == device || 0 == descriptorsPerFrame || 0 == frameCount)
    {
        outError = "디스크립터 링 인자가 잘못됐다";
        return false;
    }

    // 셰이더 가시 힙만 링으로 쓸 이유가 있다. CPU 전용 힙은 GPU가 읽지 않으므로
    // 프레임 구간으로 나눌 필요가 없다 — 실수로 그렇게 쓰면 낭비라 막아 둔다.
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        outError = "디스크립터 링은 CBV/SRV/UAV 힙 전용이다(샘플러는 DX12SamplerHeap)";
        return false;
    }

    m_descriptorsPerFrame = descriptorsPerFrame;
    m_frameCount = frameCount;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = descriptorsPerFrame * frameCount;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));
    if (FAILED(hr))
    {
        outError = "디스크립터 링 힙 생성 실패 " + DescHeapHrToString(hr);
        return false;
    }

    m_incrementSize = device->GetDescriptorHandleIncrementSize(type);
    m_cpuBase = m_heap->GetCPUDescriptorHandleForHeapStart();
    m_gpuBase = m_heap->GetGPUDescriptorHandleForHeapStart();
    m_frameIndex = 0;
    m_cursor.store(0, std::memory_order_relaxed);
    m_stats.allocations.store(0, std::memory_order_relaxed);
    m_stats.descriptors.store(0, std::memory_order_relaxed);
    m_stats.overflows.store(0, std::memory_order_relaxed);
    m_stats.peakFrameDescriptors.store(0, std::memory_order_relaxed);

    m_heap->SetName(L"DX12DescriptorRing");
    return true;
}

void DX12DescriptorRing::Shutdown()
{
    m_heap.Reset();
    m_cpuBase = {};
    m_gpuBase = {};
    m_incrementSize = 0;
    m_descriptorsPerFrame = 0;
    m_frameCount = 0;
    m_frameIndex = 0;
    m_cursor = 0;
}

void DX12DescriptorRing::BeginFrame(uint32_t frameIndex)
{
    if (0 == m_frameCount) return;

    // 직전 프레임 사용량을 남긴다 — 구간 크기를 정하는 유일한 근거다.
    RecordPeak(m_cursor.load(std::memory_order_relaxed));

    m_frameIndex = frameIndex % m_frameCount;
    m_cursor.store(0, std::memory_order_relaxed);
}

void DX12DescriptorRing::RecordPeak(uint32_t used)
{
    uint32_t peak = m_stats.peakFrameDescriptors.load(std::memory_order_relaxed);
    while (used > peak &&
        !m_stats.peakFrameDescriptors.compare_exchange_weak(peak, used, std::memory_order_relaxed))
    {
    }
}

DX12DescriptorRing::Allocation DX12DescriptorRing::Allocate(uint32_t count)
{
    Allocation allocation{};
    if (!m_heap || 0 == count) return allocation;

    // 예약을 한 연산으로 묶는다(업로드 링과 같은 이유).
    uint32_t current = m_cursor.load(std::memory_order_relaxed);
    uint32_t next = 0;

    do
    {
        next = current + count;
        if (next > m_descriptorsPerFrame)
        {
            m_stats.overflows.fetch_add(1, std::memory_order_relaxed);
            return allocation;
        }
    } while (!m_cursor.compare_exchange_weak(current, next, std::memory_order_relaxed));

    const uint32_t absoluteIndex = m_frameIndex * m_descriptorsPerFrame + current;

    allocation.cpu = m_cpuBase;
    allocation.cpu.ptr += static_cast<SIZE_T>(absoluteIndex) * m_incrementSize;

    allocation.gpu = m_gpuBase;
    allocation.gpu.ptr += static_cast<UINT64>(absoluteIndex) * m_incrementSize;

    allocation.count = count;
    allocation.incrementSize = m_incrementSize;

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

    // 링이 CBV/SRV/UAV만 받는 것과 짝이다. 이쪽은 그 반대만 받는다 —
    // 둘 중 어디에 넣어야 하는지가 타입만 보고 정해진다.
    if (type != D3D12_DESCRIPTOR_HEAP_TYPE_RTV && type != D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        outError = "타깃 뷰 힙은 RTV/DSV 전용이다(CBV/SRV/UAV는 DX12DescriptorRing)";
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

uint64_t DX12SamplerHeap::ComputeHash(const D3D12_SAMPLER_DESC& desc)
{
    // D3D12_SAMPLER_DESC는 포인터가 없는 POD라 통째로 해시해도 안전하다
    // (루트 시그니처 설명과 다른 점 — 그쪽은 포인터를 들고 있어 내용으로 훑어야 했다).
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

D3D12_GPU_DESCRIPTOR_HANDLE DX12SamplerHeap::GetOrCreate(const D3D12_SAMPLER_DESC& desc)
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

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpuBase;
    cpu.ptr += static_cast<SIZE_T>(m_used) * m_incrementSize;
    m_device->CreateSampler(&desc, cpu);

    handle = m_gpuBase;
    handle.ptr += static_cast<UINT64>(m_used) * m_incrementSize;

    ++m_used;
    ++m_stats.creates;
    m_cache.emplace(hash, handle);
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE DX12SamplerHeap::CreateRange(const D3D12_SAMPLER_DESC* descs,
    uint32_t count)
{
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (!m_heap || nullptr == descs || 0 == count) return handle;

    std::lock_guard<std::mutex> guard(m_mutex);

    if (m_used + count > m_capacity)
    {
        ++m_stats.overflows;
        return handle;
    }

    const uint32_t base = m_used;
    for (uint32_t i = 0; i < count; ++i)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_cpuBase;
        cpu.ptr += static_cast<SIZE_T>(base + i) * m_incrementSize;
        m_device->CreateSampler(&descs[i], cpu);
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
