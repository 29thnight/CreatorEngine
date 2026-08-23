#include "VulkanPipelineCache.h"
#include "VulkanFormat.h"
#include "VulkanBindingModel.h"

#include <cstring>

using namespace VulkanApi;

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr uint64_t kVkHashOffset = 1469598103934665603ull;
    constexpr uint64_t kVkHashPrime = 1099511628211ull;

    void VkHashBytes(uint64_t& hash, const void* data, size_t bytes)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i)
        {
            hash ^= p[i];
            hash *= kVkHashPrime;
        }
    }

    /// `RHISamplerDesc` → `VkSamplerCreateInfo` (V8-b).
    ///
    /// ★ DX12 쪽 대응은 `DX12PipelineLayoutTranslate.h` 의 `ToD3D12(RHISamplerDesc)`
    ///   인데, 그쪽은 **필드 넷을 하나로 접는다**(D3D12_FILTER 가 min/mag·mip·
    ///   비교 여부를 한 값에 담는다). 이쪽은 접을 것이 없어 그대로 옮긴다 —
    ///   V4 가 "펴진 형태로 들고 DX12 백엔드가 접는다"고 정한 판단이 그대로
    ///   맞았다는 뜻이다. 반대로 뒀다면 여기서 매번 폈어야 한다.
    VkFilter VkFilterFromDesc(RHIFilterMode mode)
    {
        return (RHIFilterMode::Linear == mode) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    }

    /// ★ 셋뿐이다. V4 가 "어휘를 실사용에서 뽑았다 — 25곳이 쓰는 값이 놀랄
    ///   만큼 적다"고 적어 둔 그 결과이고, **두 번째 백엔드에서 그 판단이
    ///   값을 한다**: 옮길 것이 셋이라 대응표에 틀릴 자리가 거의 없다.
    ///   D3D12_TEXTURE_ADDRESS_MODE 다섯을 통째로 옮겼다면 둘은 소비자 없이
    ///   남았을 것이다.
    VkSamplerAddressMode VkAddressFromDesc(RHIAddressMode mode)
    {
        switch (mode)
        {
        case RHIAddressMode::Wrap:   return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        case RHIAddressMode::Border: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case RHIAddressMode::Clamp:
        default:                     return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        }
    }

    VkCompareOp VkCompareFromDesc(RHICompareOp op)
    {
        switch (op)
        {
        case RHICompareOp::Less:      return VK_COMPARE_OP_LESS;
        case RHICompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case RHICompareOp::None:
        default:                      return VK_COMPARE_OP_NEVER;
        }
    }

    bool VkCreateSamplerFromDesc(VkDevice device, const RHISamplerDesc& desc,
        VkSampler& outSampler, std::string& outError)
    {
        VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        info.magFilter = VkFilterFromDesc(desc.minMag);
        info.minFilter = VkFilterFromDesc(desc.minMag);
        info.mipmapMode = (RHIFilterMode::Linear == desc.mip)
            ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VkAddressFromDesc(desc.addressU);
        info.addressModeV = VkAddressFromDesc(desc.addressV);
        info.addressModeW = VkAddressFromDesc(desc.addressW);
        info.maxLod = desc.maxLod;

        // ★ 비교 샘플러의 유무가 DX12 에서는 필터 값에 접혀 있고 여기서는
        //   별도 플래그다. `RHICompareOp::None` 을 '비교 안 함'으로 읽는 것은
        //   V4 가 정한 어휘 그대로다 — 그 어휘가 이 구분을 이미 갖고 있어서
        //   백엔드가 만들어 낼 것이 없다.
        if (RHICompareOp::None != desc.compare)
        {
            info.compareEnable = VK_TRUE;
            info.compareOp = VkCompareFromDesc(desc.compare);
        }

        info.borderColor = (RHIBorderColor::OpaqueWhite == desc.border)
            ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        const VkResult result = vkCreateSampler(device, &info, nullptr, &outSampler);
        if (VK_SUCCESS != result)
        {
            outError = "샘플러 생성 실패 — " + ResultToString(result);
            return false;
        }
        return true;
    }

    template <typename T>
    void VkHashValue(uint64_t& hash, const T& value)
    {
        VkHashBytes(hash, &value, sizeof(T));
    }

    /// 문자열은 **내용**으로 해시한다. 포인터를 해시하면 같은 레이아웃이 매번
    /// 다른 키가 되어 캐시가 논다 — DX12 쪽이 루트 시그니처에서 겪은 함정이고
    /// (`RHIInputElement` 주석), 백엔드가 갈려도 같은 함정이다.
    void VkHashString(uint64_t& hash, const char* text)
    {
        if (nullptr == text) { VkHashValue(hash, 0u); return; }
        VkHashBytes(hash, text, std::strlen(text));
    }

    VkShaderStageFlags VkStageFlags(RHIShaderVisibility visibility)
    {
        switch (visibility)
        {
        case RHIShaderVisibility::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case RHIShaderVisibility::Pixel:  return VK_SHADER_STAGE_FRAGMENT_BIT;
        // All은 graphics 한정이 아니다. Forward+의 compute layout이 기본
        // visibility를 쓰므로 compute stage까지 포함해야 한다.
        default:                          return VK_SHADER_STAGE_ALL;
        }
    }

    VkPolygonMode VkPolygon(RHIFillMode mode)
    {
        return (RHIFillMode::Wireframe == mode) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    }

    VkCullModeFlags VkCull(RHICullMode mode)
    {
        switch (mode)
        {
        case RHICullMode::Back:  return VK_CULL_MODE_BACK_BIT;
        case RHICullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        default:                 return VK_CULL_MODE_NONE;
        }
    }

    VkCompareOp VkCompare(RHICompareOp op)
    {
        switch (op)
        {
        case RHICompareOp::Less:      return VK_COMPARE_OP_LESS;
        case RHICompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        default:                      return VK_COMPARE_OP_ALWAYS;
        }
    }

    VkBlendFactor VkBlend(RHIBlendFactor factor)
    {
        switch (factor)
        {
        case RHIBlendFactor::One:         return VK_BLEND_FACTOR_ONE;
        case RHIBlendFactor::SrcAlpha:    return VK_BLEND_FACTOR_SRC_ALPHA;
        case RHIBlendFactor::InvSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        default:                          return VK_BLEND_FACTOR_ZERO;
        }
    }

    VkPrimitiveTopology VkTopologyOf(RHITopologyType type)
    {
        switch (type)
        {
        case RHITopologyType::Line:  return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case RHITopologyType::Point: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default:                     return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }
    }
}

