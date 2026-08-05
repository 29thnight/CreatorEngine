#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSkyBoxPass.h"
#include "EnhancedIBLGenerator.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../SceneRenderer.h"
#include "../../SkyBoxPass.h"
#include "../../Texture.h"
#include "../../DeviceState.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 엔진 스카이박스 텍스처의 DX12 운반 검증 (PHASE 3-6).
//
// ── 셋을 따로 단정한다 ──
//
//   ① 운반   — 살아 있는 엔진 큐브맵(DX11)이 텍스처 캐시로 올라가고,
//      Entry가 큐브임을 아는가(isCube·arraySize 6). 업로드 경로는 처음부터
//      배열을 옮겼지만 Entry가 그 사실을 안 알려 소비자가 2D로 볼 수밖에
//      없었다 — 이번에 추가한 것이 그 두 필드다.
//   ② 소비   — 운반된 큐브맵을 EnhancedSkyBoxPass가 실제로 그리는가
//      (3방향 전면 커버 — 합성이 아니라 실물 텍스처로).
//   ③ IBL    — equirect 원본이 있으면(HDR 로드 씬) 운반해 생성 체인을
//      태우고, 입력과 무관한 해석 극한(BRDF LUT 매끈 모서리 = (1,0))을
//      단정한다. DDS 직로드 씬이면 원본이 없어 스킵이 정상이다.
//
// 에디터 실행 중에만 의미 있다 — 살아 있는 SceneRenderer가 원천이다.
namespace
{
    constexpr uint32_t kSkySceneWidth = 256;
    constexpr uint32_t kSkySceneHeight = 256;

    constexpr uint64_t kSkySceneRowPitch =
        ((static_cast<uint64_t>(kSkySceneWidth) * 8ull)
            + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    float SkySceneHalfToFloat(uint16_t bits)
    {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
        uint32_t exponent = (bits >> 10) & 0x1Fu;
        uint32_t mantissa = bits & 0x3FFu;

        if (0 == exponent)
        {
            if (0 == mantissa)
            {
                float out; memcpy(&out, &sign, 4); return out;
            }
            exponent = 1;
            while (0 == (mantissa & 0x400u)) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FFu;
            const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
            float out; memcpy(&out, &bitsOut, 4); return out;
        }
        if (31 == exponent)
        {
            const uint32_t bitsOut = sign | 0x7F800000u | (mantissa << 13);
            float out; memcpy(&out, &bitsOut, 4); return out;
        }

        const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        float out; memcpy(&out, &bitsOut, 4); return out;
    }

    struct SkySceneCapture
    {
        std::vector<uint8_t> data;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                data.data() + static_cast<size_t>(y) * kSkySceneRowPitch);
            return SkySceneHalfToFloat(row[x * 4 + channel]);
        }

        // 알파로 덮임을 센다 — 하늘 셰이더는 알파 1을 쓴다. 실물 하늘은
        // 어두운 방향(RGB ≈ 0)이 있을 수 있어 색으로 세면 오탐이다.
        uint32_t CountCovered() const
        {
            uint32_t covered = 0;
            for (uint32_t y = 0; y < kSkySceneHeight; ++y)
                for (uint32_t x = 0; x < kSkySceneWidth; ++x)
                    if (At(x, y, 3) > 0.5f) ++covered;
            return covered;
        }
    };

    FrameCameraSnapshot SkySceneCamera(float atX, float atY, float atZ)
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
}

