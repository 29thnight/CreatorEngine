#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "Render/Passes/Lighting/EnhancedSkyBoxPass.h"
#include "Render/Passes/Editor/EnhancedGizmoIconPass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "FrameCameraSnapshot.h"
#include "Texture.h"
#include "PathFinder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// EnhancedSkyBoxPass — Vulkan. 두 백엔드가 공유하는 첫 텍스처 소비 패스다.
//
// dx12.skybox와 같은 4x4 합성 큐브맵, 같은 세 카메라, 같은 중심 픽셀과
// 전면 커버 판정을 쓴다. 상수만 도달하고 t0가 빠지면 검정, 큐브 뷰나 면
// 순서가 틀리면 다른 원색이므로 동적 텍스처 바인딩 전체를 픽셀로 잰다.
namespace
{
    constexpr uint32_t kVkSkyWidth = 256;
    constexpr uint32_t kVkSkyHeight = 256;
    constexpr uint32_t kVkCubeFaceSize = 4;
    constexpr uint32_t kVkCubeFaceBytes = kVkCubeFaceSize * kVkCubeFaceSize * 8;

    constexpr uint16_t kVkHalfOne = 0x3C00;
    constexpr uint16_t kVkHalfZero = 0x0000;
    constexpr uint16_t kVkFaceColors[6][4] = {
        { kVkHalfOne,  kVkHalfZero, kVkHalfZero, kVkHalfOne },  // +X 빨강
        { kVkHalfZero, kVkHalfOne,  kVkHalfZero, kVkHalfOne },  // -X 초록
        { kVkHalfZero, kVkHalfZero, kVkHalfOne,  kVkHalfOne },  // +Y 파랑
        { kVkHalfOne,  kVkHalfOne,  kVkHalfZero, kVkHalfOne },  // -Y 노랑
        { kVkHalfOne,  kVkHalfZero, kVkHalfOne,  kVkHalfOne },  // +Z 자홍
        { kVkHalfZero, kVkHalfOne,  kVkHalfOne,  kVkHalfOne },  // -Z 청록
    };

    struct VkSkyCapture
    {
        RHIReadbackImage image;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            return image.At(x, y, channel);
        }

        uint32_t CountCovered() const
        {
            uint32_t covered = 0;
            for (uint32_t y = 0; y < kVkSkyHeight; ++y)
                for (uint32_t x = 0; x < kVkSkyWidth; ++x)
                    if (At(x, y, 0) + At(x, y, 1) + At(x, y, 2) > 0.5f) ++covered;
            return covered;
        }
    };

    FrameCameraSnapshot VkSkyCamera(float atX, float atY, float atZ)
    {
        FrameCameraSnapshot snapshot{};
        snapshot.view = math::look_at_lh(
            math::vector3{0.f, 0.f, 0.f},
            math::vector3{atX, atY, atZ},
            math::vector3{0.f, 1.f, 0.f});
        snapshot.projection = math::perspective_fov_lh(
            math::half_pi * 0.5f, 1.f, 0.1f, 100.f);
        snapshot.eyePosition = math::vector3{0.f, 0.f, 0.f};
        return snapshot;
    }

    struct VkSkySpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };
}