uint64_t VulkanPipelineCache::ComputeHash(const RHIGraphicsPipelineDesc& desc) const
{
    uint64_t hash = kVkHashOffset;

    if (nullptr != desc.vsBytecode) VkHashBytes(hash, desc.vsBytecode, desc.vsSize);
    if (nullptr != desc.psBytecode) VkHashBytes(hash, desc.psBytecode, desc.psSize);

    // ★ DX12 쪽은 여기서 핸들을 **안정 해시로 풀어** 넣어야 했다. PSO 디스크
    //   캐시의 키가 실행을 넘어 안정해야 하기 때문이다. Vulkan 쪽에는 아직
    //   디스크 캐시가 없어 그럴 이유가 없으므로 핸들 값 그대로 넣는다 —
    //   **같은 계약인데 백엔드마다 해시의 요구가 다르다.**
    VkHashValue(hash, desc.layout.id);

    VkHashValue(hash, desc.fillMode);
    VkHashValue(hash, desc.cullMode);
    VkHashValue(hash, desc.depthEnable);
    VkHashValue(hash, desc.depthWriteMask);
    VkHashValue(hash, desc.depthFunc);
    VkHashValue(hash, desc.blendEnable);
    VkHashValue(hash, desc.independentBlend);
    VkHashBytes(hash, desc.renderTargetBlend, sizeof(desc.renderTargetBlend));

    VkHashValue(hash, desc.inputElementCount);
    for (uint32_t i = 0; i < desc.inputElementCount; ++i)
    {
        const RHIInputElement& element = desc.inputElements[i];
        VkHashString(hash, element.semantic);
        VkHashValue(hash, element.semanticIndex);
        VkHashValue(hash, element.format);
        VkHashValue(hash, element.inputSlot);
        VkHashValue(hash, element.alignedByteOffset);
        VkHashValue(hash, element.instanceDataStepRate);
    }

    VkHashValue(hash, desc.topologyType);
    VkHashValue(hash, desc.numRenderTargets);
    VkHashBytes(hash, desc.rtvFormats, sizeof(desc.rtvFormats));
    VkHashValue(hash, desc.dsvFormat);
    VkHashValue(hash, desc.sampleCount);

    return hash;
}