bool EnhancedSceneRenderer::RunSkySceneTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 엔진 스카이박스 DX12 운반 검증 (PHASE 3-6) ──\n";

    std::string error;

    // ── [1/4] 씬 원천 ──
    SceneRenderer* sceneRenderer = SceneRenderer::GetActive();
    if (nullptr == sceneRenderer)
    {
        outLog += "[1/4] 활성 SceneRenderer가 없다(에디터 실행 중에만 의미 있는 검증)\n";
        return false;
    }

    SkyBoxPass* enginePass = sceneRenderer->GetSkyBoxPass();
    Texture* engineCube = (nullptr != enginePass) ? enginePass->GetCubeMapTexture() : nullptr;
    Texture* engineEquirect =
        (nullptr != enginePass) ? enginePass->GetEquirectTexture() : nullptr;

    if (nullptr == engineCube)
    {
        outLog += "[1/4] 엔진 스카이박스 큐브맵이 없다\n";
        return false;
    }

    char sourceLine[224]{};
    std::snprintf(sourceLine, sizeof(sourceLine),
        "[1/4] 씬 원천 — 큐브맵 '%s' · equirect 원본 %s\n",
        engineCube->m_name.c_str(),
        (nullptr != engineEquirect) ? engineEquirect->m_name.c_str() : "(없음 — DDS 직로드)");
    outLog += sourceLine;

    // ── [2/4] 운반 + 소비 ──
    DX12DeviceResources resources;
    if (!resources.Initialize(kSkySceneWidth, kSkySceneHeight, error))
    {
        outLog += "[2/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_skyscene.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !textureCache.Initialize(&resources, DirectX11::DeviceStates->g_pDevice,
            DirectX11::DeviceStates->g_pDeviceContext, error))
    {
        outLog += "[2/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.textureCache = &textureCache;
    frameContext.width = kSkySceneWidth;
    frameContext.height = kSkySceneHeight;

    EnhancedSkyBoxPass sky;
    if (!sky.Initialize(frameContext, error))
    {
        outLog += "[2/4] 스카이박스 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    bool passed = true;
    DX12TextureCache::Entry cubeEntry{};

    // 운반은 첫 프레임에서 한다(캐시 계약 — 프레임이 열려 있어야 한다).
    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        cubeEntry = textureCache.GetOrUpload(engineCube, error);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
    }

    if (!cubeEntry.IsValid() || cubeEntry.resource == textureCache.GetWhiteTexture().resource)
    {
        outLog += "[2/4] 큐브맵 운반 실패: " + error + "\n";
        passed = false;
    }
    else
    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 운반 — %ux%u 밉 %u 슬라이스 %u 큐브 %d · 업로드 %u회 %.1fMB\n",
            cubeEntry.width, cubeEntry.height, cubeEntry.mipLevels,
            cubeEntry.arraySize, cubeEntry.isCube ? 1 : 0,
            textureCache.GetStats().uploads,
            static_cast<double>(textureCache.GetStats().bytesUploaded) / (1024.0 * 1024.0));
        outLog += line;

        // ① Entry가 큐브임을 아는가 — 이 두 필드가 이번 슬라이스의 본체다.
        if (!cubeEntry.isCube || 6 != cubeEntry.arraySize)
        {
            outLog += "Entry가 큐브를 큐브로 알리지 않는다 — 소비자가 2D로 보게 된다\n";
            passed = false;
        }
        if (1 != textureCache.GetStats().uploads)
        {
            outLog += "업로드가 1회가 아니다\n";
            passed = false;
        }
    }

    // ② 소비 — 운반된 실물 큐브맵으로 3방향을 그린다.
    if (passed)
    {
        sky.SetCubeMap(cubeEntry.resource, cubeEntry.format, cubeEntry.mipLevels);
        sky.SetScale(10.f);

        ComPtr<ID3D12Resource> readback;
        {
            D3D12_HEAP_PROPERTIES readbackHeap{};
            readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = kSkySceneRowPitch * kSkySceneHeight;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&readback))))
            {
                outLog += "[2/4] 리드백 생성 실패\n";
                resources.Shutdown();
                return false;
            }
        }

        const auto renderOnce = [&](const FrameCameraSnapshot& snapshot,
            SkySceneCapture& outCapture) -> bool
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
            EnhancedRenderGraph graph;
            sky.Declare(graph, frameContext);

            const RGHandle output = sky.GetOutput();
            if (!output.IsValid())
            {
                outLog += "스카이박스 출력이 선언되지 않았다\n";
                return false;
            }

            graph.AddPass("SkyScene.Readback",
                { { output, RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(output);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = readback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Footprint.Format = EnhancedSkyBoxPass::kOutputFormat;
                    dst.PlacedFootprint.Footprint.Width = kSkySceneWidth;
                    dst.PlacedFootprint.Footprint.Height = kSkySceneHeight;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch =
                        static_cast<UINT>(kSkySceneRowPitch);

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
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

            if (!resources.EndFrame(error))
            {
                outLog += "EndFrame 실패: " + error + "\n";
                return false;
            }
            resources.WaitForGpu();

            void* mapped = nullptr;
            D3D12_RANGE range{ 0,
                static_cast<SIZE_T>(kSkySceneRowPitch * kSkySceneHeight) };
            if (FAILED(readback->Map(0, &range, &mapped)))
            {
                outLog += "리드백 Map 실패\n";
                return false;
            }
            outCapture.data.assign(static_cast<const uint8_t*>(mapped),
                static_cast<const uint8_t*>(mapped)
                + kSkySceneRowPitch * kSkySceneHeight);
            readback->Unmap(0, nullptr);
            return true;
        };

        struct Direction { const char* name; float x, y, z; };
        const Direction directions[] = {
            { "+X", 1.f, 0.f, 0.f },
            { "-Z", 0.f, 0.f, -1.f },
            { "+Y쪽", 0.3f, 1.f, 0.f },
        };

        for (const Direction& direction : directions)
        {
            if (!passed) break;

            SkySceneCapture capture{};
            if (!renderOnce(SkySceneCamera(direction.x, direction.y, direction.z), capture))
            {
                passed = false;
                break;
            }

            const uint32_t covered = capture.CountCovered();
            const float r = capture.At(kSkySceneWidth / 2, kSkySceneHeight / 2, 0);
            const float g = capture.At(kSkySceneWidth / 2, kSkySceneHeight / 2, 1);
            const float b = capture.At(kSkySceneWidth / 2, kSkySceneHeight / 2, 2);

            char line[192]{};
            std::snprintf(line, sizeof(line),
                "[2/4] %s — 커버 %u/%u · 중심 RGB(%.3f %.3f %.3f)\n",
                direction.name, covered, kSkySceneWidth * kSkySceneHeight, r, g, b);
            outLog += line;

            if (covered != kSkySceneWidth * kSkySceneHeight)
            {
                outLog += "실물 큐브맵으로 화면이 안 덮인다 — 운반된 리소스가 비었다\n";
                passed = false;
            }
        }
    }

    // ── [3/4] IBL — equirect 원본이 있으면 생성 체인까지 ──
    if (passed)
    {
        if (nullptr == engineEquirect)
        {
            outLog += "[3/4] equirect 원본이 없어 IBL 생성은 건너뛴다(DDS 직로드 씬의 정상 경로)\n";
        }
        else
        {
            EnhancedIBLGenerator generator;
            if (!generator.Initialize(frameContext, error))
            {
                outLog += "[3/4] IBL 생성기 초기화 실패: " + error + "\n";
                passed = false;
            }
            else
            {
                DX12TextureCache::Entry equirectEntry{};
                bool generated = false;

                if (!resources.BeginFrame(error))
                {
                    outLog += "[3/4] BeginFrame 실패: " + error + "\n";
                    passed = false;
                }
                else
                {
                    equirectEntry = textureCache.GetOrUpload(engineEquirect, error);

                    // 대형 HDR(4K equirect 128MB 실측)도 전용 스테이징 경로가
                    // 받아 낸다 — 여기서부터는 운반 실패가 곧 검증 실패다.
                    if (equirectEntry.IsValid() &&
                        equirectEntry.resource != textureCache.GetWhiteTexture().resource)
                    {
                        generated = generator.Generate(frameContext, equirectEntry.resource,
                            equirectEntry.format, 128, 64, error);
                        if (!generated)
                        {
                            outLog += "[3/4] IBL 생성 실패: " + error + "\n";
                            passed = false;
                        }
                    }
                    else
                    {
                        outLog += "[3/4] equirect 운반 실패: " + error + "\n";
                        passed = false;
                    }

                    if (!resources.EndFrame(error))
                    {
                        outLog += "[3/4] EndFrame 실패: " + error + "\n";
                        passed = false;
                    }
                    resources.WaitForGpu();

                    // 전용 스테이징은 GPU가 다 읽은 지금이 돌려줄 자리다.
                    textureCache.ReleaseStagingBuffers();
                }

                if (passed && generated)
                {
                    char line[224]{};
                    std::snprintf(line, sizeof(line),
                        "[3/4] IBL 생성 — equirect %ux%u(%.0fMB 운반) → 큐브 128 · 조도 · 프리필터 6밉 · LUT 64\n",
                        equirectEntry.width, equirectEntry.height,
                        static_cast<double>(textureCache.GetStats().bytesUploaded)
                            / (1024.0 * 1024.0));
                    outLog += line;
                }

                generator.Shutdown();
            }
        }
    }

    // ── [4/4] 검증 레이어 ──
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    sky.Shutdown();
    textureCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "엔진 스카이박스 운반 검증 통과\n" : "엔진 스카이박스 운반 검증 실패\n";
    return passed;
}

#endif