bool RunVulkanSkyBoxTest(std::string& outLog)
{
    outLog += "── 스카이박스 패스 — Vulkan (첫 텍스처 소비 공용 패스) ──\n";
    std::string error;

    VulkanDeviceResources resources;
    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[1/4] Vulkan 로더 없음 — 건너뜀: " + error + "\n";
        return false;
    }
    if (!resources.Initialize(kVkSkyWidth, kVkSkyHeight, true, error))
    {
        outLog += "[1/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }

    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelineCache);

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &pipelineCache;
    frameContext.rootSignatures = &pipelineCache;
    frameContext.width = kVkSkyWidth;
    frameContext.height = kVkSkyHeight;

    EnhancedSkyBoxPass sky;
    RHITextureHandle cubeMap;
    RHIReadback readback{};

    const auto fail = [&](const std::string& line) {
        outLog += line;
        std::string layerMessages;
        if (0 != resources.DrainDebugMessages(layerMessages)) outLog += layerMessages;
        resources.WaitForGpu();
        sky.Shutdown();
        resources.ReleaseReadback(readback);
        resources.ReleaseTexture(cubeMap);
        pipelineCache.Shutdown();
        resources.Shutdown();
        outLog += "스카이박스 패스(Vulkan) 검증 실패\n";
        return false;
    };

    {
        VkSkySpirvScope spirvScope;
        if (!sky.Initialize(frameContext, error))
            return fail("[1/4] 스카이박스 초기화 실패: " + error + "\n");
    }
    outLog += "[1/4] 셰이더 SPIR-V 컴파일·파이프라인 생성 통과 — 패스 코드 무변경\n";

    // 합성 큐브맵을 RHI 리소스로 만들고, 검사용 원시 복사 한 번으로 채운다.
    RHITextureDesc cubeDesc{};
    cubeDesc.width = kVkCubeFaceSize;
    cubeDesc.height = kVkCubeFaceSize;
    cubeDesc.depthOrArraySize = 6;
    cubeDesc.mipLevels = 1;
    cubeDesc.format = RHIFormat::RGBA16Float;
    cubeDesc.debugName = L"VkSkyBox.Cube";
    if (!resources.CreateTexture(cubeDesc, cubeMap, error))
        return fail("[2/4] 큐브맵 생성 실패: " + error + "\n");

    if (!resources.BeginFrame(error))
        return fail("[2/4] 업로드 BeginFrame 실패: " + error + "\n");

    const RHIBufferSlice upload = resources.AllocateUpload(
        RHIUploadRequest{ kVkCubeFaceBytes * 6, RHIUploadUsage::TextureCopy, 16 });
    if (!upload.IsValid()) return fail("[2/4] 업로드 링 할당 실패\n");

    uint16_t* pixels = static_cast<uint16_t*>(upload.cpuAddress);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t pixel = 0; pixel < kVkCubeFaceSize * kVkCubeFaceSize; ++pixel)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
                *pixels++ = kVkFaceColors[face][channel];
        }
    }

    const RHITransition toCopy{
        cubeMap, RHIResourceState::Common, RHIResourceState::CopyDest };
    resources.TransitionResources({ &toCopy, 1 });

    const VulkanBufferEntry uploadEntry = resources.GetResourceTable().Resolve(upload.buffer);
    const VulkanImageEntry cubeEntry = resources.GetResourceTable().Resolve(cubeMap);
    if (!uploadEntry.IsValid() || !cubeEntry.IsValid())
        return fail("[2/4] 업로드 리소스 표 해석 실패\n");

    VkBufferImageCopy copies[6]{};
    for (uint32_t face = 0; face < 6; ++face)
    {
        VkBufferImageCopy& copy = copies[face];
        copy.bufferOffset = upload.offset + static_cast<VkDeviceSize>(face) * kVkCubeFaceBytes;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = face;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = { kVkCubeFaceSize, kVkCubeFaceSize, 1 };
    }
    VulkanApi::vkCmdCopyBufferToImage(resources.GetCommandBuffer(), uploadEntry.buffer,
        cubeEntry.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, copies);

    const RHITransition toShader{
        cubeMap, RHIResourceState::CopyDest, RHIResourceState::PixelShaderResource };
    resources.TransitionResources({ &toShader, 1 });
    if (!resources.EndFrame(error))
        return fail("[2/4] 업로드 EndFrame 실패: " + error + "\n");
    resources.WaitForGpu();
    outLog += "[2/4] 합성 큐브맵 업로드(6면 원색) 완료\n";

    sky.SetCubeMap(cubeMap, RHIFormat::RGBA16Float, 1);
    sky.SetScale(10.f);
    if (!resources.CreateReadback(kVkSkyWidth, kVkSkyHeight,
        EnhancedSkyBoxPass::kOutputFormat, 1, readback, error))
    {
        return fail("[2/4] 리드백 생성 실패: " + error + "\n");
    }

    EnhancedRenderGraph::Stats lastStats{};
    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, VkSkyCapture& capture) {
        frameContext.camera = &snapshot;
        if (!resources.BeginFrame(error)) { outLog += "BeginFrame 실패: " + error + "\n"; return false; }
        if (!sky.PrepareFrame(frameContext, error)) { outLog += "PrepareFrame 실패: " + error + "\n"; return false; }

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        sky.Declare(graph, frameContext);
        const RGHandle output = sky.GetOutput();
        if (!output.IsValid()) { outLog += "스카이박스 출력이 선언되지 않았다\n"; return false; }

        graph.AddPass("SkyBox.Readback", { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext) {
                executeContext.encoder->CopyToReadback(readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(error)) { outLog += "Compile 실패: " + error + "\n"; return false; }
        if (!graph.Execute(error)) { outLog += "Execute 실패: " + error + "\n"; return false; }
        lastStats = graph.GetStats();
        if (!resources.EndFrame(error)) { outLog += "EndFrame 실패: " + error + "\n"; return false; }
        resources.WaitForGpu();
        if (!resources.MapReadback(readback, capture.image, error))
        { outLog += error + "\n"; return false; }
        return true;
    };

    struct Expect { const char* name; float x, y, z, r, g, b; };
    const Expect expects[] = {
        { "+X(빨강)", 1.f, 0.f, 0.f,  1.f, 0.f, 0.f },
        { "-Z(청록)", 0.f, 0.f, -1.f, 0.f, 1.f, 1.f },
        { "+Z(자홍)", 0.f, 0.f, 1.f,  1.f, 0.f, 1.f },
    };

    for (const Expect& expect : expects)
    {
        VkSkyCapture capture{};
        if (!renderOnce(VkSkyCamera(expect.x, expect.y, expect.z), capture)) return fail("");

        const float r = capture.At(kVkSkyWidth / 2, kVkSkyHeight / 2, 0);
        const float g = capture.At(kVkSkyWidth / 2, kVkSkyHeight / 2, 1);
        const float b = capture.At(kVkSkyWidth / 2, kVkSkyHeight / 2, 2);
        const uint32_t covered = capture.CountCovered();
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[3/4] %s — 중심 RGB(%.2f %.2f %.2f) · 커버 %u/%u\n",
            expect.name, r, g, b, covered, kVkSkyWidth * kVkSkyHeight);
        outLog += line;

        if (std::fabs(r - expect.r) > 0.1f || std::fabs(g - expect.g) > 0.1f ||
            std::fabs(b - expect.b) > 0.1f)
            return fail("면 색이 DX12 기준과 다르다 — t0·큐브 뷰·면 순서를 확인하라\n");
        if (covered != kVkSkyWidth * kVkSkyHeight)
            return fail("화면에 빈 곳이 있다 — SkyBox 래스터/깊이 결과가 DX12와 다르다\n");
    }

    char graphLine[160]{};
    std::snprintf(graphLine, sizeof(graphLine),
        "[4/4] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u\n",
        lastStats.passesDeclared, lastStats.passesExecuted,
        lastStats.passesCulled, lastStats.transientCreated);
    outLog += graphLine;
    if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled ||
        2 != lastStats.transientCreated)
        return fail("그래프 통계가 다르다 — 패스 2·컬링 0·transient 2여야 한다\n");

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    if (0 != stubs)
    {
        const char* last = resources.GetEncoderLastUnimplemented();
        if (nullptr == last) last = resources.GetLastUnimplemented();
        return fail("미구현 호출 " + std::to_string(stubs) + "건(" +
            (nullptr != last ? last : "-") + ")\n");
    }

    resources.WaitForGpu();
    sky.Shutdown();
    resources.ReleaseReadback(readback);
    resources.ReleaseTexture(cubeMap);
    pipelineCache.Shutdown();

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
        resources.Shutdown();
        outLog += "스카이박스 패스(Vulkan) 검증 실패\n";
        return false;
    }

    resources.Shutdown();
    outLog += "스카이박스 패스(Vulkan) 검증 통과 — 텍스처 소비 공용 패스 2/17\n";
    return true;
}

