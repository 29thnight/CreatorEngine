#ifndef DYNAMICCPP_EXPORTS
#include "VulkanTrianglePass.h"
#include "VulkanDeviceResources.h"
#include "VulkanBindingModel.h"
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
    if (!CreateTexture(context, outError)) return false;
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
    // ★ V8-b 에서 두 줄이 늘었다 — 테이블(t0) 과 정적 샘플러(s0). 늘어난
    //   모양도 그리드 패스와 같다: `RHILayout::Table(...)` 과
    //   `RHIStaticSamplerDesc{ RHISampler::Point(), 0 }`.
    //
    //   여기서 세 자리가 처음 소비자를 얻는다 — `RHIDescriptorRange`(V4) ·
    //   `RHIStaticSamplerDesc`(V4) · `RHISampler::Point()`(V4). 그때 만들어
    //   두고 **Vulkan 이 쓸 수 있는지 아무도 몰랐던** 것들이다.
    const RHIPipelineLayoutParam params[] = {
        RHILayout::Cbv(0),
        RHILayout::Table(RHIDescriptorType::ShaderResource, 1, 0),
    };

    // ★ 포인트 샘플러다. 체커보드를 선형으로 읽으면 칸 경계가 섞여 표본 두
    //   점의 차이가 흐려진다 — 판정이 드라이버의 필터링 구현에 의존하게 된다.
    //   §8.6 이 "밉 필터링은 정당하게 다를 수 있다"고 적어 둔 자리를 피한다.
    const RHIStaticSamplerDesc samplers[] = {
        RHIStaticSamplerDesc{ RHISampler::Point(RHIAddressMode::Clamp), 0,
            RHIShaderVisibility::All },
    };

    RHIPipelineLayoutDesc rootDesc{};
    rootDesc.params = params;
    rootDesc.staticSamplers = samplers;

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