uint64_t VulkanPipelineCache::ComputeHash(const RHIComputePipelineDesc& desc) const
{
    uint64_t hash = kVkHashOffset;
    constexpr uint32_t kComputeTag = 0x4353504Fu;
    VkHashValue(hash, kComputeTag);
    if (nullptr != desc.csBytecode) VkHashBytes(hash, desc.csBytecode, desc.csSize);
    VkHashValue(hash, desc.layout.id);
    return hash;
}

VulkanPipelineCache::~VulkanPipelineCache()
{
    Shutdown();
}

void VulkanPipelineCache::Shutdown()
{
    if (VK_NULL_HANDLE == m_device) return;

    for (const VulkanPipelineEntry& entry : m_pipelines)
    {
        if (VK_NULL_HANDLE != entry.pipeline) vkDestroyPipeline(m_device, entry.pipeline, nullptr);
    }
    m_pipelines.clear();
    m_pipelineByHash.clear();

    for (const VulkanPipelineLayoutEntry& entry : m_layouts)
    {
        if (VK_NULL_HANDLE != entry.layout)    vkDestroyPipelineLayout(m_device, entry.layout, nullptr);
        if (VK_NULL_HANDLE != entry.setLayout) vkDestroyDescriptorSetLayout(m_device, entry.setLayout, nullptr);

        // ★ 정적 샘플러는 셋 레이아웃보다 오래 살면 안 되고 짧아도 안 된다 —
        //   같은 자리에서 놓는 이유다(V8-b). DX12 쪽 캐시에는 이 줄이 없다.
        for (VkSampler sampler : entry.staticSamplers)
        {
            if (VK_NULL_HANDLE != sampler) vkDestroySampler(m_device, sampler, nullptr);
        }
    }
    m_layouts.clear();
    m_layoutByHash.clear();

    for (VkShaderModule module : m_modules)
    {
        if (VK_NULL_HANDLE != module) vkDestroyShaderModule(m_device, module, nullptr);
    }
    m_modules.clear();

    m_device = VK_NULL_HANDLE;
}

