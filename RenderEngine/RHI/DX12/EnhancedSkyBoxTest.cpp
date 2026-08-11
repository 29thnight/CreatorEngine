#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSkyBoxPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 스카이박스 패스 자가 검증 (PHASE 3-6).
//
// ── 셋을 따로 단정한다 ──
//
//   ① 방향   — 보는 방향의 큐브 면 색이 나오는가(면마다 다른 색을 깐다)
//   ② 전면   — 화면 전체가 하늘로 덮이는가. z = w x 0.99999 quirk가
//      깨지면(z = w) 깊이 LESS를 통과하지 못해 화면이 통째로 빈다 —
//      이 단정이 그 실패를 잡는다
//   ③ 카메라 — 방향을 바꾸면 다른 면이 나오는가
//
// 면마다 원색을 깔아 두면 방향 계산의 축 뒤집힘(±X 혼동, Y/Z 스왑)이
// '비슷한 하늘색'이 아니라 전혀 다른 원색으로 드러난다.
namespace
{
    constexpr uint32_t kSkyWidth = 256;
    constexpr uint32_t kSkyHeight = 256;
    constexpr uint32_t kCubeFaceSize = 4;

    struct SkyCapture
    {
        RHIReadbackImage image;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            return image.At(x, y, channel);
        }

        // 화면 전체가 덮였는가 — RGB 합이 0.5를 넘는 픽셀 수.
        uint32_t CountCovered() const
        {
            uint32_t covered = 0;
            for (uint32_t y = 0; y < kSkyHeight; ++y)
                for (uint32_t x = 0; x < kSkyWidth; ++x)
                    if (At(x, y, 0) + At(x, y, 1) + At(x, y, 2) > 0.5f) ++covered;
            return covered;
        }
    };

    FrameCameraSnapshot SkyCamera(float atX, float atY, float atZ)
    {
        FrameCameraSnapshot snapshot{};
        snapshot.view = XMMatrixLookAtLH(
            XMVectorSet(0.f, 0.f, 0.f, 1.f),
            XMVectorSet(atX, atY, atZ, 1.f),
            XMVectorSet(0.f, 1.f, 0.f, 0.f));
        snapshot.projection = XMMatrixPerspectiveFovLH(
            DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 100.f);
        snapshot.eyePosition = XMVectorSet(0.f, 0.f, 0.f, 1.f);
        return snapshot;
    }

    constexpr uint16_t kHalfOne = 0x3C00;
    constexpr uint16_t kHalfZero = 0x0000;
}