namespace
{
    constexpr uint32_t kVkIconWidth = 256;
    constexpr uint32_t kVkIconHeight = 256;

    struct VkIconCapture
    {
        RHIReadbackImage image;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            return image.At(x, y, channel);
        }

        float MaxInWindow(uint32_t centerX, uint32_t centerY, uint32_t radius,
            uint32_t channel) const
        {
            float best = 0.f;
            const uint32_t x0 = centerX > radius ? centerX - radius : 0;
            const uint32_t y0 = centerY > radius ? centerY - radius : 0;
            const uint32_t x1 = (std::min)(centerX + radius, kVkIconWidth - 1);
            const uint32_t y1 = (std::min)(centerY + radius, kVkIconHeight - 1);
            for (uint32_t y = y0; y <= y1; ++y)
                for (uint32_t x = x0; x <= x1; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }

        uint32_t CountLit(float threshold) const
        {
            uint32_t lit = 0;
            for (uint32_t y = 0; y < kVkIconHeight; ++y)
                for (uint32_t x = 0; x < kVkIconWidth; ++x)
                    if (At(x, y, 0) > threshold) ++lit;
            return lit;
        }
    };

    FrameCameraSnapshot VkIconCamera()
    {
        FrameCameraSnapshot snapshot{};
        snapshot.view = math::look_at_lh(
            math::vector3{0.f, 1.f, -10.f},
            math::vector3{0.f, 1.f, 0.f},
            math::vector3{0.f, 1.f, 0.f});
        snapshot.projection = math::perspective_fov_lh(
            math::quarter_pi, 1.f, 0.1f, 100.f);
        snapshot.eyePosition = math::vector3{0.f, 1.f, -10.f};
        return snapshot;
    }