RHIPipelineLayoutHandle VulkanPipelineCache::GetOrCreate(
    const RHIPipelineLayoutDesc& desc, std::string& outError)
{
    // desc 를 내용으로 해시한다. span 이 가리키는 배열은 호출 동안만 살아
    // 있으면 된다는 계약이라(RHIPipelineLayout.h) 붙들지 않는다.
    uint64_t hash = kVkHashOffset;
    for (const RHIPipelineLayoutParam& param : desc.params)
    {
        VkHashValue(hash, param.kind);
        VkHashValue(hash, param.visibility);
        VkHashValue(hash, param.table);
        VkHashValue(hash, param.shaderRegister);
        VkHashValue(hash, param.constantCount);
    }
    for (const RHIStaticSamplerDesc& sampler : desc.staticSamplers)
    {
        VkHashBytes(hash, &sampler, sizeof(sampler));
    }
    VkHashValue(hash, desc.allowInputAssembler);

    if (const auto it = m_layoutByHash.find(hash); m_layoutByHash.end() != it)
    {
        return it->second;
    }

    // ★ 옮긴 것은 세 종류다 — ConstantBuffer(V8-a), DescriptorTable(V8-b),
    //   ShaderResourceBuffer(GizmoIcon root SRV).
    //   나머지 둘(UnorderedAccessBuffer · Constants)은
    //   소비자가 없어 그대로 둔다. 안 쓰는 종류를 옮기면 **틀려도 아무도
    //   모르는 대응표**가 된다(RHIPipelineState.h 의 규칙). 조용히 건너뛰지
    //   않고 실패시키는 것이 요점이다.
    //
    // ★ **binding 번호가 V8-b 에서 규약을 얻었다.** V8-a 는
    //   `binding.binding = param.shaderRegister` 였고 "지금은 b 하나뿐이라
    //   성립한다"고 적어 뒀는데, 텍스처가 들어오자 `b0` 과 `t0` 이 둘 다 0 이
    //   되어 충돌했다. 종류마다 구간을 나눈다 — `VulkanBindingModel.h`.
    //   셰이더를 굽는 쪽(dxc 의 -fvk-*-shift)이 같은 값을 쓴다.
    //
    // ★ 걸으면서 **슬롯 번호표**도 만든다 (5c-4d). 계약의 `slot` 은 DX12 의
    //   루트 파라미터 번호라, Vulkan 은 "몇 번째 칸이 어느 binding 인가"를
    //   레이아웃마다 들어야 한다 — `VulkanLayoutSlot` 의 ★ 참고.
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VulkanLayoutSlot> paramSlots;
    for (const RHIPipelineLayoutParam& param : desc.params)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.descriptorCount = 1;
        binding.stageFlags = VkStageFlags(param.visibility);

        switch (param.kind)
        {
        case RHILayoutParamKind::ConstantBuffer:
            binding.binding = VulkanBindingModel::kConstantBufferShift + param.shaderRegister;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings.push_back(binding);
            paramSlots.push_back({ binding.binding, 1, binding.descriptorType });
            break;

        case RHILayoutParamKind::ShaderResourceBuffer:
            // HLSL StructuredBuffer(tN)는 Vulkan storage buffer descriptor다.
            // DX12의 root SRV 주소는 SetRootBuffer가 이 한 칸으로 옮긴다.
            binding.binding = VulkanBindingModel::kShaderResourceShift + param.shaderRegister;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings.push_back(binding);
            paramSlots.push_back({ binding.binding, 1, binding.descriptorType });
            break;

        case RHILayoutParamKind::DescriptorTable:
        {
            // ★ **여기가 DX12 와 가장 크게 갈리는 자리다.** DX12 의 테이블은
            //   "연속한 레지스터 N개"를 루트 파라미터 **하나**로 묶는다
            //   (디스크립터 힙 안의 연속 구간을 가리키는 핸들 하나).
            //   Vulkan 에는 그 묶음이 없다 — 셋 레이아웃의 binding 을 N개
            //   따로 적는다. 즉 **한 파라미터가 N개 binding 으로 펼쳐진다.**
            //
            //   그래서 A-5 의 `RHIBindingTable`(GPU 핸들 하나)이 Vulkan 에서
            //   그대로 성립하지 않는다는 것이 여기서 실물로 확인된다.
            const RHIDescriptorRange& range = param.table;
            if (0 == range.count)
            {
                outError = "디스크립터 테이블의 범위가 비었다";
                return {};
            }

            uint32_t shift = 0;
            switch (range.type)
            {
            case RHIDescriptorType::ShaderResource:
                shift = VulkanBindingModel::kShaderResourceShift;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;
            case RHIDescriptorType::UnorderedAccess:
                shift = VulkanBindingModel::kUnorderedAccessShift;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;
            case RHIDescriptorType::UnorderedAccessBuffer:
                shift = VulkanBindingModel::kUnorderedAccessShift;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case RHIDescriptorType::Sampler:
                shift = VulkanBindingModel::kSamplerShift;
                binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                break;
            default:
                outError = "알 수 없는 디스크립터 종류다";
                return {};
            }

            for (uint32_t i = 0; i < range.count; ++i)
            {
                binding.binding = shift + range.baseRegister + i;
                bindings.push_back(binding);
            }

            // ★ 번호표에는 **첫 binding** 을 적는다. 계약이 테이블을 슬롯
            //   하나로 걸기 때문이고(`SetBindings(slot, table)`), N개로
            //   펼쳐지는 것은 백엔드 안쪽의 이야기다 — 나머지는 첫 번호에서
            //   순서대로 이어진다.
            paramSlots.push_back({ shift + range.baseRegister, range.count,
                binding.descriptorType });
            break;
        }

        default:
            outError = "아직 옮기지 않은 레이아웃 종류다 (루트 UAV · 루트 상수)";
            return {};
        }
    }

    VulkanPipelineLayoutEntry entry{};
    entry.paramSlots = std::move(paramSlots);

    // ── 정적 샘플러 (V8-b) ──
    //
    // ★ DX12 는 이 자리에 만들 객체가 없다 — 상태가 루트 시그니처 안에 값으로
    //   들어간다. Vulkan 은 `VkSampler` 를 만들어 `pImmutableSamplers` 로
    //   넘겨야 하고, 그 포인터는 **셋 레이아웃을 만드는 동안** 유효해야 하므로
    //   아래 vkCreateDescriptorSetLayout 전에 만들어 둔다.
    //
    //   V4 가 `D3D12_FILTER` 를 펴서 min/mag · mip · 비교 여부로 나눠 둔 것이
    //   여기서 값을 한다 — 접힌 값을 들고 왔다면 백엔드가 매번 그것을 펴야 했다.
    for (const RHIStaticSamplerDesc& staticSampler : desc.staticSamplers)
    {
        VkSampler sampler = VK_NULL_HANDLE;
        if (!VkCreateSamplerFromDesc(m_device, staticSampler.sampler, sampler, outError))
        {
            for (VkSampler made : entry.staticSamplers)
            {
                vkDestroySampler(m_device, made, nullptr);
            }
            return {};
        }
        entry.staticSamplers.push_back(sampler);

        VkDescriptorSetLayoutBinding binding{};
        binding.binding = VulkanBindingModel::kSamplerShift + staticSampler.shaderRegister;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VkStageFlags(staticSampler.visibility);
        binding.pImmutableSamplers = &entry.staticSamplers.back();
        bindings.push_back(binding);
    }

    // ★ 위 루프가 `&entry.staticSamplers.back()` 을 든다 — vector 가 자라면
    //   그 주소가 무효가 된다. 그래서 여기서 한 번에 다시 채운다. 이런 자리는
    //   "지금은 샘플러가 하나라 안 터진다"로 두면 둘째가 생기는 날 조용히
    //   틀린다(디스크립터가 남의 샘플러를 가리킨다).
    {
        size_t samplerIndex = 0;
        for (VkDescriptorSetLayoutBinding& binding : bindings)
        {
            if (nullptr != binding.pImmutableSamplers)
            {
                binding.pImmutableSamplers = &entry.staticSamplers[samplerIndex];
                ++samplerIndex;
            }
        }
    }

    VkDescriptorSetLayoutCreateInfo setInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

    // 실패 경로에서 정적 샘플러를 놓는다 — 여기서 새면 Shutdown 이 못 잡는다
    // (엔트리가 표에 안 들어갔으므로).
    const auto releaseSamplers = [&] {
        for (VkSampler sampler : entry.staticSamplers)
        {
            if (VK_NULL_HANDLE != sampler) vkDestroySampler(m_device, sampler, nullptr);
        }
        entry.staticSamplers.clear();
    };

    VkResult result = vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &entry.setLayout);
    if (VK_SUCCESS != result)
    {
        releaseSamplers();
        outError = "디스크립터 셋 레이아웃 생성 실패 — " + ResultToString(result);
        return {};
    }

    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &entry.setLayout;

    result = vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &entry.layout);
    if (VK_SUCCESS != result)
    {
        vkDestroyDescriptorSetLayout(m_device, entry.setLayout, nullptr);
        releaseSamplers();
        outError = "파이프라인 레이아웃 생성 실패 — " + ResultToString(result);
        return {};
    }

    // ★ `allowInputAssembler` 를 여기서 쓰지 않는다. DX12 는 루트 시그니처
    //   **플래그**로 받지만 Vulkan 은 파이프라인의 정점 입력 상태로 받는다 —
    //   RHIPipelineLayout.h 가 "어느 쪽이든 상위가 아는 사실은 하나뿐이라
    //   bool 로 든다"고 적어 둔 것이 맞았고, 그 bool 이 두 백엔드에서 서로
    //   다른 객체로 흘러간다는 것까지 확인된다.

    // 핸들의 슬롯이 곧 배열 인덱스다. 세대는 아직 안 든다 — 놓는 호출자가 0.
    m_layouts.push_back(std::move(entry));
    const RHIPipelineLayoutHandle handle{
        RHIHandleBits::Encode(static_cast<uint32_t>(m_layouts.size() - 1), 0) };
    m_layoutByHash.emplace(hash, handle);
    return handle;
}

