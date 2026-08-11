#ifndef DYNAMICCPP_EXPORTS
#include "VulkanTrianglePass.h"
#include "VulkanDeviceResources.h"
#include "Shaders/VkTriangleSpv.h"

#include "../RHIShaderBlob.h"

#include <cstring>

using namespace VulkanApi;

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.

    /// 셰이더 바이트코드를 중립 블롭에 담는다.
    ///
    /// ★ DX12 패스는 여기서 `DX12ShaderCompiler::CompileFile(파일, 진입점,
    ///   타깃, RHIShaderBlob&, 오류)` 를 부른다. Vulkan 에는 OS 가 주는
    ///   컴파일러가 없어 **컴파일이 아니라 조회**다(§7.2.2 의 설계 판단 셋째).
    ///
    ///   즉 V5 가 세운 경계 중 **결과(`RHIShaderBlob`)는 그대로 성립하고
    ///   호출(`CompileFile`)은 성립하지 않는다.** 경계가 값에서는 맞고 동작에서
    ///   갈린다 — MaterialPipelinePlan 의 M1 이 이 자리를 받는다.
    bool VkTriangleLoadShader(const uint32_t* words, size_t bytes, RHIShaderBlob& outBlob,
        std::string& outError)
    {
        if (nullptr == words || 0 == bytes)
        {
            outError = "미리 뽑아 둔 SPIR-V 가 없다 — pwsh scripts/build_vk_shaders.ps1";
            return false;
        }
        outBlob.Assign(words, bytes);
        return true;
    }
}

bool VulkanTrianglePass::Initialize(const VulkanFrameContext& context, std::string& outError)
{
    if (nullptr == context.resources || nullptr == context.pipelineCache)
    {
        outError = "삼각형 패스 컨텍스트가 불완전하다";
        return false;
    }

    m_device = context.resources->GetDevice();
    if (VK_NULL_HANDLE == m_device)
    {
        outError = "디바이스가 없다";
        return false;
    }

    if (!CreatePipelines(context, outError)) return false;
    if (!CreateConstantBuffer(context, outError)) return false;

    m_cacheStats = context.pipelineCache->GetStats();
    return true;
}

bool VulkanTrianglePass::CreatePipelines(const VulkanFrameContext& context,
    std::string& outError)
{
    // ★ 그리드 패스와 **글자 그대로 같은 레이아웃**이다:
    //
    //     const RHIPipelineLayoutParam params[] = { RHILayout::Cbv(0) };
    //     RHIPipelineLayoutDesc rootDesc{};
    //     rootDesc.params = params;
    //
    //   V4 가 만든 중립 desc 가 두 번째 백엔드에서 성립하는지를 재는 자리이고,
    //   위 세 줄은 DX12 쪽에서 복사해 온 것이지 고쳐 쓴 것이 아니다.
    const RHIPipelineLayoutParam params[] = { RHILayout::Cbv(0) };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;

    const RHIPipelineLayoutHandle root = context.pipelineCache->GetOrCreate(rootDesc, outError);
    if (!root.IsValid()) return false;
    m_layout = root;

    RHIShaderBlob vsBlob;
    RHIShaderBlob psBlob;
    if (!VkTriangleLoadShader(kVkTriangleVsSpv, sizeof(kVkTriangleVsSpv), vsBlob, outError))
    {
        return false;
    }
    if (!VkTriangleLoadShader(kVkTrianglePsSpv, sizeof(kVkTrianglePsSpv), psBlob, outError))
    {
        return false;
    }

    RHIGraphicsPipelineDesc desc{};
    desc.vsBytecode = vsBlob.Data();
    desc.vsSize = vsBlob.Size();
    desc.psBytecode = psBlob.Data();
    desc.psSize = psBlob.Size();

    // ★ V8-a 때는 여기가 두 줄이었다(`desc.layout` + `desc.layoutId`) —
    //   DX12 쪽의 `rootSignature` + `rootSignatureId` 와 짝이 같고 타입만
    //   갈리던 자리다. A-1 이 그 짝을 핸들 하나로 접었고, 그래서 두 백엔드가
    //   **같은 desc 타입에 같은 한 줄**을 쓴다.
    desc.layout = root;

    // 정점을 셰이더가 만든다 — 입력 레이아웃이 없다(그리드와 같다).
    desc.inputElements = nullptr;
    desc.inputElementCount = 0;
    desc.topologyType = RHITopologyType::Triangle;

    desc.cullMode = RHICullMode::None;
    desc.depthEnable = false;
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = m_outputFormat;
    desc.dsvFormat = RHIFormat::Unknown;

    m_pipeline = context.pipelineCache->GetOrCreate(desc, outError);
    return m_pipeline.IsValid();
}

