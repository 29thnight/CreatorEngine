#ifndef DYNAMICCPP_EXPORTS
#include "VulkanPipelineCache.h"
#include "VulkanFormat.h"

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
        default:                          return VK_SHADER_STAGE_ALL_GRAPHICS;
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

    // ★ 지금 옮긴 것은 ConstantBuffer 한 종류다. 삼각형이 그것만 쓰기
    //   때문이고, 안 쓰는 종류를 옮기면 **틀려도 아무도 모르는 대응표**가
    //   된다(RHIPipelineState.h 가 D3D12 열거 전체를 옮겨 적지 않은 규칙).
    //   조용히 건너뛰지 않고 실패시키는 것이 요점이다 — 다음에 이 자리를
    //   때리는 패스가 생기면 여기서 멈춘다.
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const RHIPipelineLayoutParam& param : desc.params)
    {
        if (RHILayoutParamKind::ConstantBuffer != param.kind)
        {
            outError = "아직 옮기지 않은 레이아웃 종류다 (V8-a 는 ConstantBuffer 만 옮겼다)";
            return {};
        }

        VkDescriptorSetLayoutBinding binding{};
        // ★ `shaderRegister` 를 그대로 binding 번호로 쓴다. DX12 는 b·t·u·s 가
        //   각각 별개 이름공간인데 SPIR-V 는 binding 하나뿐이라, 종류가 섞이는
        //   순간 이 대입이 충돌한다. 지금은 b 하나뿐이라 성립한다 —
        //   성립하는 이유를 적어 둔다.
        binding.binding = param.shaderRegister;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VkStageFlags(param.visibility);
        bindings.push_back(binding);
    }

    if (!desc.staticSamplers.empty())
    {
        outError = "정적 샘플러는 아직 옮기지 않았다 (V8-a 범위 밖)";
        return {};
    }

    VulkanPipelineLayoutEntry entry{};

    VkDescriptorSetLayoutCreateInfo setInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setInfo.pBindings = bindings.empty() ? nullptr : bindings.data();

    VkResult result = vkCreateDescriptorSetLayout(m_device, &setInfo, nullptr, &entry.setLayout);
    if (VK_SUCCESS != result)
    {
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
        outError = "파이프라인 레이아웃 생성 실패 — " + ResultToString(result);
        return {};
    }

    // ★ `allowInputAssembler` 를 여기서 쓰지 않는다. DX12 는 루트 시그니처
    //   **플래그**로 받지만 Vulkan 은 파이프라인의 정점 입력 상태로 받는다 —
    //   RHIPipelineLayout.h 가 "어느 쪽이든 상위가 아는 사실은 하나뿐이라
    //   bool 로 든다"고 적어 둔 것이 맞았고, 그 bool 이 두 백엔드에서 서로
    //   다른 객체로 흘러간다는 것까지 확인된다.

    // 핸들의 슬롯이 곧 배열 인덱스다. 세대는 아직 안 든다 — 놓는 호출자가 0.
    m_layouts.push_back(entry);
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

RHIPipelineHandle VulkanPipelineCache::GetOrCreateCompute(const RHIComputePipelineDesc& desc,
    std::string& outError)
{
    (void)desc;
    outError = "컴퓨트 파이프라인은 아직 옮기지 않았다 (소비자 없음)";
    return {};
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
    m_pipelines.push_back({ pipeline, layout.layout });
    const RHIPipelineHandle handle{
        RHIHandleBits::Encode(static_cast<uint32_t>(m_pipelines.size() - 1), 0) };
    m_pipelineByHash.emplace(hash, handle);
    return handle;
}

VkPipeline VulkanPipelineCache::CreateOne(const RHIGraphicsPipelineDesc& desc,
    VkPipelineLayout layout, std::string& outError)
{
    if (nullptr == desc.vsBytecode || nullptr == desc.psBytecode)
    {
        outError = "셰이더 바이트코드가 없다";
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
    if (!createModule(desc.psBytecode, desc.psSize, ps)) return VK_NULL_HANDLE;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "VSMain";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName = "PSMain";

    // ★ 진입점 이름이 서명에 없다. DX12 는 컴파일 시점에 정하고 바이트코드에는
    //   진입점이 하나뿐이지만, SPIR-V 는 한 모듈에 여럿을 담을 수 있어
    //   **파이프라인 생성 시점에** 고른다. 지금은 VSMain/PSMain 으로 박아 두고,
    //   이것이 desc 로 올라와야 하는지는 소비자가 둘 이상 생길 때 정한다.

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    if (0 != desc.inputElementCount)
    {
        outError = "정점 입력 레이아웃은 아직 옮기지 않았다 (V8-a 범위 밖)";
        return VK_NULL_HANDLE;
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
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
    const uint32_t targetCount = (0 == desc.numRenderTargets) ? 1u : desc.numRenderTargets;
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
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
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
    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    };
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;

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
    info.stageCount = 2;
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

#endif
