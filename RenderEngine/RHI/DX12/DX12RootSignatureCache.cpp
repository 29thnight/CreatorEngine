#ifndef DYNAMICCPP_EXPORTS
#include "DX12RootSignatureCache.h"
#include "DX12PipelineLayoutTranslate.h"

#include <sstream>
#include <vector>

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

uint64_t DX12RootSignatureCache::ComputeHash(const RHIPipelineLayoutDesc& desc)
{
    uint64_t hash = kRootSigFnvOffset;

    hash = RootSigHashValue(desc.allowInputAssembler, hash);

    const uint32_t paramCount = static_cast<uint32_t>(desc.params.size());
    const uint32_t samplerCount = static_cast<uint32_t>(desc.staticSamplers.size());
    hash = RootSigHashValue(paramCount, hash);
    hash = RootSigHashValue(samplerCount, hash);

    // 내용으로 해시한다. span 을 통째로 바이트 해시하면 주소를 해시하는 꼴이
    // 되고, 같은 레이아웃이 매번 다른 키가 되어 캐시가 통째로 논다.
    //
    // 구성 요소는 전부 열거와 정수라 통째로 해시해도 안전하다 — 다만 구조체
    // 패딩이 미초기화면 같은 내용이 다른 키가 되므로, 필드를 하나씩 넣는다.
    for (const RHIPipelineLayoutParam& param : desc.params)
    {
        hash = RootSigHashValue(param.kind, hash);
        hash = RootSigHashValue(param.visibility, hash);

        if (RHILayoutParamKind::DescriptorTable == param.kind)
        {
            hash = RootSigHashValue(param.table.type, hash);
            hash = RootSigHashValue(param.table.count, hash);
            hash = RootSigHashValue(param.table.baseRegister, hash);
            continue;
        }

        hash = RootSigHashValue(param.shaderRegister, hash);
        if (RHILayoutParamKind::Constants == param.kind)
        {
            hash = RootSigHashValue(param.constantCount, hash);
        }
    }

    for (const RHIStaticSamplerDesc& sampler : desc.staticSamplers)
    {
        hash = RootSigHashValue(sampler.sampler.minMag, hash);
        hash = RootSigHashValue(sampler.sampler.mip, hash);
        hash = RootSigHashValue(sampler.sampler.addressU, hash);
        hash = RootSigHashValue(sampler.sampler.addressV, hash);
        hash = RootSigHashValue(sampler.sampler.addressW, hash);
        hash = RootSigHashValue(sampler.sampler.compare, hash);
        hash = RootSigHashValue(sampler.sampler.border, hash);
        hash = RootSigHashValue(sampler.sampler.maxLod, hash);
        hash = RootSigHashValue(sampler.shaderRegister, hash);
        hash = RootSigHashValue(sampler.visibility, hash);
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
    const RHIPipelineLayoutDesc& desc, std::string& outError)
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

    // ── 중립 설명을 DX12 구조체로 조립한다 ──
    //
    // ★ 범위는 파라미터와 따로 담는다. D3D12_ROOT_PARAMETER 가 범위를
    //   포인터로 가리키므로, 범위를 파라미터 루프 안의 지역 변수로 두면
    //   루프를 빠져나가는 순간 죽은 주소를 가리킨다. 캐시 미스 때만 도는
    //   길이라 벡터 두 개의 비용은 문제가 되지 않는다.
    std::vector<D3D12_ROOT_PARAMETER> params(desc.params.size());
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges(desc.params.size());

    for (size_t i = 0; i < desc.params.size(); ++i)
    {
        const RHIPipelineLayoutParam& source = desc.params[i];
        D3D12_ROOT_PARAMETER& param = params[i];

        param.ParameterType = DX12Translate::ToD3D12(source.kind);
        param.ShaderVisibility = DX12Translate::ToD3D12(source.visibility);

        switch (source.kind)
        {
        case RHILayoutParamKind::DescriptorTable:
            ranges[i].RangeType = DX12Translate::ToD3D12(source.table.type);
            ranges[i].NumDescriptors = source.table.count;
            ranges[i].BaseShaderRegister = source.table.baseRegister;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = &ranges[i];
            break;

        case RHILayoutParamKind::Constants:
            param.Constants.ShaderRegister = source.shaderRegister;
            param.Constants.Num32BitValues = source.constantCount;
            break;

        default:
            param.Descriptor.ShaderRegister = source.shaderRegister;
            break;
        }
    }

    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers(desc.staticSamplers.size());
    for (size_t i = 0; i < desc.staticSamplers.size(); ++i)
    {
        samplers[i] = DX12Translate::ToD3D12(desc.staticSamplers[i]);
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = static_cast<UINT>(params.size());
    rootDesc.pParameters = params.empty() ? nullptr : params.data();
    rootDesc.NumStaticSamplers = static_cast<UINT>(samplers.size());
    rootDesc.pStaticSamplers = samplers.empty() ? nullptr : samplers.data();
    rootDesc.Flags = desc.allowInputAssembler
        ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        : D3D12_ROOT_SIGNATURE_FLAG_NONE;

    // 직렬화·생성은 락 밖에서 한다 — 다른 레이아웃의 생성을 막을 이유가 없다.
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
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