bool VulkanTrianglePass::CreateConstantBuffer(const VulkanFrameContext& context,
    std::string& outError)
{
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = sizeof(TriangleConstants);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &m_constantBuffer);
    if (VK_SUCCESS != result)
    {
        outError = "상수 버퍼 생성 실패 — " + ResultToString(result);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, m_constantBuffer, &requirements);

    const uint32_t hostType = context.resources->FindMemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (UINT32_MAX == hostType)
    {
        outError = "호스트 가시 메모리 타입을 찾지 못했다";
        return false;
    }

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = hostType;

    result = vkAllocateMemory(m_device, &allocInfo, nullptr, &m_constantMemory);
    if (VK_SUCCESS != result)
    {
        outError = "상수 버퍼 메모리 할당 실패 — " + ResultToString(result);
        return false;
    }
    vkBindBufferMemory(m_device, m_constantBuffer, m_constantMemory, 0);

    result = vkMapMemory(m_device, m_constantMemory, 0, VK_WHOLE_SIZE, 0, &m_constantMapped);
    if (VK_SUCCESS != result || nullptr == m_constantMapped)
    {
        outError = "상수 버퍼 매핑 실패 — " + ResultToString(result);
        return false;
    }

    // ── 디스크립터 셋 ──
    //
    // ★ DX12 는 이 자리가 없다. 루트 CBV 는 GPU 주소 8바이트를 커맨드에 직접
    //   꽂는 것이라 만들 객체가 없다. Vulkan 은 풀 → 셋 → 갱신 셋이 필요하다.
    //   같은 뜻("상수 하나를 b0 에 건다")에 드는 객체가 0 대 3 이다.
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);
    if (VK_SUCCESS != result)
    {
        outError = "디스크립터 풀 생성 실패 — " + ResultToString(result);
        return false;
    }

    // 셋을 할당하려면 파이프라인 레이아웃이 아니라 **디스크립터 셋 레이아웃**이
    // 필요하다 — VulkanPipelineLayoutEntry 가 필드 셋인 이유다.
    std::string layoutError;
    const RHIPipelineLayoutParam params[] = { RHILayout::Cbv(0) };
    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    const RHIPipelineLayoutHandle rootHandle =
        context.pipelineCache->GetOrCreate(rootDesc, layoutError);
    const VulkanPipelineLayoutEntry root = context.pipelineCache->Resolve(rootHandle);
    if (!root.IsValid())
    {
        outError = layoutError;
        return false;
    }

    VkDescriptorSetAllocateInfo setInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    setInfo.descriptorPool = m_descriptorPool;
    setInfo.descriptorSetCount = 1;
    setInfo.pSetLayouts = &root.setLayout;

    result = vkAllocateDescriptorSets(m_device, &setInfo, &m_descriptorSet);
    if (VK_SUCCESS != result)
    {
        outError = "디스크립터 셋 할당 실패 — " + ResultToString(result);
        return false;
    }

    VkDescriptorBufferInfo bufferBinding{};
    bufferBinding.buffer = m_constantBuffer;
    bufferBinding.offset = 0;
    bufferBinding.range = sizeof(TriangleConstants);

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferBinding;

    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return true;
}