bool EnhancedSceneRenderer::RunSkyBoxTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 스카이박스 패스 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kSkyWidth, kSkyHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_skybox.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kSkyWidth;
    frameContext.height = kSkyHeight;

    EnhancedSkyBoxPass sky;
    if (!sky.Initialize(frameContext, error))
    {
        outLog += "[1/4] 스카이박스 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과\n";

    // ── [2/4] 합성 큐브맵 — 면마다 원색 ──
    //
    // D3D 면 순서: +X -X +Y -Y +Z -Z.
    // 빨강 초록 파랑 노랑 자홍 청록 — 어느 축이 뒤집혀도 원색이 갈린다.
    ComPtr<ID3D12Resource> cubeMap;
    {
        constexpr uint16_t kFaceColors[6][4] = {
            { kHalfOne, kHalfZero, kHalfZero, kHalfOne },   // +X 빨강
            { kHalfZero, kHalfOne, kHalfZero, kHalfOne },   // -X 초록
            { kHalfZero, kHalfZero, kHalfOne, kHalfOne },   // +Y 파랑
            { kHalfOne, kHalfOne, kHalfZero, kHalfOne },    // -Y 노랑
            { kHalfOne, kHalfZero, kHalfOne, kHalfOne },    // +Z 자홍
            { kHalfZero, kHalfOne, kHalfOne, kHalfOne },    // -Z 청록
        };

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC cubeDesc{};
        cubeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        cubeDesc.Width = kCubeFaceSize;
        cubeDesc.Height = kCubeFaceSize;
        cubeDesc.DepthOrArraySize = 6;
        cubeDesc.MipLevels = 1;
        cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        cubeDesc.SampleDesc.Count = 1;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &cubeDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&cubeMap))))
        {
            outLog += "[2/4] 큐브맵 생성 실패\n";
            resources.Shutdown();
            return false;
        }

        // 전용 업로드 사이클(3-3의 체커보드와 같은 방식) — 렌더 프레임과
        // 분리해 실패 지점을 격리한다.
        constexpr uint32_t kFaceRowBytes = kCubeFaceSize * 8;
        constexpr uint32_t kFaceRowPitch = 256;   // 정렬 상한이 곧 4x4의 피치
        constexpr uint32_t kFaceBytes = kFaceRowPitch * kCubeFaceSize;

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] 업로드 BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        const auto upload = resources.AllocateUpload(
            kFaceBytes * 6, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (!upload.IsValid())
        {
            outLog += "[2/4] 업로드 링 할당 실패\n";
            resources.Shutdown();
            return false;
        }

        for (uint32_t face = 0; face < 6; ++face)
        {
            auto* faceBase = static_cast<uint8_t*>(upload.cpuAddress)
                + static_cast<size_t>(face) * kFaceBytes;
            for (uint32_t y = 0; y < kCubeFaceSize; ++y)
            {
                auto* row = reinterpret_cast<uint16_t*>(faceBase + y * kFaceRowPitch);
                for (uint32_t x = 0; x < kCubeFaceSize; ++x)
                {
                    row[x * 4 + 0] = kFaceColors[face][0];
                    row[x * 4 + 1] = kFaceColors[face][1];
                    row[x * 4 + 2] = kFaceColors[face][2];
                    row[x * 4 + 3] = kFaceColors[face][3];
                }
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = resources.Resolve(upload.buffer);
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = upload.offset
                + static_cast<uint64_t>(face) * kFaceBytes;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            src.PlacedFootprint.Footprint.Width = kCubeFaceSize;
            src.PlacedFootprint.Footprint.Height = kCubeFaceSize;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = kFaceRowPitch;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = cubeMap.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = face;

            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = cubeMap.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        resources.GetCommandList()->ResourceBarrier(1, &barrier);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] 업로드 EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();

        outLog += "[2/4] 합성 큐브맵 업로드(6면 원색) 완료\n";
    }

    sky.SetCubeMap(resources.RegisterExternalTexture(cubeMap.Get()),
        RHIFormat::RGBA16Float, 1);
    sky.SetScale(10.f);

    RHIReadback readback{};
    if (!resources.CreateReadback(kSkyWidth, kSkyHeight,
        EnhancedSkyBoxPass::kOutputFormat, 1, readback, error))
    {
        outLog += "[2/4] 리드백 생성 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    bool passed = true;
    EnhancedRenderGraph::Stats lastStats{};

    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, SkyCapture& outCapture)
        -> bool
    {
        frameContext.camera = &snapshot;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }

        if (!sky.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);
        sky.Declare(graph, frameContext);

        const RGHandle output = sky.GetOutput();
        if (!output.IsValid())
        {
            outLog += "스카이박스 출력이 선언되지 않았다\n";
            return false;
        }

        graph.AddPass("SkyBox.Readback",
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        if (!graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "Execute 실패: " + error + "\n";
            return false;
        }
        lastStats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "EndFrame 실패: " + error + "\n";
            return false;
        }
        resources.WaitForGpu();

        if (!resources.MapReadback(readback, outCapture.image, error))
        {
            outLog += error + "\n";
            return false;
        }
        return true;
    };

    // ── [3/4] 방향별 면 색 + 전면 커버 ──
    struct Expect
    {
        const char* name;
        float atX, atY, atZ;
        float r, g, b;
    };
    const Expect expects[] = {
        { "+X(빨강)", 1.f, 0.f, 0.f,   1.f, 0.f, 0.f },
        { "-Z(청록)", 0.f, 0.f, -1.f,  0.f, 1.f, 1.f },
        { "+Z(자홍)", 0.f, 0.f, 1.f,   1.f, 0.f, 1.f },
    };

    for (const Expect& expect : expects)
    {
        if (!passed) break;

        SkyCapture capture{};
        if (!renderOnce(SkyCamera(expect.atX, expect.atY, expect.atZ), capture))
        {
            passed = false;
            break;
        }

        const float r = capture.At(kSkyWidth / 2, kSkyHeight / 2, 0);
        const float g = capture.At(kSkyWidth / 2, kSkyHeight / 2, 1);
        const float b = capture.At(kSkyWidth / 2, kSkyHeight / 2, 2);
        const uint32_t covered = capture.CountCovered();

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[3/4] %s — 중심 RGB(%.2f %.2f %.2f) · 커버 %u/%u\n",
            expect.name, r, g, b, covered, kSkyWidth * kSkyHeight);
        outLog += line;

        // ① 방향 — 중심 픽셀이 그 면의 원색인가.
        if (std::fabs(r - expect.r) > 0.1f || std::fabs(g - expect.g) > 0.1f ||
            std::fabs(b - expect.b) > 0.1f)
        {
            outLog += "면 색이 다르다 — 방향 계산의 축이 뒤집혔거나 면 순서가 틀렸다\n";
            passed = false;
        }

        // ② 전면 — z = w x 0.99999가 깨지면(z=w) 깊이 LESS를 통과하지
        //   못해 여기가 0으로 떨어진다.
        if (covered != kSkyWidth * kSkyHeight)
        {
            outLog += "화면에 빈 곳이 있다 — 원평면 밀어넣기(z=w x 0.99999)가 깨졌다\n";
            passed = false;
        }
    }

    // ── [4/4] 그래프·검증 레이어 ──
    if (passed)
    {
        char line[128]{};
        std::snprintf(line, sizeof(line),
            "[4/4] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u\n",
            lastStats.passesDeclared, lastStats.passesExecuted,
            lastStats.passesCulled, lastStats.transientCreated);
        outLog += line;

        if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled ||
            2 != lastStats.transientCreated)
        {
            outLog += "그래프 통계가 다르다 — 패스 2·컬링 0·transient 2여야 한다\n";
            passed = false;
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    sky.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "스카이박스 패스 검증 통과\n" : "스카이박스 패스 검증 실패\n";
    return passed;
}

#endif