    bool VkIconProjectToPixel(const FrameCameraSnapshot& camera,
        float worldX, float worldY, float worldZ, uint32_t& outX, uint32_t& outY)
    {
        const math::vector4 clip =
            math::vector4{worldX, worldY, worldZ, 1.f} *
            (camera.view * camera.projection);
        const float w = clip.w;
        if (w <= 1e-6f) return false;

        const float ndcX = clip.x / w;
        const float ndcY = clip.y / w;
        if (ndcX < -1.f || ndcX > 1.f || ndcY < -1.f || ndcY > 1.f) return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * kVkIconWidth);
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * kVkIconHeight);
        outX = (std::min)(outX, kVkIconWidth - 1);
        outY = (std::min)(outY, kVkIconHeight - 1);
        return true;
    }
}

bool RunVulkanGizmoIconTest(std::string& outLog)
{
    outLog += "── 기즈모 아이콘 패스 — Vulkan (실제 PNG·root buffer) ──\n";
    std::string error;

    const file::path cameraIconPath = PathFinder::IconPath() / L"CameraGizmo.png";
    std::unique_ptr<Texture> cameraIcon(Texture::LoadFormPath(cameraIconPath));
    if (!cameraIcon || nullptr == cameraIcon->GetCpuPixels())
    {
        outLog += "[1/4] CameraGizmo.png 로드 실패: " + cameraIconPath.string() + "\n";
        return false;
    }
    const DirectX::TexMetadata& metadata = cameraIcon->GetCpuPixels()->GetMetadata();
    if (128 != metadata.width || 128 != metadata.height ||
        cameraIcon->GetCpuPixels()->IsAlphaAllOpaque())
    {
        outLog += "[1/4] CameraGizmo.png가 기대한 128x128 RGBA 자산이 아니다\n";
        return false;
    }

    VulkanDeviceResources resources;
    if (!VulkanApi::LoadLoader(error) ||
        !resources.Initialize(kVkIconWidth, kVkIconHeight, true, error))
    {
        outLog += "[1/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }

    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelineCache);

    VulkanTextureCache textureCache;
    if (!textureCache.Initialize(&resources, error))
    {
        outLog += "[1/4] Vulkan 텍스처 캐시 초기화 실패: " + error + "\n";
        pipelineCache.Shutdown();
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &pipelineCache;
    frameContext.rootSignatures = &pipelineCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = kVkIconWidth;
    frameContext.height = kVkIconHeight;

    const FrameCameraSnapshot camera = VkIconCamera();
    frameContext.camera = &camera;

    EnhancedGizmoIconPass gizmo;
    RHIReadback readback{};
    const auto cleanup = [&]() {
        resources.WaitForGpu();
        gizmo.Shutdown();
        resources.ReleaseReadback(readback);
        textureCache.Shutdown();
        pipelineCache.Shutdown();
        resources.Shutdown();
    };
    const auto fail = [&](const std::string& message) {
        outLog += message;
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        if (0 != problems)
            outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
        cleanup();
        outLog += "기즈모 아이콘 패스(Vulkan) 검증 실패\n";
        return false;
    };

    {
        VkSkySpirvScope spirvScope;
        if (!gizmo.Initialize(frameContext, error))
            return fail("[1/4] 셰이더/파이프라인 초기화 실패: " + error + "\n");
    }
    outLog += "[1/4] 실제 CameraGizmo.png 로드 · SPIR-V·파이프라인 생성 통과\n";

    if (!resources.CreateReadback(kVkIconWidth, kVkIconHeight,
        EnhancedGizmoIconPass::kOutputFormat, 1, readback, error))
        return fail("[2/4] 리드백 생성 실패: " + error + "\n");

    std::vector<EnhancedGizmoIconPass::Icon> icons(1);
    icons[0].position = { 0.f, 0.f, 0.f };
    icons[0].size = 2.f;
    icons[0].texture = cameraIcon.get();
    gizmo.SetIcons(&icons);

    if (!resources.BeginFrame(error))
        return fail("[2/4] BeginFrame 실패: " + error + "\n");
    if (!gizmo.PrepareFrame(frameContext, error))
        return fail("[2/4] PrepareFrame/PNG 업로드 실패: " + error + "\n");

    EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
    gizmo.Declare(graph, frameContext);
    const RGHandle output = gizmo.GetOutput();
    if (!output.IsValid()) return fail("[2/4] 아이콘 출력이 선언되지 않았다\n");

    graph.AddPass("GizmoIcon.Readback", { { output, RHIResourceState::CopySource } },
        [&](const EnhancedRenderGraph::ExecuteContext& executeContext) {
            executeContext.encoder->CopyToReadback(readback,
                executeContext.ResolveHandle(output));
        }, true);
    if (!graph.Compile(error) || !graph.Execute(error))
        return fail("[2/4] 그래프 실행 실패: " + error + "\n");
    if (!resources.EndFrame(error))
        return fail("[2/4] EndFrame 실패: " + error + "\n");
    resources.WaitForGpu();

    VkIconCapture capture{};
    if (!resources.MapReadback(readback, capture.image, error))
        return fail("[2/4] 리드백 매핑 실패: " + error + "\n");

    const EnhancedRenderGraph::Stats graphStats = graph.GetStats();
    const VulkanTextureCache::Stats textureStats = textureCache.GetStats();
    char graphLine[384]{};
    std::snprintf(graphLine, sizeof(graphLine),
        "[2/4] 그래프 — 실행 %u · 컬링 %u · transient %u · 아이콘 %u · 배치 %u"
        " · PNG 업로드 %u/실패 %u · texture pool segment/pooled/dedicated %u/%u/%u"
        " · %.1f/%.1f MiB\n",
        graphStats.passesExecuted, graphStats.passesCulled, graphStats.transientCreated,
        gizmo.GetLastIconCount(), gizmo.GetLastBatchCount(),
        textureStats.fromCpuPixels, textureStats.failures,
        textureStats.persistentHeap.activeSegments,
        textureStats.persistentHeap.livePooledAllocations,
        textureStats.persistentHeap.liveDedicatedAllocations,
        static_cast<double>(textureStats.persistentHeap.allocatedBytes) /
            (1024.0 * 1024.0),
        static_cast<double>(textureStats.persistentHeap.budgetBytes) /
            (1024.0 * 1024.0));
    outLog += graphLine;
    if (2 != graphStats.passesExecuted || 0 != graphStats.passesCulled ||
        1 != graphStats.transientCreated || 1 != gizmo.GetLastIconCount() ||
        1 != gizmo.GetLastBatchCount() || 1 != textureStats.fromCpuPixels ||
        0 != textureStats.failures ||
        0 == textureStats.persistentHeap.activeSegments ||
        0 == textureStats.persistentHeap.livePooledAllocations ||
        0 != textureStats.persistentHeap.liveDedicatedAllocations ||
        0 == textureStats.persistentHeap.allocatedBytes ||
        0 == textureStats.persistentHeap.budgetBytes)
        return fail("그래프·배치·실 PNG 업로드 또는 Vulkan budget 기반 texture compatibility pool이 기대와 다르다\n");

    uint32_t centerX = 0, centerY = 0;
    uint32_t transparentX = 0, transparentY = 0;
    uint32_t outsideX = 0, outsideY = 0;
    if (!VkIconProjectToPixel(camera, 0.f, 1.f, 0.f, centerX, centerY) ||
        !VkIconProjectToPixel(camera, 0.f, 0.2f, 0.f, transparentX, transparentY) ||
        !VkIconProjectToPixel(camera, 3.f, 1.f, 0.f, outsideX, outsideY))
        return fail("[3/4] 표본 투영 실패\n");

    const float centerR = capture.MaxInWindow(centerX, centerY, 2, 0);
    const float transparentR = capture.At(transparentX, transparentY, 0);
    const float outsideR = capture.At(outsideX, outsideY, 0);
    const uint32_t lit = capture.CountLit(0.1f);
    char pixelLine[256]{};
    std::snprintf(pixelLine, sizeof(pixelLine),
        "[3/4] DX12 기준 대조 — 중심 R %.3f(px %u,%u) · 쿼드 투명 R %.3f(px %u,%u) · 밖 R %.3f · 점등 %u\n",
        centerR, centerX, centerY, transparentR, transparentX, transparentY,
        outsideR, lit);
    outLog += pixelLine;
    if (centerR < 0.35f || centerR > 0.65f || transparentR > 0.05f ||
        outsideR > 0.05f || 0 == lit)
        return fail("실제 PNG 픽셀이 DX12 기준과 다르다 — t0 root buffer/t1 2D SRV를 확인하라\n");

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    if (0 != stubs)
    {
        const char* last = resources.GetEncoderLastUnimplemented();
        if (nullptr == last) last = resources.GetLastUnimplemented();
        return fail("[4/4] 미구현 호출 " + std::to_string(stubs) + "건(" +
            (nullptr != last ? last : "-") + ")\n");
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
        return fail("[4/4] 검증 레이어 문제 " + std::to_string(problems) +
            "건\n" + validation);
    outLog += "[4/4] descriptor kind·범위·per-frame 수명 통과 · Vulkan validation 0건\n";

    cleanup();
    outLog += "기즈모 아이콘 패스(Vulkan) 검증 통과 — 텍스처 소비 공용 패스 3/17\n";
    return true;
}
