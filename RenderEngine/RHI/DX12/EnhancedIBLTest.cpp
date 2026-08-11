#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedIBLGenerator.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedSceneRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// IBL 생성 체인 자가 검증 (PHASE 3-6).
//
// 반구가 갈리는 equirect(위 빨강 · 아래 초록)를 넣고 넷을 따로 단정한다:
//
//   ① rect→cube — +Y 면이 빨강, -Y 면이 초록인가(구면 매핑의 방향)
//   ② 조도       — +Y 법선은 빨강 우세, -Y는 초록 우세, ±X는 반반인가.
//      절대값은 원본의 톤 정책(로그 평균·NoL 이중 가중)에 묶여 있어
//      비율·우세만 본다 — 수식을 고치면 절대값 단정이 같이 흔들린다.
//   ③ 프리필터   — 거칠기 0(밉0)은 면 색이 또렷하고, 거칠기 1(밉5)은
//      반구가 섞여 색 차가 줄어드는가(수렴)
//   ④ BRDF LUT  — (NdotV≈1, 거칠기≈0) 모서리에서 A≈1·B≈0, 가운데는
//      에너지 보존(A+B ≤ 1 근처)인가
namespace
{
    constexpr uint32_t kIblCubeSize = 64;
    constexpr uint32_t kIblBrdfSize = 64;

    // 극점은 V CLAMP여야 반대편 위도 행이 섞이지 않는다. 일부러 낮은 8행을
    // 써서 +Y 면 중심이 첫 텍셀 중심 바깥을 샘플하게 만든다. V가 WRAP으로
    // 회귀하면 반대쪽 행이 약 44% 섞여 [3/5] 순색 단정이 즉시 실패한다.
    constexpr uint32_t kIblEquirectWidth = 256;
    constexpr uint32_t kIblEquirectHeight = 8;

    constexpr uint16_t kIblHalfOne = 0x3C00;

    // 구획 열의 자리(장 번호). 배치는 등차라 장 하나가 곧 구획 하나다.
    //   0 큐브+Y · 1 큐브-Y · 2 조도+Y · 3 조도-Y · 4 조도+X
    //   5 프리필터 밉0+Y · 6 밉5+Y · 7 밉0-Y · 8 밉5-Y · 9 LUT
    constexpr uint32_t kIblRegionCount = 10;
}

