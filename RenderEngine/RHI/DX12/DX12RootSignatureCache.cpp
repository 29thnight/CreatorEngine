#ifndef DYNAMICCPP_EXPORTS
#include "DX12RootSignatureCache.h"

#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string RootSigHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    constexpr uint64_t kRootSigFnvOffset = 1469598103934665603ull;
    constexpr uint64_t kRootSigFnvPrime = 1099511628211ull;

    uint64_t RootSigHashBytes(const void* data, size_t size, uint64_t seed)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = seed;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= kRootSigFnvPrime;
        }
        return hash;
    }

    template <typename T>
    uint64_t RootSigHashValue(const T& value, uint64_t seed)
    {
        return RootSigHashBytes(&value, sizeof(T), seed);
    }
}

uint64_t DX12RootSignatureCache::ComputeHash(const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    uint64_t hash = kRootSigFnvOffset;

    hash = RootSigHashValue(desc.Flags, hash);
    hash = RootSigHashValue(desc.NumParameters, hash);
    hash = RootSigHashValue(desc.NumStaticSamplers, hash);

    // 파라미터는 포인터를 따라 들어가 내용으로 해시한다. 구조체를 통째로
    // 바이트 해시하면 union 뒤에 있는 포인터(pDescriptorRanges)를 해시하게 되고,
    // 같은 레이아웃이 매번 다른 키가 된다.
    for (uint32_t i = 0; i < desc.NumParameters; ++i)
    {
        const D3D12_ROOT_PARAMETER& param = desc.pParameters[i];
        hash = RootSigHashValue(param.ParameterType, hash);
        hash = RootSigHashValue(param.ShaderVisibility, hash);

        switch (param.ParameterType)
        {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            hash = RootSigHashValue(param.DescriptorTable.NumDescriptorRanges, hash);
            for (uint32_t r = 0; r < param.DescriptorTable.NumDescriptorRanges; ++r)
            {
                // D3D12_DESCRIPTOR_RANGE는 포인터가 없는 POD라 통째로 안전하다.
                hash = RootSigHashValue(param.DescriptorTable.pDescriptorRanges[r], hash);
            }
            break;

        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            hash = RootSigHashValue(param.Constants, hash);
            break;

        case D3D12_ROOT_PARAMETER_TYPE_CBV:
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            hash = RootSigHashValue(param.Descriptor, hash);
            break;

        default:
            break;
        }
    }

    for (uint32_t i = 0; i < desc.NumStaticSamplers; ++i)
    {
        // 정적 샘플러도 POD.
        hash = RootSigHashValue(desc.pStaticSamplers[i], hash);
    }

    return hash;
}

bool DX12RootSignatureCache::Initialize(ID3D12Device* device, std::string& outError)
{
    if (nullptr == device)
    {
        outError = "디바이스가 없다";
        return false;
    }

    m_device = device;
    m_cache.clear();
    m_stats = Stats{};
    return true;
}

void DX12RootSignatureCache::Shutdown()
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_cache.clear();
    m_device.Reset();
}

DX12RootSignatureCache::Entry DX12RootSignatureCache::GetOrCreate(
    const D3D12_ROOT_SIGNATURE_DESC& desc, std::string& outError)
{
    Entry entry{};
    if (!m_device)
    {
        outError = "루트 시그니처 캐시가 초기화되지 않았다";
        return entry;
    }

    const uint64_t hash = ComputeHash(desc);
    entry.id = hash;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        const auto found = m_cache.find(hash);
        if (found != m_cache.end())
        {
            ++m_stats.hits;
            entry.signature = found->second.Get();
            return entry;
        }
    }

    // 직렬화·생성은 락 밖에서 한다 — 다른 레이아웃의 생성을 막을 이유가 없다.
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
        &serialized, &errors);
    if (FAILED(hr))
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.failures;
        outError = "루트 시그니처 직렬화 실패 " + RootSigHrToString(hr);
        if (errors) { outError += ": "; outError += static_cast<const char*>(errors->GetBufferPointer()); }
        return entry;
    }

    ComPtr<ID3D12RootSignature> signature;
    hr = m_device->CreateRootSignature(0, serialized->GetBufferPointer(),
        serialized->GetBufferSize(), IID_PPV_ARGS(&signature));
    if (FAILED(hr))
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        ++m_stats.failures;
        outError = "루트 시그니처 생성 실패 " + RootSigHrToString(hr);
        return entry;
    }

    std::lock_guard<std::mutex> guard(m_mutex);

    // 락 밖에서 만드는 동안 다른 스레드가 먼저 넣었을 수 있다. 그러면 그쪽을
    // 쓴다 — 같은 레이아웃은 객체도 하나여야 한다는 것이 이 캐시의 약속이다.
    const auto inserted = m_cache.emplace(hash, signature);
    if (!inserted.second)
    {
        ++m_stats.hits;
    }
    else
    {
        ++m_stats.creates;
    }

    entry.signature = inserted.first->second.Get();
    return entry;
}

DX12RootSignatureCache::Stats DX12RootSignatureCache::GetStats() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_stats;
}

size_t DX12RootSignatureCache::GetCachedCount() const
{
    std::lock_guard<std::mutex> guard(m_mutex);
    return m_cache.size();
}

#endif