VulkanPipelineLayoutEntry VulkanPipelineCache::Resolve(RHIPipelineLayoutHandle handle) const
{
    if (!handle.IsValid()) return {};
    const uint32_t slot = RHIHandleBits::SlotOf(handle.id);
    return (slot < m_layouts.size()) ? m_layouts[slot] : VulkanPipelineLayoutEntry{};
}

VulkanPipelineEntry VulkanPipelineCache::Resolve(RHIPipelineHandle handle) const
{
    if (!handle.IsValid()) return {};
    const uint32_t slot = RHIHandleBits::SlotOf(handle.id);
    return (slot < m_pipelines.size()) ? m_pipelines[slot] : VulkanPipelineEntry{};
}

VulkanLayoutSlot VulkanPipelineCache::ResolveParam(RHIPipelineLayoutHandle handle,
    uint32_t param) const
{
    if (!handle.IsValid()) return {};
    const uint32_t slot = RHIHandleBits::SlotOf(handle.id);
    if (slot >= m_layouts.size()) return {};

    const std::vector<VulkanLayoutSlot>& slots = m_layouts[slot].paramSlots;
    if (param >= slots.size()) return {};
    return slots[param];
}

RHIPipelineHandle VulkanPipelineCache::GetOrCreateCompute(const RHIComputePipelineDesc& desc,
    std::string& outError)
{
    if (nullptr == desc.csBytecode || 0 == desc.csSize)
    {
        outError = "컴퓨트 셰이더 바이트코드가 없다";
        return {};
    }

    const uint64_t hash = ComputeHash(desc);
    if (const auto it = m_pipelineByHash.find(hash); m_pipelineByHash.end() != it)
    {
        ++m_stats.memoryHits;
        return it->second;
    }

    const VulkanPipelineLayoutEntry layout = Resolve(desc.layout);
    if (!layout.IsValid())
    {
        ++m_stats.failures;
        outError = "컴퓨트 파이프라인 레이아웃 핸들이 유효하지 않다";
        return {};
    }

    VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    moduleInfo.codeSize = desc.csSize;
    moduleInfo.pCode = static_cast<const uint32_t*>(desc.csBytecode);
    VkShaderModule module = VK_NULL_HANDLE;
    VkResult result = vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module);
    if (VK_SUCCESS != result)
    {
        ++m_stats.failures;
        outError = "컴퓨트 셰이더 모듈 생성 실패 — " + ResultToString(result);
        return {};
    }
    m_modules.push_back(module);

    VkPipelineShaderStageCreateInfo stage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "CSMain";

    VkComputePipelineCreateInfo info{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    info.stage = stage;
    info.layout = layout.layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &info,
        nullptr, &pipeline);
    if (VK_SUCCESS != result)
    {
        ++m_stats.failures;
        outError = "컴퓨트 파이프라인 생성 실패 — " + ResultToString(result);
        return {};
    }

    ++m_stats.compiles;
    m_pipelines.push_back({ pipeline, layout.layout, layout.setLayout, desc.layout });
    const RHIPipelineHandle handle{
        RHIHandleBits::Encode(static_cast<uint32_t>(m_pipelines.size() - 1), 0) };
    m_pipelineByHash.emplace(hash, handle);
    return handle;
}