bool VulkanTrianglePass::CreateTexture(const VulkanFrameContext& context, std::string& outError)
{
    VulkanDeviceResources& resources = *context.resources;

    // ── 픽셀 ──
    //
    // 흑백 체커보드. 밝은 칸 255 · 어두운 칸 32 로 둔다 — 0 을 쓰면 "텍스처가
    // 안 걸려 0 이 읽힌 것"과 "어두운 칸을 읽은 것"이 구분되지 않는다.
    uint8_t pixels[kTextureSize * kTextureSize * 4]{};
    for (uint32_t y = 0; y < kTextureSize; ++y)
    {
        for (uint32_t x = 0; x < kTextureSize; ++x)
        {
            const bool bright = ((x / kCheckerCell) + (y / kCheckerCell)) % 2 == 0;
            const uint8_t value = bright ? 255u : 32u;
            uint8_t* texel = pixels + (static_cast<size_t>(y) * kTextureSize + x) * 4;
            texel[0] = value; texel[1] = value; texel[2] = value; texel[3] = 255u;
        }
    }

    // ── 이미지 ──

    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = { kTextureSize, kTextureSize, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult result = vkCreateImage(m_device, &imageInfo, nullptr, &m_texture);
    if (VK_SUCCESS != result)
    {
        outError = "텍스처 이미지 생성 실패 — " + ResultToString(result);
        return false;
    }

    VkMemoryRequirements imageReq{};
    vkGetImageMemoryRequirements(m_device, m_texture, &imageReq);

    const uint32_t deviceType = resources.FindMemoryType(imageReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (UINT32_MAX == deviceType)
    {
        outError = "디바이스 로컬 메모리 타입을 찾지 못했다";
        return false;
    }

    VkMemoryAllocateInfo imageAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    imageAlloc.allocationSize = imageReq.size;
    imageAlloc.memoryTypeIndex = deviceType;

    result = vkAllocateMemory(m_device, &imageAlloc, nullptr, &m_textureMemory);
    if (VK_SUCCESS != result)
    {
        outError = "텍스처 메모리 할당 실패 — " + ResultToString(result);
        return false;
    }
    vkBindImageMemory(m_device, m_texture, m_textureMemory, 0);

    // ── 스테이징 버퍼 ──
    //
    // ★ DX12 는 이 자리에 업로드 링이 있다(`GetUploadRing().Allocate`). 링은
    //   프레임 단위로 도는 것이라 초기화 시점의 일회성 업로드에는 안 맞고,
    //   그래서 DX12 텍스처 캐시도 자기 스테이징을 따로 만든다. 즉 이 코드가
    //   대응하는 것은 링이 아니라 **텍스처 캐시**다 — A-4 의 몫.

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    const auto releaseStaging = [&] {
        if (VK_NULL_HANDLE != staging)       vkDestroyBuffer(m_device, staging, nullptr);
        if (VK_NULL_HANDLE != stagingMemory) vkFreeMemory(m_device, stagingMemory, nullptr);
    };

    VkBufferCreateInfo stagingInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    stagingInfo.size = sizeof(pixels);
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    result = vkCreateBuffer(m_device, &stagingInfo, nullptr, &staging);
    if (VK_SUCCESS != result)
    {
        outError = "스테이징 버퍼 생성 실패 — " + ResultToString(result);
        return false;
    }

    VkMemoryRequirements stagingReq{};
    vkGetBufferMemoryRequirements(m_device, staging, &stagingReq);

    const uint32_t hostType = resources.FindMemoryType(stagingReq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (UINT32_MAX == hostType)
    {
        releaseStaging();
        outError = "호스트 가시 메모리 타입을 찾지 못했다";
        return false;
    }

    VkMemoryAllocateInfo stagingAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    stagingAlloc.allocationSize = stagingReq.size;
    stagingAlloc.memoryTypeIndex = hostType;

    result = vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory);
    if (VK_SUCCESS != result)
    {
        releaseStaging();
        outError = "스테이징 메모리 할당 실패 — " + ResultToString(result);
        return false;
    }
    vkBindBufferMemory(m_device, staging, stagingMemory, 0);

    void* mapped = nullptr;
    result = vkMapMemory(m_device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (VK_SUCCESS != result || nullptr == mapped)
    {
        releaseStaging();
        outError = "스테이징 매핑 실패 — " + ResultToString(result);
        return false;
    }
    std::memcpy(mapped, pixels, sizeof(pixels));
    vkUnmapMemory(m_device, stagingMemory);

    // ── 일회성 커맨드: 전이 → 복사 → 전이 ──
    //
    // ★ 프레임 커맨드 버퍼를 쓸 수 없다. Initialize 는 BeginFrame 밖에서
    //   불리기 때문이고, 그 순서를 바꾸면 "패스 초기화가 프레임 안에서만
    //   된다"는 계약이 생긴다 — DX12 쪽에 없는 제약이다.
    //
    //   DX12 는 이 자리에 배리어가 없다. 업로드 힙에서 복사하고
    //   COPY_DEST → SHADER_RESOURCE 전이 하나면 되는데, Vulkan 은
    //   UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY 로 **두 번** 바꾼다.
    //   레이아웃이 상태보다 잘게 갈리는 것이 A-5 뒤에 올 배리어 중립화의
    //   숙제다(§8.5 의 6번).

    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = resources.GetQueueFamily();

    VkCommandPool uploadPool = VK_NULL_HANDLE;
    result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &uploadPool);
    if (VK_SUCCESS != result)
    {
        releaseStaging();
        outError = "업로드 커맨드 풀 생성 실패 — " + ResultToString(result);
        return false;
    }

    VkCommandBufferAllocateInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdInfo.commandPool = uploadPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    result = vkAllocateCommandBuffers(m_device, &cmdInfo, &cmd);
    if (VK_SUCCESS != result)
    {
        vkDestroyCommandPool(m_device, uploadPool, nullptr);
        releaseStaging();
        outError = "업로드 커맨드 버퍼 할당 실패 — " + ResultToString(result);
        return false;
    }

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // 동기화 2 를 쓴다 — 골격이 이미 그것으로 서 있다(VulkanDeviceResources).
    // 한 파일만 구 API 를 쓰면 스테이지·접근 마스크의 어휘가 두 벌이 된다.
    VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_texture;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = VK_ACCESS_2_NONE;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

    VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(cmd, &dependency);

    VkBufferImageCopy copy{};
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent = { kTextureSize, kTextureSize, 1 };

    vkCmdCopyBufferToImage(cmd, staging, m_texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

    vkCmdPipelineBarrier2(cmd, &dependency);

    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    commandInfo.commandBuffer = cmd;

    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;

    result = vkQueueSubmit2(resources.GetQueue(), 1, &submit, VK_NULL_HANDLE);
    if (VK_SUCCESS == result)
    {
        // 업로드가 끝나야 스테이징과 풀을 놓을 수 있다. 초기화 경로라
        // 큐를 통째로 기다려도 된다 — 프레임 안이면 못 하는 일이다.
        vkQueueWaitIdle(resources.GetQueue());
    }

    vkDestroyCommandPool(m_device, uploadPool, nullptr);
    releaseStaging();

    if (VK_SUCCESS != result)
    {
        outError = "텍스처 업로드 제출 실패 — " + ResultToString(result);
        return false;
    }

    // ── 뷰 ──

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = m_texture;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    result = vkCreateImageView(m_device, &viewInfo, nullptr, &m_textureView);
    if (VK_SUCCESS != result)
    {
        outError = "텍스처 뷰 생성 실패 — " + ResultToString(result);
        return false;
    }
    return true;
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
    // ★ V8-b 에서 종류가 둘이 됐다. 풀은 **종류마다 개수를 미리 말해야** 한다 —
    //   DX12 의 디스크립터 힙은 종류가 하나(CBV/SRV/UAV 한 힙)라 이 구분이
    //   없다. A-5 가 `RHIBindingTable` 을 중립화할 때 이 비대칭이 인터페이스에
    //   드러날 자리다: "몇 개를 어느 종류로 쓸 것인가"를 Vulkan 은 풀을 만들
    //   때, DX12 는 자를 때 안다.
    //
    // ★ **샘플러도 풀에 있어야 한다 — 검증 레이어가 반증했다.** 처음에는
    //   "정적 샘플러라 셋 레이아웃에 구워져 있으니(pImmutableSamplers) 풀에서
    //   잘라 오지 않는다"고 적고 빼 뒀는데, 그 자리에서 이 경고가 났다:
    //
    //     binding 2 was created with VK_DESCRIPTOR_TYPE_SAMPLER but
    //     VkDescriptorPool was not created with any VkDescriptorPoolSize::type
    //     with VK_DESCRIPTOR_TYPE_SAMPLER
    //
    //   즉 불변 샘플러여도 셋 안의 **자리**는 차지한다. DX12 는 정적 샘플러가
    //   디스크립터 힙을 아예 안 쓰므로 이 비대칭에 대응이 없다 —
    //   §7.2.8 의 예상 3(수명 비대칭)에 이어 **예산 비대칭**이 하나 더다.
    //
    //   ★ 이것이 §7.2.2 가 "검증 레이어 없이 골격을 세우면 안 된다"고 적은
    //     이유의 실례다. 이 드라이버는 할당을 성공시켰고, 픽셀도 맞게 나왔다.
    const VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  1 },
        { VK_DESCRIPTOR_TYPE_SAMPLER,        1 },
    };

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;

    result = vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);
    if (VK_SUCCESS != result)
    {
        outError = "디스크립터 풀 생성 실패 — " + ResultToString(result);
        return false;
    }

    // 셋을 할당하려면 파이프라인 레이아웃이 아니라 **디스크립터 셋 레이아웃**이
    // 필요하다 — VulkanPipelineLayoutEntry 가 필드 셋인 이유다.
    // ★ CreatePipelines 가 이미 만든 레이아웃을 **핸들로** 되찾는다. 예전에는
    //   같은 desc 를 여기서 다시 조립해 캐시를 두 번 불렀는데, V8-b 에서 desc 가
    //   길어지면서 두 벌이 어긋날 자리가 됐다 — 한쪽만 고치면 셋 레이아웃이
    //   달라져 디스크립터가 엉뚱한 자리에 걸린다.
    const VulkanPipelineLayoutEntry root = context.pipelineCache->Resolve(m_layout);
    if (!root.IsValid())
    {
        outError = "레이아웃 핸들을 풀지 못했다";
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

    VkDescriptorImageInfo imageBinding{};
    imageBinding.imageView = m_textureView;
    imageBinding.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2]{};
    uint32_t writeCount = 0;

    writes[writeCount] = VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[writeCount].dstSet = m_descriptorSet;
    // ★ binding 번호가 규약을 지난다. `b0` 이 0, `t0` 이 100 이다 —
    //   `VulkanBindingModel.h` 와 셰이더를 구운 dxc 시프트가 같은 값이어야
    //   여기가 맞는다. 손으로 0 과 1 을 적으면 지금은 우연히 맞을 수도 있지만
    //   셰이더를 다시 구우면 어긋난다.
    writes[writeCount].dstBinding = VulkanBindingModel::kConstantBufferShift + 0;
    writes[writeCount].descriptorCount = 1;
    writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[writeCount].pBufferInfo = &bufferBinding;
    ++writeCount;

    // ★ 음성 대조가 이 분기를 끈다. 안 걸어도 삼각형은 그려지므로, 끄고 재
    //   보지 않으면 텍스처 경로가 죽어도 판정이 통과한다.
    if (m_bindTexture)
    {
        writes[writeCount] = VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        writes[writeCount].dstSet = m_descriptorSet;
        writes[writeCount].dstBinding = VulkanBindingModel::kShaderResourceShift + 0;
        writes[writeCount].descriptorCount = 1;
        writes[writeCount].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[writeCount].pImageInfo = &imageBinding;
        ++writeCount;
    }

    vkUpdateDescriptorSets(m_device, writeCount, writes, 0, nullptr);
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

        // 뷰 → 이미지 → 메모리 순이다. 뷰가 이미지를 참조하므로 거꾸로 놓으면
        // 검증 레이어가 잡는다 — DX12 는 참조 계수가 이 순서를 대신 지킨다.
        if (VK_NULL_HANDLE != m_textureView)   vkDestroyImageView(m_device, m_textureView, nullptr);
        if (VK_NULL_HANDLE != m_texture)       vkDestroyImage(m_device, m_texture, nullptr);
        if (VK_NULL_HANDLE != m_textureMemory) vkFreeMemory(m_device, m_textureMemory, nullptr);
    }

    m_textureView = VK_NULL_HANDLE;
    m_texture = VK_NULL_HANDLE;
    m_textureMemory = VK_NULL_HANDLE;

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
