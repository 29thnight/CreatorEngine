#ifndef DYNAMICCPP_EXPORTS
#include "VulkanSelfTest.h"
#include "VulkanDeviceResources.h"
#include "Shaders/VkTriangleSpv.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <array>
#include <vector>

using namespace VulkanApi;

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr uint32_t kVkTestWidth = 256;
    constexpr uint32_t kVkTestHeight = 256;
    constexpr VkFormat kVkTestFormat = VK_FORMAT_R8G8B8A8_UNORM;

    // 지우는 색. 8비트로 대략 (13, 13, 38) 이다 — 픽셀 검증이 이 값과
    // 삼각형 색을 가른다.
    constexpr float kVkClearR = 0.05f;
    constexpr float kVkClearG = 0.05f;
    constexpr float kVkClearB = 0.15f;

    /// 골격이 쓰는 오프스크린 타깃과 리드백 버퍼.
    ///
    /// ★ DX12 쪽처럼 IRenderDeviceServices 를 통하지 않고 직접 만든다.
    ///   그 인터페이스는 서명이 아직 DX12 라 Vulkan 이 구현할 수 없다(§7.2.2).
    struct VkTestResources
    {
        VkImage        image{ VK_NULL_HANDLE };
        VkDeviceMemory imageMemory{ VK_NULL_HANDLE };
        VkImageView    imageView{ VK_NULL_HANDLE };
        VkBuffer       readback{ VK_NULL_HANDLE };
        VkDeviceMemory readbackMemory{ VK_NULL_HANDLE };

        VkShaderModule   vs{ VK_NULL_HANDLE };
        VkShaderModule   ps{ VK_NULL_HANDLE };
        VkPipelineLayout layout{ VK_NULL_HANDLE };
        VkPipeline       pipeline{ VK_NULL_HANDLE };

        void Destroy(VkDevice device)
        {
            if (VK_NULL_HANDLE == device) return;
            if (VK_NULL_HANDLE != pipeline)       vkDestroyPipeline(device, pipeline, nullptr);
            if (VK_NULL_HANDLE != layout)         vkDestroyPipelineLayout(device, layout, nullptr);
            if (VK_NULL_HANDLE != ps)             vkDestroyShaderModule(device, ps, nullptr);
            if (VK_NULL_HANDLE != vs)             vkDestroyShaderModule(device, vs, nullptr);
            if (VK_NULL_HANDLE != readback)       vkDestroyBuffer(device, readback, nullptr);
            if (VK_NULL_HANDLE != readbackMemory) vkFreeMemory(device, readbackMemory, nullptr);
            if (VK_NULL_HANDLE != imageView)      vkDestroyImageView(device, imageView, nullptr);
            if (VK_NULL_HANDLE != image)          vkDestroyImage(device, image, nullptr);
            if (VK_NULL_HANDLE != imageMemory)    vkFreeMemory(device, imageMemory, nullptr);
            *this = VkTestResources{};
        }
    };

    bool VkTestCreateTarget(VulkanDeviceResources& resources, VkTestResources& out,
        std::string& outError)
    {
        VkDevice device = resources.GetDevice();

        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = kVkTestFormat;
        imageInfo.extent = { kVkTestWidth, kVkTestHeight, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkResult result = vkCreateImage(device, &imageInfo, nullptr, &out.image);
        if (VK_SUCCESS != result)
        {
            outError = "타깃 이미지 생성 실패 — " + ResultToString(result);
            return false;
        }

        // ★ 여기가 V2 가 "핸들이어야 한다"고 적어 둔 이유의 실물이다.
        //   VkImage · VkDeviceMemory · VkImageView 셋이 한 리소스를 이룬다 —
        //   포인터 하나로 못 가리킨다. RHITextureHandle 이 이 셋을 묶는 자리다.
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device, out.image, &requirements);

        const uint32_t memoryType = resources.FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (UINT32_MAX == memoryType)
        {
            outError = "디바이스 로컬 메모리 타입을 찾지 못했다";
            return false;
        }

        VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.allocationSize = requirements.size;
        allocInfo.memoryTypeIndex = memoryType;

        result = vkAllocateMemory(device, &allocInfo, nullptr, &out.imageMemory);
        if (VK_SUCCESS != result)
        {
            outError = "타깃 메모리 할당 실패 — " + ResultToString(result);
            return false;
        }
        vkBindImageMemory(device, out.image, out.imageMemory, 0);

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = out.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = kVkTestFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        result = vkCreateImageView(device, &viewInfo, nullptr, &out.imageView);
        if (VK_SUCCESS != result)
        {
            outError = "타깃 뷰 생성 실패 — " + ResultToString(result);
            return false;
        }

        // 리드백 버퍼. 행 정렬 요구가 없다 — DX12 의
        // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT(256) 에 해당하는 제약이
        // vkCmdCopyImageToBuffer 에는 없다.
        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = static_cast<VkDeviceSize>(kVkTestWidth) * kVkTestHeight * 4;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        result = vkCreateBuffer(device, &bufferInfo, nullptr, &out.readback);
        if (VK_SUCCESS != result)
        {
            outError = "리드백 버퍼 생성 실패 — " + ResultToString(result);
            return false;
        }

        VkMemoryRequirements bufferRequirements{};
        vkGetBufferMemoryRequirements(device, out.readback, &bufferRequirements);

        const uint32_t hostType = resources.FindMemoryType(bufferRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (UINT32_MAX == hostType)
        {
            outError = "호스트 가시 메모리 타입을 찾지 못했다";
            return false;
        }

        VkMemoryAllocateInfo bufferAlloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        bufferAlloc.allocationSize = bufferRequirements.size;
        bufferAlloc.memoryTypeIndex = hostType;

        result = vkAllocateMemory(device, &bufferAlloc, nullptr, &out.readbackMemory);
        if (VK_SUCCESS != result)
        {
            outError = "리드백 메모리 할당 실패 — " + ResultToString(result);
            return false;
        }
        vkBindBufferMemory(device, out.readback, out.readbackMemory, 0);

        return true;
    }

    bool VkTestCreatePipeline(VulkanDeviceResources& resources, VkTestResources& out,
        std::string& outError)
    {
        VkDevice device = resources.GetDevice();

        VkShaderModuleCreateInfo vsInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        vsInfo.codeSize = sizeof(kVkTriangleVsSpv);
        vsInfo.pCode = kVkTriangleVsSpv;
        VkResult result = vkCreateShaderModule(device, &vsInfo, nullptr, &out.vs);
        if (VK_SUCCESS != result)
        {
            outError = "정점 셰이더 모듈 생성 실패 — " + ResultToString(result);
            return false;
        }

        VkShaderModuleCreateInfo psInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        psInfo.codeSize = sizeof(kVkTrianglePsSpv);
        psInfo.pCode = kVkTrianglePsSpv;
        result = vkCreateShaderModule(device, &psInfo, nullptr, &out.ps);
        if (VK_SUCCESS != result)
        {
            outError = "픽셀 셰이더 모듈 생성 실패 — " + ResultToString(result);
            return false;
        }

        // 빈 레이아웃. V4 가 만든 RHIPipelineLayoutDesc 를 여기서 쓰지 않는다 —
        // IRenderRootSignatureCache::GetOrCreate 가 DX12RootSignatureEntry 를
        // 돌려주기 때문이다(§7.2.2). **중립 설명이 있어도 반환형이 DX12 면
        // 두 번째 백엔드는 그 캐시를 쓸 수 없다.**
        VkPipelineLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        result = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &out.layout);
        if (VK_SUCCESS != result)
        {
            outError = "파이프라인 레이아웃 생성 실패 — " + ResultToString(result);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = out.vs;
        stages[0].pName = "VSMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = out.ps;
        stages[1].pName = "PSMain";

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.f;

        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        const VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        // ★ 동적 렌더링. VkRenderPass 를 만들지 않는다 — DX12 에 대응물이
        //   없는 객체를 계약에 들이지 않기 위해서다. 파이프라인은 대신
        //   '어떤 포맷의 타깃에 그리는가'만 안다. 그것은 DX12 의
        //   rtvFormats 와 같은 정보다.
        VkPipelineRenderingCreateInfo renderingInfo{
            VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &kVkTestFormat;

        VkGraphicsPipelineCreateInfo pipelineInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipelineInfo.pNext = &renderingInfo;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisample;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &blend;
        pipelineInfo.pDynamicState = &dynamic;
        pipelineInfo.layout = out.layout;

        result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
            nullptr, &out.pipeline);
        if (VK_SUCCESS != result)
        {
            outError = "그래픽 파이프라인 생성 실패 — " + ResultToString(result);
            return false;
        }

        return true;
    }

    void VkTestImageBarrier(VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
        VkPipelineStageFlags2 destinationStage, VkAccessFlags2 destinationAccess)
    {
        // ★ 이 함수의 인자 수가 V3 의 판단이 옳았다는 증거다. D3D12_RESOURCE_STATES
        //   하나가 여기서 **레이아웃 + 스테이지 + 접근 마스크** 셋으로 갈린다.
        //   RHIResourceState 가 '무엇에 쓰는가'만 말하기 때문에 백엔드가 이 셋을
        //   각자 유도할 수 있다 — 원시 D3D12 상수를 상위가 적었다면 되돌릴 수 없다.
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = sourceStage;
        barrier.srcAccessMask = sourceAccess;
        barrier.dstStageMask = destinationStage;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(commandBuffer, &dependency);
    }

    /// 스왑체인 검사용 숨김 창.
    ///
    /// ★ 왜 창을 따로 만드는가: 엔진의 창은 DX12 스왑체인이 쥐고 있다(D2).
    ///   한 창에 두 백엔드의 스왑체인을 함께 둘 수 없다 — DXGI 제약이자
    ///   Vulkan 쪽에서도 서피스가 이미 쓰이는 창이면
    ///   VK_ERROR_NATIVE_WINDOW_IN_USE_KHR 가 난다.
    struct VkTestWindow
    {
        HWND handle{ nullptr };
        ATOM atom{ 0 };

        bool Create()
        {
            WNDCLASSEXW cls{};
            cls.cbSize = sizeof(cls);
            cls.lpfnWndProc = ::DefWindowProcW;
            cls.hInstance = ::GetModuleHandleW(nullptr);
            cls.lpszClassName = L"CreatorEngineVulkanSelfTest";
            atom = ::RegisterClassExW(&cls);

            handle = ::CreateWindowExW(0, L"CreatorEngineVulkanSelfTest", L"vk.selftest",
                WS_OVERLAPPEDWINDOW, 0, 0, static_cast<int>(kVkTestWidth),
                static_cast<int>(kVkTestHeight), nullptr, nullptr,
                ::GetModuleHandleW(nullptr), nullptr);
            return nullptr != handle;
        }

        void Destroy()
        {
            if (nullptr != handle) { ::DestroyWindow(handle); handle = nullptr; }
            if (0 != atom)
            {
                ::UnregisterClassW(L"CreatorEngineVulkanSelfTest", ::GetModuleHandleW(nullptr));
                atom = 0;
            }
        }
    };
}