bool EnhancedSceneRenderer::RunIBLTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── IBL 생성 체인 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kIblCubeSize, kIblCubeSize, error))
    {
        outLog += "[1/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_ibl.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/5] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kIblCubeSize;
    frameContext.height = kIblCubeSize;

    EnhancedIBLGenerator generator;
    if (!generator.Initialize(frameContext, resources, error))
    {
        outLog += "[1/5] IBL 생성기 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/5] 셰이더 4종 컴파일·PSO 생성 통과\n";

    // ── [2/5] 합성 equirect — 위 반구 빨강 · 아래 반구 초록 ──
    //
    // 구면 매핑에서 v=0이 +Y(천정)다. 반구가 갈려 있으면 조도·프리필터의
    // 방향 적분이 '어느 반구를 봤는가'로 검증된다.
    ComPtr<ID3D12Resource> equirect;
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kIblEquirectWidth;
        desc.Height = kIblEquirectHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&equirect))))
        {
            outLog += "[2/5] equirect 생성 실패\n";
            resources.Shutdown();
            return false;
        }

        constexpr uint32_t kRowPitch = kIblEquirectWidth * 8;   // 2048 — 256 정렬 배수

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/5] 업로드 BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        const auto upload = resources.AllocateUpload(
            kRowPitch * kIblEquirectHeight, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (!upload.IsValid())
        {
            outLog += "[2/5] 업로드 링 할당 실패\n";
            resources.Shutdown();
            return false;
        }

        for (uint32_t y = 0; y < kIblEquirectHeight; ++y)
        {
            auto* row = reinterpret_cast<uint16_t*>(
                static_cast<uint8_t*>(upload.cpuAddress) + y * kRowPitch);
            const bool top = y < kIblEquirectHeight / 2;
            for (uint32_t x = 0; x < kIblEquirectWidth; ++x)
            {
                row[x * 4 + 0] = top ? kIblHalfOne : 0;
                row[x * 4 + 1] = top ? 0 : kIblHalfOne;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = kIblHalfOne;
            }
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = resources.Resolve(upload.buffer);
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = upload.offset;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        src.PlacedFootprint.Footprint.Width = kIblEquirectWidth;
        src.PlacedFootprint.Footprint.Height = kIblEquirectHeight;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = kRowPitch;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = equirect.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = equirect.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        resources.GetCommandList()->ResourceBarrier(1, &barrier);

        // ── 같은 프레임에서 생성 체인 전체를 기록한다 ──
        if (!generator.Generate(frameContext, equirect.Get(),
            DXGI_FORMAT_R16G16B16A16_FLOAT, kIblCubeSize, kIblBrdfSize, error))
        {
            outLog += "[2/5] 생성 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/5] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();

        outLog += "[2/5] equirect 업로드 + 생성 체인 기록·실행 완료\n";
    }

    // ── 리드백 — 필요한 면·밉만 구획으로 뜬다 ──
    //
    // ★ 장을 전부 큐브 면 크기로 잡는다(R2c-b2).
    //
    //   구획 열 중 둘은 밉5(2x2)이고 나머지는 64x64인데, 예전 코드도 오프셋을
    //   등차로 두고 행 간격을 면 것으로 통일해 두었다 — 즉 배치는 처음부터
    //   균일했고 크기만 달랐다. 작은 것은 장의 왼쪽 위 구석에 들어가고,
    //   그 둘을 읽는 자리도 (0,0)이라 읽는 쪽이 달라지지 않는다.
    RHIReadback readback{};
    {
        std::string readbackError;
        if (!resources.CreateReadback(kIblCubeSize, kIblCubeSize,
            EnhancedIBLGenerator::kFormat, kIblRegionCount, readback, readbackError))
        {
            outLog += "[3/5] 리드백 생성 실패: " + readbackError + "\n";
            resources.Shutdown();
            return false;
        }
    }

    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/5] 리드백 BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        auto* commandList = resources.GetCommandList();

        const auto toCopySource = [&](RHITextureHandle handle)
        {
            ID3D12Resource* const resource = resources.Resolve(handle);
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            commandList->ResourceBarrier(1, &barrier);
        };
        toCopySource(generator.GetCubeMap());
        toCopySource(generator.GetIrradianceMap());
        toCopySource(generator.GetPrefilteredMap());
        toCopySource(generator.GetBrdfLut());

        const auto copyRegion = [&](RHITextureHandle source, uint32_t subresource,
            uint32_t region)
        {
            resources.CopyToReadback(commandList, readback, resources.Resolve(source), region, subresource);
        };

        // 면 인덱스: +X 0 · -X 1 · +Y 2 · -Y 3. 서브리소스 = 밉 + 면 x 밉수.
        constexpr uint32_t kMips = EnhancedIBLGenerator::kPrefilterMips;
        copyRegion(generator.GetCubeMap(), 2, 0);
        copyRegion(generator.GetCubeMap(), 3, 1);
        copyRegion(generator.GetIrradianceMap(), 2, 2);
        copyRegion(generator.GetIrradianceMap(), 3, 3);
        copyRegion(generator.GetIrradianceMap(), 0, 4);
        copyRegion(generator.GetPrefilteredMap(), 0 + 2 * kMips, 5);
        copyRegion(generator.GetPrefilteredMap(), 5 + 2 * kMips, 6);
        copyRegion(generator.GetPrefilteredMap(), 0 + 3 * kMips, 7);
        copyRegion(generator.GetPrefilteredMap(), 5 + 3 * kMips, 8);
        copyRegion(generator.GetBrdfLut(), 0, 9);

        if (!resources.EndFrame(error))
        {
            outLog += "[3/5] 리드백 EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
    }

    bool passed = true;

    RHIReadbackImage captured{};
    {
        std::string readbackError;
        if (!resources.MapReadback(readback, captured, readbackError))
        {
            outLog += "[3/5] 리드백 Map 실패: " + readbackError + "\n";
            resources.Shutdown();
            return false;
        }
    }

    // 구획 = 장. 디코드와 행 간격은 캡처가 안다.
    const auto region = [&](uint32_t index, uint32_t x, uint32_t y, uint32_t channel)
    {
        return captured.At(x, y, channel, index);
    };
    constexpr uint32_t kMid = kIblCubeSize / 2;

    // ── [3/5] rect→cube — 방향 ──
    {
        const float upR = region(0, kMid, kMid, 0);
        const float upG = region(0, kMid, kMid, 1);
        const float downR = region(1, kMid, kMid, 0);
        const float downG = region(1, kMid, kMid, 1);

        char line[160]{};
        std::snprintf(line, sizeof(line),
            "[3/5] rect→cube — +Y(R %.2f G %.2f) · -Y(R %.2f G %.2f)\n",
            upR, upG, downR, downG);
        outLog += line;

        if (upR < 0.9f || upG > 0.1f || downG < 0.9f || downR > 0.1f)
        {
            outLog += "천정/바닥 면 색이 틀렸다 — 구면 매핑의 v 방향이 뒤집혔다\n";
            passed = false;
        }
    }

    // ── [4/5] 조도 + 프리필터 — 반구 적분의 방향과 수렴 ──
    if (passed)
    {
        const float irrUpR = region(2, kMid, kMid, 0);
        const float irrUpG = region(2, kMid, kMid, 1);
        const float irrDownR = region(3, kMid, kMid, 0);
        const float irrDownG = region(3, kMid, kMid, 1);
        const float irrSideR = region(4, kMid, kMid, 0);
        const float irrSideG = region(4, kMid, kMid, 1);

        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[4/5] 조도 — +Y(R %.2f G %.2f) · -Y(R %.2f G %.2f) · +X(R %.2f G %.2f)\n",
            irrUpR, irrUpG, irrDownR, irrDownG, irrSideR, irrSideG);
        outLog += line;

        // 절대값은 원본 톤 정책의 몫 — 우세와 대칭만 본다.
        if (irrUpR < irrUpG * 2.f)
        {
            outLog += "+Y 조도가 빨강 우세가 아니다 — 반구 적분 방향이 틀렸다\n";
            passed = false;
        }
        if (irrDownG < irrDownR * 2.f)
        {
            outLog += "-Y 조도가 초록 우세가 아니다 — 반구 적분 방향이 틀렸다\n";
            passed = false;
        }
        const float sideRatio = irrSideR / (std::max)(irrSideG, 1e-4f);
        if (sideRatio < 0.5f || sideRatio > 2.f)
        {
            outLog += "+X 조도가 반반이 아니다 — 접선 기저가 기울었다\n";
            passed = false;
        }

        const float sharpGap = region(5, kMid, kMid, 0) - region(5, kMid, kMid, 1);
        const float roughGap = region(6, 0, 0, 0) - region(6, 0, 0, 1);
        const float sharpGapDown = region(7, kMid, kMid, 1) - region(7, kMid, kMid, 0);
        const float roughGapDown = region(8, 0, 0, 1) - region(8, 0, 0, 0);

        char line2[192]{};
        std::snprintf(line2, sizeof(line2),
            "[4/5] 프리필터 — +Y 색차(밉0 %.2f → 밉5 %.2f) · -Y(밉0 %.2f → 밉5 %.2f)\n",
            sharpGap, roughGap, sharpGapDown, roughGapDown);
        outLog += line2;

        if (sharpGap < 0.8f || sharpGapDown < 0.8f)
        {
            outLog += "거칠기 0이 면 색을 보존하지 못한다 — 거울 반사가 아니다\n";
            passed = false;
        }
        // 거칠기 1에서도 색차는 완만하게만 준다(실측 1.00 → 0.82) — GGX가
        // 넓어져도 NdotL 가중이 위 반구를 계속 우대하기 때문이다. 그래서
        // '많이 섞였는가'가 아니라 '측정 가능하게 줄었는가'를 본다.
        if (roughGap > sharpGap * 0.95f || roughGapDown > sharpGapDown * 0.95f)
        {
            outLog += "거칠기 1이 수렴하지 않는다 — GGX 로브가 안 넓어진다\n";
            passed = false;
        }
    }

    // ── [5/5] BRDF LUT ──
    if (passed)
    {
        constexpr uint32_t kLut = 9;
        const float cornerA = region(kLut, kIblBrdfSize - 1, 0, 0);   // NdotV≈1 · 거칠기≈0
        const float cornerB = region(kLut, kIblBrdfSize - 1, 0, 1);
        const float centerA = region(kLut, kIblBrdfSize / 2, kIblBrdfSize / 2, 0);
        const float centerB = region(kLut, kIblBrdfSize / 2, kIblBrdfSize / 2, 1);

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[5/5] BRDF LUT — 모서리(A %.3f B %.3f) · 가운데(A %.3f B %.3f)\n",
            cornerA, cornerB, centerA, centerB);
        outLog += line;

        // (NdotV≈1, 거칠기≈0)의 해석적 극한은 A→1 · B→0이다.
        if (cornerA < 0.9f || cornerB > 0.05f)
        {
            outLog += "매끈한 모서리가 (1,0)이 아니다 — 사전 적분이 틀렸다\n";
            passed = false;
        }
        if (centerA < 0.2f || centerA + centerB > 1.1f)
        {
            outLog += "가운데가 에너지 보존을 깬다 — G 항이나 가중이 틀렸다\n";
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

    generator.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "IBL 생성 체인 검증 통과\n" : "IBL 생성 체인 검증 실패\n";
    return passed;
}

#endif