void VulkanTrianglePass::SetTint(float r, float g, float b, float a)
{
    m_constants.tint[0] = r;
    m_constants.tint[1] = g;
    m_constants.tint[2] = b;
    m_constants.tint[3] = a;
}

bool VulkanTrianglePass::PrepareFrame(const VulkanFrameContext& context, std::string& outError)
{
    (void)outError;

    // 프레임 밀봉. Record 가 바깥 상태를 읽지 않고 여기서 복사한 값을 쓴다 —
    // `EnhancedGridPass::PrepareFrame` 과 같은 규약이다.
    m_width = context.width;
    m_height = context.height;

    if (nullptr != m_constantMapped)
    {
        std::memcpy(m_constantMapped, &m_constants, sizeof(m_constants));
    }
    return true;
}

void VulkanTrianglePass::Record(VulkanEncoder& encoder,
    const VulkanRenderTargetBinding& targets)
{
    if (!m_pipeline.IsValid() || 0 == m_width || 0 == m_height) return;
    if (!targets.IsValid()) return;

    // ★ 아래 여섯 줄이 `EnhancedGridPass` 의 실행 람다와 같은 순서·같은
    //   이름이다. 갈린 것은 SetPipeline 의 객체 타입과 SetConstantBuffer 의
    //   셋째 인자뿐이다.
    encoder.SetViewportAndScissor(m_width, m_height);
    encoder.BindRenderTargets(targets);

    constexpr float kClear[4] = { 0.05f, 0.05f, 0.15f, 1.f };
    encoder.ClearRenderTargets(targets, kClear);

    encoder.SetPipeline(VulkanBindPoint::Graphics, m_pipeline);
    encoder.SetPrimitiveTopology(VulkanPrimitiveTopology::TriangleList);
    encoder.SetConstantBuffer(VulkanBindPoint::Graphics, 0, m_descriptorSet);

    encoder.Draw(3, 1);

    // ★ DX12 패스에는 이 줄이 없다. `vkCmdBeginRendering` 이 연 것을 닫아야
    //   하기 때문이고, 닫지 않으면 뒤따르는 복사가 검증 레이어에 잡힌다.
    //   인코더 소멸자도 닫지만, 패스가 자기가 연 것을 자기가 닫는 편이
    //   "누가 닫는가"를 계약으로 남긴다.
    encoder.EndRenderTargets();
}

void VulkanTrianglePass::Shutdown()
{
    if (VK_NULL_HANDLE != m_device)
    {
        if (VK_NULL_HANDLE != m_descriptorPool)
        {
            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        }
        if (nullptr != m_constantMapped)   vkUnmapMemory(m_device, m_constantMemory);
        if (VK_NULL_HANDLE != m_constantBuffer) vkDestroyBuffer(m_device, m_constantBuffer, nullptr);
        if (VK_NULL_HANDLE != m_constantMemory) vkFreeMemory(m_device, m_constantMemory, nullptr);
    }

    m_descriptorPool = VK_NULL_HANDLE;
    m_descriptorSet = VK_NULL_HANDLE;
    m_constantMapped = nullptr;
    m_constantBuffer = VK_NULL_HANDLE;
    m_constantMemory = VK_NULL_HANDLE;

    // ★ 파이프라인과 레이아웃은 **놓지 않는다** — 캐시가 소유한다. DX12 패스의
    //   Shutdown 이 `m_pso = nullptr;` 하나인 것과 같은 모양인데, 그쪽은
    //   참조 계수가 있어 이름을 버리는 것이 곧 소유를 던지는 것이고 이쪽은
    //   그냥 이름만 버리는 것이다. **같은 코드가 다른 뜻이다.**
    m_pipeline = {};
    m_layout = {};

    m_width = 0;
    m_height = 0;
    m_device = VK_NULL_HANDLE;
}

#endif