RHIPipelineHandle VulkanPipelineCache::GetOrCreate(const RHIGraphicsPipelineDesc& desc,
    std::string& outError)
{
    const uint64_t hash = ComputeHash(desc);

    if (const auto it = m_pipelineByHash.find(hash); m_pipelineByHash.end() != it)
    {
        ++m_stats.memoryHits;
        return it->second;
    }

    // ★ 캐시가 레이아웃을 **기억한다** — 그것이 핸들이 짝을 들 수 있는 근거다.
    //   호출부는 desc 에 레이아웃 핸들을 넣었을 뿐이고, 거는 시점에는 아무것도
    //   안 넘긴다(RHIHandle.h ★).
    const VulkanPipelineLayoutEntry layout = Resolve(desc.layout);
    if (!layout.IsValid())
    {
        ++m_stats.failures;
        outError = "파이프라인 레이아웃 핸들이 유효하지 않다";
        return {};
    }

    VkPipeline pipeline = CreateOne(desc, layout.layout, outError);
    if (VK_NULL_HANDLE == pipeline)
    {
        ++m_stats.failures;
        return {};
    }

    ++m_stats.compiles;
    m_pipelines.push_back({ pipeline, layout.layout, layout.setLayout, desc.layout });
    const RHIPipelineHandle handle{
        RHIHandleBits::Encode(static_cast<uint32_t>(m_pipelines.size() - 1), 0) };
    m_pipelineByHash.emplace(hash, handle);
    return handle;
}