bool RunVulkanSelfTest(const std::string& outputPngPath, std::string& outLog)
{
    std::string error;

    // ── [1/5] 로더·인스턴스·디바이스 ──

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[1/5] " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    if (!resources.Initialize(kVkTestWidth, kVkTestHeight, true, error))
    {
        outLog += "[1/5] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    {
        const uint32_t api = resources.GetApiVersion();
        outLog += "[1/5] 디바이스 생성 통과 — " + resources.GetAdapterName()
            + " (Vulkan " + std::to_string(VK_API_VERSION_MAJOR(api)) + "."
            + std::to_string(VK_API_VERSION_MINOR(api)) + "."
            + std::to_string(VK_API_VERSION_PATCH(api)) + ")"
            + " · 검증 레이어 " + (resources.IsValidationEnabled() ? "켬" : "끔") + "\n";
    }

    if (!resources.IsValidationEnabled())
    {
        outLog += "[1/5] 검증 레이어가 꺼져 있다 — 통과로 세지 않는다\n";
        return false;
    }

    VkTestResources test{};
    VkDevice device = resources.GetDevice();

    auto fail = [&](const std::string& line) {
        outLog += line;
        test.Destroy(device);
        resources.WaitForGpu();
        return false;
    };

    if (!VkTestCreateTarget(resources, test, error))   return fail("[1/5] " + error + "\n");
    if (!VkTestCreatePipeline(resources, test, error)) return fail("[1/5] " + error + "\n");

    // ── [2/5] 프레임 경계·타임라인 세마포어 ──

    const uint64_t completedBefore = resources.GetCompletedFenceValue();

    if (!resources.BeginFrame(error)) return fail("[2/5] BeginFrame 실패: " + error + "\n");

    VkCommandBuffer commandBuffer = resources.GetCommandBuffer();

    // ── [3/5] 오프스크린 삼각형 ──

    VkTestImageBarrier(commandBuffer, test.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = test.imageView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = { { kVkClearR, kVkClearG, kVkClearB, 1.f } };

    VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    rendering.renderArea = { { 0, 0 }, { kVkTestWidth, kVkTestHeight } };
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &rendering);

    // ★ 높이를 음수로 준다. Vulkan 의 클립 공간 Y 는 아래로 향하고 D3D 는
    //   위로 향한다 — 셰이더를 고치지 않고 백엔드가 좌표계를 맞춘다.
    //   어느 층이 이것을 맞추는지가 계약의 문제라서, 셰이더 바이너리 안에
    //   숨기지 않는다(생성 스크립트의 -fvk-invert-y 주석 참조).
    VkViewport viewport{};
    viewport.x = 0.f;
    viewport.y = static_cast<float>(kVkTestHeight);
    viewport.width = static_cast<float>(kVkTestWidth);
    viewport.height = -static_cast<float>(kVkTestHeight);
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{ { 0, 0 }, { kVkTestWidth, kVkTestHeight } };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, test.pipeline);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRendering(commandBuffer);

    VkTestImageBarrier(commandBuffer, test.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { kVkTestWidth, kVkTestHeight, 1 };
    vkCmdCopyImageToBuffer(commandBuffer, test.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, test.readback, 1, &copy);

    if (!resources.EndFrame(error)) return fail("[3/5] EndFrame 실패: " + error + "\n");

    resources.WaitForGpu();

    {
        const uint64_t completedAfter = resources.GetCompletedFenceValue();
        const uint64_t signaled = resources.GetLastSignaledFenceValue();
        const bool advanced = completedAfter > completedBefore && completedAfter >= signaled;
        outLog += "[2/5] 프레임 경계·타임라인 세마포어 "
            + std::string(advanced ? "통과" : "실패")
            + " (완료값 " + std::to_string(completedBefore) + " → "
            + std::to_string(completedAfter) + " · 서명 "
            + std::to_string(signaled) + ")\n";
        if (!advanced) return fail("");
    }

    outLog += "[3/5] 동적 렌더링 삼각형 기록 통과 (렌더 패스 객체 없이)\n";

    // ── [4/5] 리드백·픽셀 검증·PNG ──

    {
        void* mapped = nullptr;
        const VkResult mappedResult = vkMapMemory(device, test.readbackMemory, 0,
            VK_WHOLE_SIZE, 0, &mapped);
        if (VK_SUCCESS != mappedResult || nullptr == mapped)
        {
            return fail("[4/5] 리드백 매핑 실패 — " + ResultToString(mappedResult) + "\n");
        }

        const auto* pixels = static_cast<const uint8_t*>(mapped);
        const size_t rowPitch = static_cast<size_t>(kVkTestWidth) * 4;

        auto sample = [&](uint32_t x, uint32_t y) {
            const uint8_t* p = pixels + static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * 4;
            return std::array<uint8_t, 4>{ p[0], p[1], p[2], p[3] };
        };

        const auto center = sample(kVkTestWidth / 2, kVkTestHeight / 2);
        const auto corner = sample(2, 2);

        // 지운 색은 대략 (13, 13, 38). 삼각형은 정점 색 보간이라 어느 채널이든
        // 훨씬 밝다.
        const int centerSum = center[0] + center[1] + center[2];
        const bool centerIsTriangle = centerSum > 100;
        const bool cornerIsClear = corner[0] < 40 && corner[1] < 40
            && corner[2] > 20 && corner[2] < 70;

        if (!centerIsTriangle || !cornerIsClear)
        {
            vkUnmapMemory(device, test.readbackMemory);
            return fail("[4/5] 픽셀 검증 실패 — 중앙("
                + std::to_string(center[0]) + "," + std::to_string(center[1]) + ","
                + std::to_string(center[2]) + ") 구석("
                + std::to_string(corner[0]) + "," + std::to_string(corner[1]) + ","
                + std::to_string(corner[2]) + ")\n");
        }

        DirectX::Image image{};
        image.width = kVkTestWidth;
        image.height = kVkTestHeight;
        image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        image.rowPitch = rowPitch;
        image.slicePitch = rowPitch * kVkTestHeight;
        image.pixels = const_cast<uint8_t*>(pixels);

        const std::wstring widePath(outputPngPath.begin(), outputPngPath.end());
        const HRESULT saved = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), widePath.c_str());

        vkUnmapMemory(device, test.readbackMemory);

        if (FAILED(saved)) return fail("[4/5] PNG 저장 실패\n");

        outLog += "[4/5] 픽셀 검증·PNG 저장 통과 — 중앙("
            + std::to_string(center[0]) + "," + std::to_string(center[1]) + ","
            + std::to_string(center[2]) + ") 구석("
            + std::to_string(corner[0]) + "," + std::to_string(corner[1]) + ","
            + std::to_string(corner[2]) + ")\n";
    }

    // ── [5/5] 스왑체인 (숨김 창) ──

    {
        VkTestWindow window;
        if (!window.Create()) return fail("[5/5] 검사용 창 생성 실패\n");

        bool swapChainOk = resources.AttachSwapChain(window.handle,
            kVkTestWidth, kVkTestHeight, error);
        if (!swapChainOk)
        {
            outLog += "[5/5] AttachSwapChain 실패: " + error + "\n";
            window.Destroy();
            return fail("");
        }

        // 두 프레임을 돌린다. 백버퍼를 표시 가능 레이아웃으로만 옮기고 표시한다 —
        // 골격의 목적은 그림이 아니라 프레임 경계·획득·표시가 계약대로
        // 도는지다.
        for (uint32_t frame = 0; frame < 2 && swapChainOk; ++frame)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[5/5] BeginFrame 실패: " + error + "\n";
                swapChainOk = false;
                break;
            }

            VkTestImageBarrier(resources.GetCommandBuffer(),
                resources.GetBackBuffer(resources.GetBackBufferIndex()),
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);

            if (!resources.EndFrame(error) || !resources.Present(error))
            {
                outLog += "[5/5] 제출·표시 실패: " + error + "\n";
                swapChainOk = false;
                break;
            }
        }

        if (swapChainOk && !resources.ResizeSwapChain(320, 200, error))
        {
            outLog += "[5/5] ResizeSwapChain 실패: " + error + "\n";
            swapChainOk = false;
        }

        resources.WaitForGpu();
        window.Destroy();

        if (!swapChainOk) return fail("");

        outLog += "[5/5] 스왑체인 통과 — 붙임·획득·표시 2프레임·크기 변경\n";
    }

    // ── 검증 레이어가 조용해야 진짜 통과다 ──

    test.Destroy(device);
    resources.WaitForGpu();

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        outLog += "검증 레이어 경고 " + std::to_string(problems) + "건\n" + messages;
        return false;
    }

    outLog += "Vulkan 골격 검증 통과 — 검증 레이어 클린\n";
    return true;
}

#endif
