#ifndef DYNAMICCPP_EXPORTS
#include "VulkanSelfTest.h"
#include "VulkanDeviceResources.h"
#include "VulkanEncoder.h"
#include "VulkanPipelineCache.h"
#include "Shaders/VkTriangleSpv.h"
#include "../RHIShaderBlob.h"
#include "../IRenderDeviceServices.h"

#include <Windows.h>
#include <DirectXTex.h>

#include <array>
#include <string>

using namespace VulkanApi;

// Vulkan 골격 자가 검증.
//
// ── 5 마무리에서 통째로 다시 썼다 ──
//
// 예전 판은 6단계였고 [3~4/6]이 `VulkanTrianglePass` — 골격 전용 패스,
// 백엔드 전용 인코더 오버로드, 원시 타깃·리드백 — 로 그렸다. 그 검사들이
// 재던 것(파이프라인 캐시 경유 · 상수 도달 · 픽셀)을 지금은 **중립 계약**
// (`IRenderDeviceServices` + `RHIEncoder`)이 전부 잴 수 있고, 실제 패스의
// 대조는 `vk.grid` 가 픽셀 편차 0 으로 하고 있다. 골격 전용 비계는 그래서
// 죽었다 — 비계가 재던 것을 본채가 재게 된 것이 5 의 완료 형태다.
//
// ★ 원시 Vulkan 이 남은 곳은 [4/4] 스왑체인 하나다. 백버퍼는 남(스왑체인)이
//   만든 이미지라 핸들 표에 없고, 그래서 전이를 계약으로 못 건다 — R6 가
//   적어 둔 그 잔여이고, 이 배리어 헬퍼가 남아 있는 이유다.
//
// 판정 줄은 dx12.* 검사와 같은 모양([n/m] 접두)으로 낸다. 반환값이 false 라도
// outLog 는 채워진다 — 어디까지 갔는지가 산출물이다.
namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.

    /// 스왑체인 검사용 창 크기.
    constexpr uint32_t kVkTestWidth = 256;
    constexpr uint32_t kVkTestHeight = 256;

    /// 중립 경로가 그리는 크기.
    constexpr uint32_t kVkDrawSize = 64;

    /// 스왑체인 백버퍼의 표시 전이 (위 ★ — 핸들이 없어 계약으로 못 건다).
    void VkTestImageBarrier(VkCommandBuffer commandBuffer, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
        VkPipelineStageFlags2 destinationStage, VkAccessFlags2 destinationAccess)
    {
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

    // ── [1/4] 로더·인스턴스·디바이스 ──

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[1/4] " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    if (!resources.Initialize(kVkTestWidth, kVkTestHeight, true, error))
    {
        outLog += "[1/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    {
        const uint32_t api = resources.GetApiVersion();
        outLog += "[1/4] 디바이스 생성 통과 — " + resources.GetAdapterName()
            + " (Vulkan " + std::to_string(VK_API_VERSION_MAJOR(api)) + "."
            + std::to_string(VK_API_VERSION_MINOR(api)) + "."
            + std::to_string(VK_API_VERSION_PATCH(api)) + ")"
            + " · 검증 레이어 " + (resources.IsValidationEnabled() ? "켬" : "끔") + "\n";
    }

    if (!resources.IsValidationEnabled())
    {
        outLog += "[1/4] 검증 레이어가 꺼져 있다 — 통과로 세지 않는다\n";
        return false;
    }

    VkDevice device = resources.GetDevice();

    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(device);
    resources.SetPipelineCache(&pipelineCache);

    RHIReadback readback{};

    auto fail = [&](const std::string& line) {
        outLog += line;

        // ★ 실패할 때 검증 레이어가 한 말을 함께 낸다 (5c-4d). 없으면
        //   `VkResult -13` 같은 답만 남는다 — 레이어는 이미 정확히 알고 있다.
        std::string layerMessages;
        if (0 != resources.DrainDebugMessages(layerMessages) || !layerMessages.empty())
        {
            outLog += layerMessages;
        }

        resources.WaitForGpu();
        resources.ReleaseReadback(readback);
        pipelineCache.Shutdown();
        return false;
    };

    // ── [2/4]·[3/4] 중립 경로 한 프레임 — 프레임 경계와 그리기를 함께 잰다 ──
    //
    // ★ 전부 **계약**(`IRenderDeviceServices` + `RHIEncoder`)으로 부른다.
    //   구체 타입이 남은 곳은 프레임 여닫이(`IRHIDeviceResources` 의 몫)와
    //   `EndRenderTargets`(DX12 에 대응이 없어 계약에 안 넣은 것) 둘이고,
    //   끝에 **미구현 계수 0** 을 판정에 넣는다 — 스텁이 조용히 지나가면
    //   "통과했는데 아무것도 안 그렸다"가 된다.

    const uint64_t completedBefore = resources.GetCompletedFenceValue();

    {
        IRenderDeviceServices& services = resources;

        if (!resources.BeginFrame(error)) return fail("[2/4] BeginFrame 실패: " + error + "\n");

        // 기본 세그먼트보다 큰 첫 요청이 즉시 성공하고, 중간 제출 셋이
        // CPU wait 없이 서로 다른 command pool로 이어지는지 검증한다.
        constexpr uint64_t kLargeVertexBytes = 20ull * 1024 * 1024;
        constexpr uint64_t kLargeIndexBytes = 1ull * 1024 * 1024;
        const std::array<RHIUploadRequest, 2> meshRequests = {{
            { kLargeVertexBytes, RHIUploadUsage::VertexData, 4 },
            { kLargeIndexBytes, RHIUploadUsage::IndexData, 4 }
        }};
        std::array<RHIBufferSlice, 2> meshSlices{};
        if (!services.ReserveUploadBatch(meshRequests, meshSlices, error) ||
            !meshSlices[0].IsWritable() || !meshSlices[1].IsWritable() ||
            meshSlices[0].buffer.id != meshSlices[1].buffer.id ||
            meshSlices[1].offset < meshSlices[0].offset + meshSlices[0].size)
        {
            return fail("[2/4] 20MiB 정점/인덱스 첫 배치가 all-or-none으로 성공하지 않았다: "
                + error + "\n");
        }
        for (uint32_t flush = 0; flush < 3; ++flush)
        {
            if (!resources.FlushCommandList(error))
                return fail("[2/4] 비동기 중간 제출 실패: " + error + "\n");
        }
        const RHIUploadStats uploadStats = resources.GetUploadStats();
        if (0 == uploadStats.largeSegments || 0 != uploadStats.oomFailures)
            return fail("[2/4] Vulkan large segment 분류가 관측되지 않았다\n");

        RHITextureDesc colorDesc{};
        colorDesc.width = kVkDrawSize;
        colorDesc.height = kVkDrawSize;
        colorDesc.format = RHIFormat::RGBA8Unorm;
        colorDesc.allowRenderTarget = true;
        colorDesc.initialState = RHIResourceState::RenderTarget;
        colorDesc.debugName = L"vk.selftest.color";

        RHITextureHandle color;
        if (!services.CreateTexture(colorDesc, color, error))
            return fail("[3/4] CreateTexture(색) 실패: " + error + "\n");

        RHITextureDesc depthDesc{};
        depthDesc.width = kVkDrawSize;
        depthDesc.height = kVkDrawSize;
        depthDesc.format = RHIFormat::D32Float;
        depthDesc.allowDepthStencil = true;
        depthDesc.initialState = RHIResourceState::DepthWrite;
        depthDesc.debugName = L"vk.selftest.depth";

        RHITextureHandle depthTexture;
        if (!services.CreateTexture(depthDesc, depthTexture, error))
            return fail("[3/4] CreateTexture(깊이) 실패: " + error + "\n");

        // 되묻기 — 만든 값이 그대로 돌아와야 한다(5c-1 이 계약에 둔 이유).
        const RHITextureInfo info = services.DescribeTexture(color);
        if (!info.IsValid() || kVkDrawSize != info.width || kVkDrawSize != info.height
            || RHIFormat::RGBA8Unorm != info.format)
        {
            return fail("[3/4] DescribeTexture 가 만든 값과 다르다\n");
        }

        // 버퍼 칸의 생산자 경로 (5c-4c).
        RHIBufferDesc bufferDesc{};
        bufferDesc.bytes = 4096;
        bufferDesc.debugName = L"vk.selftest.buffer";

        RHIBufferHandle buffer;
        if (!services.CreateBuffer(bufferDesc, buffer, error))
            return fail("[3/4] CreateBuffer 실패: " + error + "\n");

        const RHIDepthTargetDesc depthTarget =
            RHIDepthTargetDesc::Depth(depthTexture, RHIFormat::D32Float);
        const RHITextureHandle colorList[1] = { color };

        const RHIRenderTargetBinding targets = services.CreateRenderTargets(colorList, &depthTarget);
        if (!targets.IsValid() || 1 != targets.colorCount || !targets.HasDepth())
            return fail("[3/4] CreateRenderTargets 가 무효를 줬다\n");

        // ── 파이프라인 — 레이아웃이 그리드의 `RHILayout::Cbv(0)` 와 같다 ──

        const RHIPipelineLayoutParam layoutParams[] = { RHILayout::Cbv(0) };
        RHIPipelineLayoutDesc layoutDesc{};
        layoutDesc.params = layoutParams;

        const RHIPipelineLayoutHandle layout = pipelineCache.GetOrCreate(layoutDesc, error);
        if (!layout.IsValid())
            return fail("[3/4] 레이아웃 생성 실패: " + error + "\n");

        RHIShaderBlob vs;
        RHIShaderBlob ps;
        vs.Assign(kVkTriangleVsSpv, sizeof(kVkTriangleVsSpv));
        ps.Assign(kVkTrianglePsSpv, sizeof(kVkTrianglePsSpv));

        RHIGraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.vsBytecode = vs.Data();
        pipelineDesc.vsSize = vs.Size();
        pipelineDesc.psBytecode = ps.Data();
        pipelineDesc.psSize = ps.Size();
        pipelineDesc.layout = layout;
        pipelineDesc.topologyType = RHITopologyType::Triangle;
        pipelineDesc.cullMode = RHICullMode::None;
        pipelineDesc.depthEnable = false;
        pipelineDesc.numRenderTargets = 1;
        pipelineDesc.rtvFormats[0] = RHIFormat::RGBA8Unorm;

        // ★ 깊이를 안 쓰는데도 포맷을 적어야 한다 — 검증 레이어가 반증했다
        //   (5c-4d). 렌더링에 깊이 타깃을 걸었으면 파이프라인이 같은 포맷을
        //   선언해야 한다. "포맷은 묶음이 아니라 파이프라인이 든다"의 실물.
        pipelineDesc.dsvFormat = RHIFormat::D32Float;

        const RHIPipelineHandle pipeline = pipelineCache.GetOrCreate(pipelineDesc, error);
        if (!pipeline.IsValid())
            return fail("[3/4] 파이프라인 생성 실패: " + error + "\n");

        // ── 링에서 자른다 — 두 번 잘라 겹침·정렬을 함께 잰다 (5c-4d) ──

        const float tint[4] = { 0.f, 1.f, 0.f, 1.f };   // 초록만
        const RHIBufferSlice firstSlice = services.UploadConstants(tint, sizeof(tint));
        const RHIBufferSlice cb = services.UploadConstants(tint, sizeof(tint));

        if (!firstSlice.IsWritable() || !cb.IsWritable())
            return fail("[3/4] UploadConstants 가 쓸 수 없는 조각을 줬다\n");
        if (cb.offset < firstSlice.offset + firstSlice.size)
            return fail("[3/4] 링에서 자른 두 조각이 겹친다\n");
        const uint64_t uniformAlignment =
            resources.GetRequiredUploadAlignmentForTesting(RHIUploadUsage::ConstantBuffer);
        if (0 != (cb.offset % uniformAlignment))
            return fail("[3/4] 상수 정렬이 장치 하한과 다르다 — "
                + std::to_string(cb.offset) + "/" + std::to_string(uniformAlignment) + "\n");

        // ── 리드백도 계약이다 (5d 가 열었다) ──

        if (!services.CreateReadback(kVkDrawSize, kVkDrawSize, RHIFormat::RGBA8Unorm,
            1, readback, error))
        {
            return fail("[3/4] CreateReadback 실패: " + error + "\n");
        }

        VulkanEncoder& backendEncoder = static_cast<VulkanEncoder&>(services.GetImmediateEncoder());
        RHIEncoder& encoder = backendEncoder;

        // 지운 색은 (0, 0, 0.5) — 삼각형이 안 그려지면 파랑만 남는다.
        const float clearColor[4] = { 0.f, 0.f, 0.5f, 1.f };
        encoder.BindRenderTargets(targets);
        encoder.ClearRenderTargets(targets, clearColor);
        encoder.ClearDepthTarget(targets, 1.f);
        encoder.SetViewportAndScissor(kVkDrawSize, kVkDrawSize);
        encoder.SetPipeline(RHIBindPoint::Graphics, pipeline);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, cb);
        encoder.Draw(3, 1);
        backendEncoder.EndRenderTargets();

        // 그래프 밖 전이도 계약으로 건다(V3 가 이 자리를 위해 만든 어휘다).
        const RHITransition transition{
            color, RHIResourceState::RenderTarget, RHIResourceState::CopySource };
        services.TransitionResources({ &transition, 1 });

        encoder.CopyToReadback(readback, color);

        if (!resources.EndFrame(error)) return fail("[3/4] EndFrame 실패: " + error + "\n");
        resources.WaitForGpu();

        // ── [2/4] 프레임 경계·타임라인 세마포어 ──

        {
            const uint64_t completedAfter = resources.GetCompletedFenceValue();
            const uint64_t signaled = resources.GetLastSignaledFenceValue();
            const bool advanced = completedAfter > completedBefore && completedAfter >= signaled;
            outLog += "[2/4] 프레임 경계·타임라인 세마포어 "
                + std::string(advanced ? "통과" : "실패")
                + " (완료값 " + std::to_string(completedBefore) + " → "
                + std::to_string(completedAfter) + " · 서명 "
                + std::to_string(signaled)
                + " · 첫 대형 메시 21MiB 배치 · 비동기 flush 3회)\n";
            if (!advanced) return fail("");
        }

        // ── 미구현 계수 — 통과 판정의 일부다 ──

        const uint32_t deviceStubs = resources.GetUnimplementedCount();
        const uint32_t encoderStubs = backendEncoder.GetUnimplementedCount();
        if (0 != deviceStubs || 0 != encoderStubs)
        {
            return fail("[3/4] 미구현 호출이 있었다 — 서비스 "
                + std::to_string(deviceStubs) + "건("
                + (nullptr != resources.GetLastUnimplemented()
                    ? resources.GetLastUnimplemented() : "-")
                + ") · 인코더 " + std::to_string(encoderStubs) + "건("
                + (nullptr != backendEncoder.GetLastUnimplemented()
                    ? backendEncoder.GetLastUnimplemented() : "-") + ")\n");
        }

        // ── 픽셀 검증·PNG ──

        RHIReadbackImage image;
        if (!services.MapReadback(readback, image, error))
            return fail("[3/4] MapReadback 실패: " + error + "\n");

        const auto sample = [&](uint32_t x, uint32_t y) {
            return std::array<float, 3>{
                image.At(x, y, 0), image.At(x, y, 1), image.At(x, y, 2) };
        };

        // 삼각형 안(중앙 아래쪽)과 밖(구석).
        const auto inside = sample(kVkDrawSize / 2, kVkDrawSize * 2 / 3);
        const auto outside = sample(1, 1);

        // ★ 판정 둘이 서로 다른 실패를 잡는다. 디스크립터가 안 걸려도
        //   삼각형은 그려지므로(그리기는 상수를 안 쓴다), 안 걸리면 검은
        //   삼각형이 된다 — "그려졌는가"와 "상수가 닿았는가"를 가른다.
        const bool drewTriangle = inside[2] < 0.16f;                       // 지운 파랑이 아니다
        const bool constantsReached = inside[1] > 0.78f && inside[0] < 0.08f;   // 초록만
        const bool clearedOutside = outside[2] > 0.39f;

        if (!drewTriangle || !constantsReached || !clearedOutside)
        {
            char detail[160]{};
            std::snprintf(detail, sizeof(detail),
                "[3/4] 픽셀 검증 실패 — 안(%.2f,%.2f,%.2f) 밖(%.2f,%.2f,%.2f)%s\n",
                inside[0], inside[1], inside[2], outside[0], outside[1], outside[2],
                constantsReached ? "" : " — 상수 버퍼가 셰이더에 닿지 않았다");
            return fail(detail);
        }

        {
            DirectX::Image png{};
            png.width = kVkDrawSize;
            png.height = kVkDrawSize;
            png.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            png.rowPitch = image.rowPitch;
            png.slicePitch = image.data.size();
            png.pixels = image.data.data();

            const std::wstring widePath(outputPngPath.begin(), outputPngPath.end());
            const HRESULT saved = DirectX::SaveToWICFile(png, DirectX::WIC_FLAGS_NONE,
                DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), widePath.c_str());
            if (FAILED(saved)) return fail("[3/4] PNG 저장 실패\n");
        }

        services.ReleaseReadback(readback);
        services.ReleaseTexture(color);
        services.ReleaseTexture(depthTexture);
        resources.GetResourceTable().Release(device, buffer);

        const auto stats = pipelineCache.GetStats();
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 중립 경로 통과 — 상수가 셰이더에 닿았다(안 G %.2f · 밖 B %.2f)"
            " · 캐시 구움 %u · 링 사용 %uB · 표 잔량 %u장 · 미구현 0\n",
            inside[1], outside[2], stats.compiles,
            static_cast<uint32_t>(resources.GetUploadUsedBytes()),
            static_cast<uint32_t>(resources.GetResourceTable().LiveImageCount()));
        outLog += line;
    }

    // ── [4/4] 스왑체인 (숨김 창) ──

    {
        VkTestWindow window;
        if (!window.Create()) return fail("[4/4] 검사용 창 생성 실패\n");

        bool swapChainOk = resources.AttachSwapChain(window.handle,
            kVkTestWidth, kVkTestHeight, error);
        if (!swapChainOk)
        {
            outLog += "[4/4] AttachSwapChain 실패: " + error + "\n";
            window.Destroy();
            return fail("");
        }

        // 두 프레임을 돌린다. 백버퍼를 표시 가능 레이아웃으로만 옮기고 표시한다 —
        // 골격의 목적은 그림이 아니라 프레임 경계·획득·표시가 계약대로 도는지다.
        for (uint32_t frame = 0; frame < 2 && swapChainOk; ++frame)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[4/4] BeginFrame 실패: " + error + "\n";
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
                outLog += "[4/4] 제출·표시 실패: " + error + "\n";
                swapChainOk = false;
                break;
            }
        }

        // 획득 뒤 Abort해도 acquire semaphore/이미지가 남지 않아 다음
        // BeginFrame이 정상 진행되는지 확인한다.
        if (swapChainOk)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[4/4] Abort 준비 BeginFrame 실패: " + error + "\n";
                swapChainOk = false;
            }
            else
            {
                const RHIBufferSlice abortedUpload = resources.AllocateUpload(
                    RHIUploadRequest{ 1024, RHIUploadUsage::BufferCopy, 4 });
                if (!abortedUpload.IsWritable()) swapChainOk = false;
                resources.AbortFrame();
            }
        }
        if (swapChainOk)
        {
            if (!resources.BeginFrame(error))
            {
                outLog += "[4/4] Abort 뒤 BeginFrame 실패: " + error + "\n";
                swapChainOk = false;
            }
            else
            {
                VkTestImageBarrier(resources.GetCommandBuffer(),
                    resources.GetBackBuffer(resources.GetBackBufferIndex()),
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0);
                if (!resources.EndFrame(error) || !resources.Present(error))
                {
                    outLog += "[4/4] Abort 뒤 제출·표시 실패: " + error + "\n";
                    swapChainOk = false;
                }
            }
        }

        if (swapChainOk && !resources.ResizeSwapChain(320, 200, error))
        {
            outLog += "[4/4] ResizeSwapChain 실패: " + error + "\n";
            swapChainOk = false;
        }

        resources.WaitForGpu();
        window.Destroy();

        if (!swapChainOk) return fail("");

        outLog += "[4/4] 스왑체인 통과 — 붙임·획득·표시 2프레임·Abort 복구·크기 변경\n";
    }

    // ── 검증 레이어가 조용해야 진짜 통과다 ──

    resources.WaitForGpu();
    pipelineCache.Shutdown();

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