VkPipeline VulkanPipelineCache::CreateOne(const RHIGraphicsPipelineDesc& desc,
    VkPipelineLayout layout, std::string& outError)
{
    if (nullptr == desc.vsBytecode || 0 == desc.vsSize)
    {
        outError = "정점 셰이더 바이트코드가 없다";
        return VK_NULL_HANDLE;
    }

    // Shadow처럼 색 출력을 전혀 쓰지 않는 파이프라인은 fragment stage가
    // 없어도 유효하다. PS 포인터와 크기는 둘 다 있거나 둘 다 없어야 한다.
    const bool hasPixelShader = nullptr != desc.psBytecode && 0 != desc.psSize;
    if ((nullptr != desc.psBytecode) != (0 != desc.psSize))
    {
        outError = "픽셀 셰이더 포인터와 크기가 서로 어긋난다";
        return VK_NULL_HANDLE;
    }


    auto createModule = [&](const void* code, size_t bytes, VkShaderModule& out) {
        VkShaderModuleCreateInfo info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        info.codeSize = bytes;
        info.pCode = static_cast<const uint32_t*>(code);
        const VkResult result = vkCreateShaderModule(m_device, &info, nullptr, &out);
        if (VK_SUCCESS != result)
        {
            outError = "셰이더 모듈 생성 실패 — " + ResultToString(result);
            return false;
        }
        m_modules.push_back(out);
        return true;
    };

    VkShaderModule vs = VK_NULL_HANDLE;
    VkShaderModule ps = VK_NULL_HANDLE;
    if (!createModule(desc.vsBytecode, desc.vsSize, vs)) return VK_NULL_HANDLE;
    if (hasPixelShader && !createModule(desc.psBytecode, desc.psSize, ps))
        return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "VSMain";
    if (hasPixelShader)
    {
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = ps;
        stages[1].pName = "PSMain";
    }

    // ★ 진입점 이름이 서명에 없다. DX12 는 컴파일 시점에 정하고 바이트코드에는
    //   진입점이 하나뿐이지만, SPIR-V 는 한 모듈에 여럿을 담을 수 있어
    //   **파이프라인 생성 시점에** 고른다. 지금은 VSMain/PSMain 으로 박아 두고,
    //   이것이 desc 로 올라와야 하는지는 소비자가 둘 이상 생길 때 정한다.

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkVertexInputBindingDescription vertexBinding{};
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    if (0 != desc.inputElementCount)
    {
        // 현재 RHIEncoder는 vertex slot 0 하나만 바인딩한다. 지원하지 않는
        // multi-stream/instance 입력을 조용히 slot 0으로 접지 않는다.
        vertexAttributes.reserve(desc.inputElementCount);
        for (uint32_t i = 0; i < desc.inputElementCount; ++i)
        {
            const RHIInputElement& element = desc.inputElements[i];
            if (0 != element.inputSlot || 0 != element.instanceDataStepRate)
            {
                outError = "Vulkan 정점 입력은 현재 vertex slot 0의 per-vertex 요소만 지원한다";
                return VK_NULL_HANDLE;
            }

            const VkFormat format = ToVulkan(element.format);
            if (VK_FORMAT_UNDEFINED == format)
            {
                outError = "Vulkan 정점 입력에 대응하지 않는 RHIFormat이다";
                return VK_NULL_HANDLE;
            }
            vertexAttributes.push_back(VkVertexInputAttributeDescription{
                i, element.inputSlot, format, element.alignedByteOffset });
        }

        // stride는 RHIEncoder::SetVertexBuffer의 인자다. 파이프라인에 임의로
        // 추론해 굽지 않고 동적 binding stride로 받는다.
        vertexBinding.binding = 0;
        vertexBinding.stride = 0;
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VkTopologyOf(desc.topologyType);

    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VkPolygon(desc.fillMode);
    raster.cullMode = VkCull(desc.cullMode);
    // 공용 mesh winding은 DX12의 FrontCounterClockwise=FALSE, 즉 clockwise
    // front를 기준으로 작성돼 있다. negative-height viewport로 Y축을 맞춰도
    // front-face 열거값 자체를 뒤집어 대신할 수는 없다.
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(
        (0 == desc.sampleCount) ? 1u : desc.sampleCount);

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = desc.depthEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable =
        (desc.depthEnable && RHIDepthWrite::All == desc.depthWriteMask) ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VkCompare(desc.depthFunc);

    // ★ 여기서 어휘 하나가 어긋난다. DX12 는 깊이 쓰기 마스크가 깊이 테스트와
    //   독립이라 `depthEnable=false` 여도 마스크 필드가 살아 있지만, Vulkan 은
    //   `depthTestEnable=false` 면 쓰기도 없다. 위에서 `depthEnable` 을 곱해
    //   맞췄다 — RHIDepthWrite 가 '쓰는가'만 말하기 때문에 백엔드가 이렇게
    //   접을 수 있다(V3 가 상태 어휘를 '무엇에 쓰는가'로 둔 것과 같은 값).

    VkPipelineColorBlendAttachmentState attachments[8]{};
    const uint32_t targetCount = desc.numRenderTargets;
    for (uint32_t i = 0; i < targetCount; ++i)
    {
        // 공용 blendEnable 경로는 RenderTarget[0] 의 고정 조합을 모든 타깃에
        // 적용한다 — DX12PSOManager 의 계약 그대로다.
        const RHIRenderTargetBlend& source = desc.independentBlend
            ? desc.renderTargetBlend[i] : desc.renderTargetBlend[0];

        VkPipelineColorBlendAttachmentState& target = attachments[i];
        if (desc.independentBlend)
        {
            target.blendEnable = source.enable ? VK_TRUE : VK_FALSE;
            target.srcColorBlendFactor = VkBlend(source.srcColor);
            target.dstColorBlendFactor = VkBlend(source.dstColor);
            target.colorBlendOp = VK_BLEND_OP_ADD;
            target.srcAlphaBlendFactor = VkBlend(source.srcAlpha);
            target.dstAlphaBlendFactor = VkBlend(source.dstAlpha);
            target.alphaBlendOp = VK_BLEND_OP_ADD;
            target.colorWriteMask = static_cast<VkColorComponentFlags>(source.writeMask);
        }
        else
        {
            target.blendEnable = desc.blendEnable ? VK_TRUE : VK_FALSE;
            target.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            target.colorBlendOp = VK_BLEND_OP_ADD;
            // DX12 공용 경로와 동일하게 대상 alpha를 보존한다. 표시용 공유
            // 텍스처의 alpha를 반투명 draw가 덮으면 최종 UI 합성에 구멍이 난다.
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
            target.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }
    }

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = targetCount;
    blend.pAttachments = attachments;

    // ★ 토폴로지를 동적 상태로 둔다. `RHIPrimitiveTopology`(드로우마다) 와
    //   `RHITopologyType`(파이프라인마다)을 V6 이 갈라 둔 것이 여기서 값을
    //   한다 — Vulkan 은 **부류**를 파이프라인에 굽고 구체 배열만 동적으로
    //   바꿀 수 있다. 두 어휘를 겹쳐 두었다면 인코더의 SetPrimitiveTopology 가
    //   파이프라인을 다시 구우라는 요구가 됐을 자리다.
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    };
    if (0 != desc.inputElementCount)
        dynamicStates.push_back(VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE);
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkFormat colorFormats[8]{};
    for (uint32_t i = 0; i < targetCount; ++i)
    {
        colorFormats[i] = ToVulkan(desc.rtvFormats[i]);
    }

    // 동적 렌더링. VkRenderPass 를 만들지 않는다 — DX12 에 대응물이 없는
    // 객체를 계약에 들이지 않는다(§7.2.2). 파이프라인이 아는 것은
    // '어떤 포맷의 타깃에 그리는가' 뿐이고, 그것은 DX12 의 rtvFormats 와
    // 같은 정보다.
    VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = targetCount;
    rendering.pColorAttachmentFormats = colorFormats;
    rendering.depthAttachmentFormat = ToVulkan(desc.dsvFormat);

    VkGraphicsPipelineCreateInfo info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering;
    info.stageCount = hasPixelShader ? 2u : 1u;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly;
    info.pViewportState = &viewportState;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info,
        nullptr, &pipeline);
    if (VK_SUCCESS != result)
    {
        outError = "그래픽 파이프라인 생성 실패 — " + ResultToString(result);
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

