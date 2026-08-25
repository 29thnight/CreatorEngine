// Forward+ 자가 검증 (E5에서 EnhancedForwardPass.cpp 말미에서 분리 —
// 프로덕션 패스 파일이 테스트 헤더(DX12TestTextureRegistration)를 물던
// 역결합이 래칫의 상향 간선으로 드러났다).
#include "Render/Passes/Geometry/EnhancedForwardPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "../DX12TestTextureRegistration.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/RHIEncoder.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "Mesh.h"
#include "RHI/RHIShaderCompiler.h"
#include <algorithm>
#include <sstream>
#include <vector>
#include <wrl/client.h>

// ── 자가 검증 ──
//
// 알려진 배치로 컬링을 단정한다. 광원 하나를 화면 중앙 표면 위에 두면
// 중앙 타일의 카운트는 1, 구석 타일은 0이어야 한다. 이것이 틀리는 방향이
// 둘 다 위험하다 — 중앙이 0이면 광원이 사라지고(어두워짐), 구석이 1이면
// 컬링이 안 도는 것이다(Forward+의 이득이 통째로 사라진다).
bool DX12Test::RunForwardPlusTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 128;
    constexpr uint32_t kHeight = 128;

    outLog += "── Forward+ 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_fwd.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/3] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // 카메라: 원점에서 +Z를 본다. 깊이 0.6의 표면이 화면 전체에 깔린다.
    FrameCameraSnapshot camera{};
    camera.view = math::matrix4x4::identity();
    camera.projection = math::perspective_fov_lh(DirectX::XM_PIDIV2, 1.f, 0.1f, 100.f);
    camera.inverseView = math::matrix4x4::identity();
    camera.inverseProjection = math::inverse(camera.projection);

    // 광원: 깊이 0.6이 뷰 z ≈ 0.25이므로 그 근처 화면 중앙에 반경 0.05짜리
    // 점광 하나. 중앙 타일에는 닿고 구석에는 절대 닿지 않는 크기다.
    const float surfaceViewZ = 0.25f;
    std::vector<EnhancedLight> lights(1);
    lights[0].position = Mathf::Vector4(0.f, 0.f, surfaceViewZ, 1.f);   // 점광
    lights[0].attenuation = Mathf::Vector4(1.f, 0.f, 0.f, 0.05f);       // 반경

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;
    frameContext.camera = &camera;
    frameContext.lights = &lights;

    EnhancedForwardPass forward;
    if (!forward.Initialize(frameContext, error))
    {
        outLog += "[1/3] Forward+ 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/3] 컬링 셰이더 컴파일·PSO 생성 통과\n";

    bool passed = true;
    {
        // 깊이 텍스처를 0.6으로 채운다. ClearUnorderedAccessView가 제일
        // 짧지만 디스크립터 두 벌(셰이더 가시 + 비가시)이 필요해 오히려
        // 길다 — 업로드 텍스처 복사로 채운다.
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kWidth;
        depthDesc.Height = kHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthDesc.SampleDesc.Count = 1;

        ComPtr<ID3D12Resource> depth;
        DX12TestTextureRegistration depthRegistration;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[2/3] 깊이 생성 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        depthRegistration.Register(resources, depth.Get());
        if (!depthRegistration.IsValid())
        {
            outLog += "[2/3] 깊이 핸들 등록 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/3] BeginFrame 실패: " + error + "\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 깊이 채우기: 업로드 링에서 행 정렬 규칙에 맞춰 복사한다.
        {
            const uint32_t rowPitch =
                ((kWidth * 4u) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
                & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
            const auto upload = resources.AllocateUpload(
                RHIUploadRequest{ static_cast<uint64_t>(rowPitch) * kHeight,
                    RHIUploadUsage::TextureCopy, 1 });
            if (upload.IsValid())
            {
                for (uint32_t y = 0; y < kHeight; ++y)
                {
                    auto* row = reinterpret_cast<float*>(
                        static_cast<uint8_t*>(upload.cpuAddress)
                        + static_cast<size_t>(y) * rowPitch);
                    for (uint32_t x = 0; x < kWidth; ++x) row[x] = 0.6f;
                }

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = depth.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = resources.Resolve(upload.buffer);
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = upload.offset;
                src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
                src.PlacedFootprint.Footprint.Width = kWidth;
                src.PlacedFootprint.Footprint.Height = kHeight;
                src.PlacedFootprint.Footprint.Depth = 1;
                src.PlacedFootprint.Footprint.RowPitch = rowPitch;

                resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                // ★ 여기가 NON_PIXEL 로 전이하면서 그래프에는 ShaderResource
                //   (=ALL)라고 말하고 있었다. 그래프의 첫 usage 도
                //   ShaderResource 라 전이가 안 나와서 드러나지 않던 불일치다 —
                //   배리어가 한 번이라도 나왔으면 before 가 실제와 어긋난다.
                //   중립 어휘로 옮기면서 선언을 참으로 만든다(V3-c).
                const RHITransition toSrv[] = {
                    { depthRegistration.Handle(),
                      RHIResourceState::CopyDest, RHIResourceState::ShaderResource } };
                resources.TransitionResources(toSrv);
            }
        }

        if (!forward.PrepareFrame(frameContext, error))
        {
            outLog += "[2/3] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        EnhancedRenderGraph graph(resources);

        EnhancedForwardPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depthRegistration.Handle(),
            RHIResourceState::ShaderResource, "Fwd.TestDepth");
        forward.SetInputs(inputs);

        forward.Declare(graph, frameContext);

        // 타일 카운트 리드백.
        const uint32_t tileGrid = kWidth / EnhancedForwardPass::kTileSize;   // 8
        const uint32_t tileTotal = tileGrid * tileGrid;

        RHIReadback readback{};
        {
            std::string readbackError;
            if (!resources.CreateBufferReadback(tileTotal * sizeof(uint32_t),
                readback, readbackError))
            {
                outLog += "[2/3] 리드백 생성 실패: " + readbackError + "\n";
                passed = false;
            }
        }

        if (passed)
        {
            // 타일 버퍼가 그래프에 들어왔으므로(R4-2b) usage만 선언한다.
            // 예전에는 UAV → COPY_SOURCE → UAV 전이를 손으로 걸며 before를
            // '늘 UAV'로 단정했다.
            graph.AddPass("Fwd.Readback",
                { { inputs.depth,                 RHIResourceState::ShaderResource },
                  { forward.GetTileCountHandle(), RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyBufferToReadback(
                        readback, forward.GetTileCountBuffer());
                }, true);

            if (!graph.Compile(error))
            {
                outLog += "[2/3] Compile 실패: " + error + "\n";
                passed = false;
            }
            if (passed && !graph.Execute(error))
            {
                outLog += "[2/3] Execute 실패: " + error + "\n";
                passed = false;
            }
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }

        // ── 단정 ──
        if (passed)
        {
            RHIReadbackImage captured{};
            std::string readbackError;
            if (!resources.MapReadback(readback, captured, readbackError))
            {
                outLog += "[3/3] 리드백 Map 실패: " + readbackError + "\n";
                passed = false;
            }
            else
            {
                const uint32_t* counts = captured.Elements<uint32_t>();
                if (nullptr == counts)
                {
                    outLog += "[3/3] 리드백이 비었다\n";
                    return false;
                }

                const uint32_t centerTile = (tileGrid / 2) * tileGrid + (tileGrid / 2);
                const uint32_t cornerTile = 0;

                uint32_t litTiles = 0;
                for (uint32_t i = 0; i < tileTotal; ++i)
                {
                    if (0 != counts[i]) ++litTiles;
                }

                char line[224]{};
                std::snprintf(line, sizeof(line),
                    "[3/3] 타일 %ux%u — 중앙 %u · 구석 %u · 켜진 타일 %u/%u\n",
                    tileGrid, tileGrid, counts[centerTile], counts[cornerTile],
                    litTiles, tileTotal);
                outLog += line;

                // 중앙이 0이면 광원이 사라진다(어두워짐). 구석이 1이면 컬링이
                // 안 도는 것이다(전 타일에 다 들어가 Forward+의 이득이 없다).
                if (0 == counts[centerTile])
                {
                    outLog += "중앙 타일에 광원이 없다 — 컬링이 광원을 떨어뜨렸다\n";
                    passed = false;
                }
                if (0 != counts[cornerTile])
                {
                    outLog += "구석 타일에 광원이 있다 — 컬링이 자르지 않는다\n";
                    passed = false;
                }
                if (litTiles == tileTotal)
                {
                    outLog += "모든 타일이 켜졌다 — 반경 컬링이 죽었다\n";
                    passed = false;
                }
            }
        }
        depthRegistration.Reset();
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    forward.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "Forward+ 검증 통과\n" : "Forward+ 검증 실패\n";
    return passed;
}
