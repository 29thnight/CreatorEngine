#ifndef DYNAMICCPP_EXPORTS
#include "VulkanSelfTest.h"
#include "VulkanDeviceResources.h"
#include "VulkanEncoder.h"
#include "VulkanFormat.h"
#include "VulkanPipelineCache.h"
#include "VulkanTrianglePass.h"

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

    // ★ 검사가 VkFormat 을 직접 적지 않는다(V8-a). 패스가 rtvFormats 에
    //   RHIFormat 을 넣고 캐시가 ToVulkan 으로 옮기므로, 검사도 같은 어휘로
    //   말해야 타깃과 파이프라인이 어긋났을 때 검증 레이어가 잡아 준다.
    constexpr RHIFormat kVkTestFormat = RHIFormat::RGBA8Unorm;

    // 지우는 색은 패스가 든다(VulkanTrianglePass::Record). 8비트로 대략
    // (13, 13, 38) 이고, 픽셀 검증이 이 값과 삼각형 색을 가른다.

    // 셰이더가 정점 색에 곱하는 값. 초록만 남긴다 — 상수가 셰이더에 닿지
    // 않으면 R·B 가 85 근처로 살아남으므로, 이 값이 **디스크립터 경로가
    // 실제로 돌았는지**를 픽셀로 판정하게 해 준다.
    constexpr float kVkTint[4] = { 0.f, 1.f, 0.f, 1.f };

    /// 골격이 쓰는 오프스크린 타깃과 리드백 버퍼.
    ///
    /// ★ DX12 쪽처럼 IRenderDeviceServices 를 통하지 않고 직접 만든다.
    ///   그 인터페이스는 서명이 아직 DX12 라 Vulkan 이 구현할 수 없다(§7.2.2).
    ///   V8-a 에서 파이프라인 넷은 캐시와 패스로 갔고, 여기 남은 것이
    ///   **`CreateRenderTargets`·`CreateReadback` 이 있었으면 안 남았을 것**이다.
    struct VkTestResources
    {
        VkImage        image{ VK_NULL_HANDLE };
        VkDeviceMemory imageMemory{ VK_NULL_HANDLE };
        VkImageView    imageView{ VK_NULL_HANDLE };
        VkBuffer       readback{ VK_NULL_HANDLE };
        VkDeviceMemory readbackMemory{ VK_NULL_HANDLE };

        void Destroy(VkDevice device)
        {
            if (VK_NULL_HANDLE == device) return;
            if (VK_NULL_HANDLE != readback)       vkDestroyBuffer(device, readback, nullptr);
            if (VK_NULL_HANDLE != readbackMemory) vkFreeMemory(device, readbackMemory, nullptr);
            if (VK_NULL_HANDLE != imageView)      vkDestroyImageView(device, imageView, nullptr);
            if (VK_NULL_HANDLE != image)          vkDestroyImage(device, image, nullptr);
            if (VK_NULL_HANDLE != imageMemory)    vkFreeMemory(device, imageMemory, nullptr);
            *this = VkTestResources{};
        }

        /// `RHIRenderTargetBinding` 의 자리. DX12 는 인덱스 셋이고 여기는
        /// 뷰와 크기다 — VulkanEncoder.h 의 ★ 참고.
        VulkanRenderTargetBinding MakeBinding() const
        {
            VulkanRenderTargetBinding binding{};
            binding.colorViews[0] = imageView;
            binding.colorCount = 1;
            binding.width = kVkTestWidth;
            binding.height = kVkTestHeight;
            return binding;
        }
    };

    bool VkTestCreateTarget(VulkanDeviceResources& resources, VkTestResources& out,
        std::string& outError)
    {
        VkDevice device = resources.GetDevice();

        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = ToVulkan(kVkTestFormat);
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
        viewInfo.format = ToVulkan(kVkTestFormat);
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

    // ★ 여기 있던 VkTestCreatePipeline(약 125줄)이 사라졌다.
    //
    //   셰이더 모듈 · 파이프라인 레이아웃 · 그래픽 파이프라인을 검사 파일이
    //   손으로 세우던 자리이고, V8-a 에서 그 일이 **캐시와 패스**로 갔다
    //   (VulkanPipelineCache · VulkanTrianglePass). DX12 쪽 자가 검증이
    //   psoManager·rootSignatures 를 통해 받는 것과 같은 모양이 됐다.
    //
    //   줄어든 것이 요점이 아니라, **줄어든 자리가 DX12 와 같은 자리**라는
    //   것이 요점이다.

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

    // ── 패스 경로의 도구 (V8-a) ──
    //
    // ★ DX12 자가 검증이 EnhancedFrameContext 를 채워 패스에 넘기는 것과 같은
    //   자리다. 필드가 다섯에서 둘로 줄어든 이유는 VulkanTrianglePass.h 에
    //   적었다 — 없어서 없는 것이 아니라 못 넣어서 없다.
    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(device);

    VulkanTrianglePass trianglePass;

    auto fail = [&](const std::string& line) {
        outLog += line;
        // ★ 순서가 계약이다. 패스가 먼저 놓고, 그 다음 캐시다 — 캐시가
        //   파이프라인을 소유하므로 반대로 하면 패스가 죽은 핸들을 든다.
        //   그리고 둘 다 GPU 가 놀고 있어야 하므로 대기가 앞선다.
        //   DX12 쪽에는 이 순서 제약이 없다(참조 계수가 대신 지킨다).
        resources.WaitForGpu();
        trianglePass.Shutdown();
        pipelineCache.Shutdown();
        test.Destroy(device);
        return false;
    };

    if (!VkTestCreateTarget(resources, test, error)) return fail("[1/5] " + error + "\n");

    VulkanFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.pipelineCache = &pipelineCache;
    frameContext.width = kVkTestWidth;
    frameContext.height = kVkTestHeight;

    trianglePass.SetOutputFormat(kVkTestFormat);
    trianglePass.SetTint(kVkTint[0], kVkTint[1], kVkTint[2], kVkTint[3]);

    if (!trianglePass.Initialize(frameContext, error))
    {
        return fail("[1/5] 패스 초기화 실패: " + error + "\n");
    }
    if (!trianglePass.PrepareFrame(frameContext, error))
    {
        return fail("[1/5] 패스 PrepareFrame 실패: " + error + "\n");
    }

    // ── [2/5] 프레임 경계·타임라인 세마포어 ──

    const uint64_t completedBefore = resources.GetCompletedFenceValue();

    if (!resources.BeginFrame(error)) return fail("[2/5] BeginFrame 실패: " + error + "\n");

    VkCommandBuffer commandBuffer = resources.GetCommandBuffer();

    // ── [3/5] 패스 경로로 그리는 삼각형 ──
    //
    // ★ 레이아웃 전이는 여기 남는다. DX12 에서 그것은 **그래프의 몫**이고
    //   (RHIEncoder.h: "상태 전이는 그래프의 몫이다") 이 골격에는 그래프가
    //   없다. 즉 아래 두 배리어가 있는 자리가 곧 '그래프가 없는 자리'다.

    VkTestImageBarrier(commandBuffer, test.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    {
        // 인코더 수명은 한 번의 기록이다 — 그래프가 패스마다 새로 만드는
        // 자리이고, 여기서는 검사가 그 자리를 대신한다.
        VulkanEncoder encoder(commandBuffer, &pipelineCache);
        trianglePass.Record(encoder, test.MakeBinding());
    }

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

    {
        // 캐시가 실제로 파이프라인을 구웠는지 남긴다. DX12 쪽 dx12.psocache 와
        // 같은 뜻의 값이다 — 판정 줄이 '무엇을 했는지'를 말해야 다음에 이
        // 자리가 조용히 비어도 눈에 띈다.
        const auto stats = trianglePass.GetCacheStats();
        outLog += "[3/5] 패스 경로 삼각형 기록 통과 — 레이아웃·파이프라인 캐시 경유"
            " (구움 " + std::to_string(stats.compiles)
            + " · 재사용 " + std::to_string(stats.memoryHits) + ")\n";
    }

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

        // 지운 색은 대략 (13, 13, 38).
        const bool cornerIsClear = corner[0] < 40 && corner[1] < 40
            && corner[2] > 20 && corner[2] < 70;

        // ★ 여기가 V8-a 에서 판정이 날카로워진 자리다.
        //
        //   셰이더가 정점 색에 tint(0,1,0)을 곱하므로, 상수가 닿았으면 중앙은
        //   **초록만** 남는다. 그리고 **디스크립터가 안 걸려도 삼각형은
        //   그려진다** — 그리기 자체는 상수를 필요로 하지 않기 때문이다.
        //   즉 예전 판정("중앙이 밝다")으로는 이 경로가 죽어도 통과한다.
        //
        //   두 조건이 서로 다른 실패를 잡는다. 실측(SetConstantBuffer 를 빼고
        //   돌린 값): **중앙(0,0,0)** — 안 걸린 디스크립터를 읽는 것은 미정의고
        //   이 드라이버는 0 을 주므로 tint 가 0 이 되어 삼각형이 검어진다.
        //   그것을 아래 첫 줄이 잡는다. 드라이버가 대신 쓰레기를 줬다면 색이
        //   섞여 나오고 둘째 줄이 잡는다.
        const bool centerIsTriangle = center[1] > 40;   // 그려지긴 했는가
        const bool tintReached = center[0] < 16 && center[2] < 16;   // 곱해졌는가

        if (!centerIsTriangle || !cornerIsClear || !tintReached)
        {
            vkUnmapMemory(device, test.readbackMemory);
            return fail("[4/5] 픽셀 검증 실패 — 중앙("
                + std::to_string(center[0]) + "," + std::to_string(center[1]) + ","
                + std::to_string(center[2]) + ") 구석("
                + std::to_string(corner[0]) + "," + std::to_string(corner[1]) + ","
                + std::to_string(corner[2]) + ")"
                + (tintReached ? "" : " — 상수 버퍼가 셰이더에 닿지 않았다") + "\n");
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
    //
    // ★ 놓는 순서가 fail 람다와 같아야 한다. 대기 → 패스 → 캐시 → 타깃 이고,
    //   그 순서가 계약인 이유는 VkPipeline 에 참조 계수가 없기 때문이다.

    resources.WaitForGpu();
    trianglePass.Shutdown();
    pipelineCache.Shutdown();
    test.Destroy(device);

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
