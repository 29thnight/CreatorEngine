#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedUIPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
// 자기가 쓰는 것은 자기가 포함한다 — 텍스처 캐시가 DX11 디바이스를
// 요구하고, 그것이 여기 있다.
#include "../../DeviceState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// UI 패스 자가 검증 (PHASE 3-6).
//
// ── 넷을 따로 단정한다 ──
//
//   ① 배칭   — 같은 텍스처가 연속하면 한 배치로 묶이는가
//   ② 순서   — 나중에 그린 것이 위에 오는가(UI에서 가장 중요하다)
//   ③ 블렌딩 — 반투명이 아래를 비추는가
//   ④ 좌표   — 화면 픽셀 좌표가 그 자리에 그려지는가
//
// ★ ②가 이 검증의 핵심이다. 배칭을 넣으면서 순서가 뒤집히는 것이 가장
//   흔한 실수인데(텍스처로 전역 정렬하면 바로 그렇게 된다), 그 증상은
//   '가끔 UI가 이상하게 겹친다'로만 나타나 원인을 짚기 어렵다.
//
// ★ 배치 수는 그림으로 알 수 없다. 한 건도 안 묶여도 화면은 똑같이
//   나온다 — GBuffer에서 '드로우 704에 배치 704'를 수치로 보고서야
//   병합이 한 건도 안 일어난 것을 알았다.
namespace
{
    constexpr uint32_t kUIWidth = 256;
    constexpr uint32_t kUIHeight = 256;

    float UiHalfToFloat(uint16_t bits)
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
}

bool EnhancedSceneRenderer::RunUITest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── UI 패스 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kUIWidth, kUIHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_ui.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.textureCache = &textureCache;
    frameContext.width = kUIWidth;
    frameContext.height = kUIHeight;

    EnhancedUIPass ui;
    if (!ui.Initialize(frameContext, error))
    {
        outLog += "[1/4] UI 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과\n";

    // ── 알려진 배치 ──
    //
    // 텍스처를 안 주므로 전부 1x1 흰색이다. 즉 연속한 사각형은 전부
    // 같은 텍스처라 배치 하나로 묶여야 한다 — 배칭이 도는지가 그것으로 갈린다.
    std::vector<EnhancedUIPass::Rect> rects;

    // ① 왼쪽 위: 불투명 빨강 (10,10)~(60,60)
    {
        EnhancedUIPass::Rect rect{};
        rect.left = 10.f; rect.top = 10.f; rect.right = 60.f; rect.bottom = 60.f;
        rect.color = { 1.f, 0.f, 0.f, 1.f };
        rect.layerOrder = 0;
        rects.push_back(rect);
    }

    // ② 그 위에 겹치는 불투명 파랑 (35,35)~(85,85).
    //    layerOrder가 커서 나중에 그려진다 — 겹친 곳은 파랑이어야 한다.
    {
        EnhancedUIPass::Rect rect{};
        rect.left = 35.f; rect.top = 35.f; rect.right = 85.f; rect.bottom = 85.f;
        rect.color = { 0.f, 0.f, 1.f, 1.f };
        rect.layerOrder = 1;
        rects.push_back(rect);
    }

    // ③ 오른쪽: 불투명 초록 바탕 (140,10)~(240,110)
    {
        EnhancedUIPass::Rect rect{};
        rect.left = 140.f; rect.top = 10.f; rect.right = 240.f; rect.bottom = 110.f;
        rect.color = { 0.f, 1.f, 0.f, 1.f };
        rect.layerOrder = 0;
        rects.push_back(rect);
    }

    // ④ 그 위에 반투명 빨강(알파 0.5). 블렌딩이 돌면 초록과 섞여야 한다.
    {
        EnhancedUIPass::Rect rect{};
        rect.left = 160.f; rect.top = 30.f; rect.right = 220.f; rect.bottom = 90.f;
        rect.color = { 1.f, 0.f, 0.f, 0.5f };
        rect.layerOrder = 1;
        rects.push_back(rect);
    }

    // ★ 목록 순서를 일부러 뒤섞어 둔다.
    //
    //   layerOrder가 순서를 정하는지 확인하려면 목록 순서와 그리는 순서가
    //   달라야 한다. 목록 순서 그대로 그려도 통과하는 배치였다면 정렬이
    //   죽어 있어도 알 수 없다.
    std::swap(rects[1], rects[2]);

    ui.SetRects(&rects);

    constexpr uint64_t kRowPitch =
        ((static_cast<uint64_t>(kUIWidth) * 8ull) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    ComPtr<ID3D12Resource> readback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kRowPitch * kUIHeight;
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
            ui.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    EnhancedRenderGraph::Stats graphStats{};

    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            ui.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!ui.PrepareFrame(frameContext, error))
        {
            outLog += "[2/4] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);

        ui.Declare(graph, frameContext);

        const RGHandle output = ui.GetOutput();
        if (!output.IsValid())
        {
            outLog += "[2/4] UI 출력이 선언되지 않았다\n";
            passed = false;
        }
        else
        {
            graph.AddPass("UI.Readback",
                { { output, RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(output);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = readback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Footprint.Format = EnhancedUIPass::kOutputFormat;
                    dst.PlacedFootprint.Footprint.Width = kUIWidth;
                    dst.PlacedFootprint.Footprint.Height = kUIHeight;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(kRowPitch);

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }, true);

            if (!graph.Compile(resources.GetDevice(), error))
            {
                outLog += "[2/4] Compile 실패: " + error + "\n";
                passed = false;
            }
            if (passed && !graph.Execute(resources.GetCommandList(), error))
            {
                outLog += "[2/4] Execute 실패: " + error + "\n";
                passed = false;
            }
            graphStats = graph.GetStats();
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }
    }

    if (passed)
    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 그래프 — 선언 %u · 실행 %u · 사각형 %u · 배치 %u\n",
            graphStats.passesDeclared, graphStats.passesExecuted,
            ui.GetLastRectCount(), ui.GetLastBatchCount());
        outLog += line;

        if (graphStats.passesExecuted < 2)
        {
            outLog += "패스가 걷어내졌다 — 결과를 읽는 사슬이 끊겼다\n";
            passed = false;
        }

        // ① 배칭 — 텍스처가 전부 같으므로(흰색) 배치는 하나여야 한다.
        //
        //   묶이지 않아도 화면은 똑같이 나오므로 이 수를 안 보면
        //   배칭이 죽은 채로 통과한다.
        if (4 != ui.GetLastRectCount())
        {
            outLog += "사각형 수가 넷이 아니다 — 목록이 안 넘어갔다\n";
            passed = false;
        }
        if (1 != ui.GetLastBatchCount())
        {
            outLog += "같은 텍스처인데 배치가 하나가 아니다 — 배칭이 안 묶는다\n";
            passed = false;
        }
    }

    if (passed)
    {
        void* mapped = nullptr;
        D3D12_RANGE range{ 0, static_cast<SIZE_T>(kRowPitch * kUIHeight) };
        if (FAILED(readback->Map(0, &range, &mapped)))
        {
            outLog += "[3/4] 리드백 Map 실패\n";
            passed = false;
        }
        else
        {
            const auto at = [&](uint32_t x, uint32_t y, uint32_t channel)
            {
                const auto* row = reinterpret_cast<const uint16_t*>(
                    static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * kRowPitch);
                return UiHalfToFloat(row[x * 4 + channel]);
            };

            // 표본 자리
            const float redOnlyR = at(20, 20, 0);      // ① 빨강만
            const float overlapR = at(50, 50, 0);      // ①②가 겹친 곳
            const float overlapB = at(50, 50, 2);
            const float greenOnlyG = at(150, 20, 1);   // ③ 초록만
            const float blendR = at(190, 60, 0);       // ③④ 반투명 겹침
            const float blendG = at(190, 60, 1);
            const float outsideA = at(120, 200, 3);    // 아무것도 없는 곳

            char line[288]{};
            std::snprintf(line, sizeof(line),
                "[3/4] 빨강 %.3f · 겹침(R %.3f B %.3f) · 초록 %.3f · "
                "반투명(R %.3f G %.3f) · 바깥 알파 %.3f\n",
                redOnlyR, overlapR, overlapB, greenOnlyG, blendR, blendG, outsideA);
            outLog += line;

            // ④ 좌표 — 지정한 자리에 그려졌는가.
            if (redOnlyR < 0.9f)
            {
                outLog += "빨강 사각형 자리가 비었다 — 화면 좌표 변환이 틀렸다\n";
                passed = false;
            }
            if (greenOnlyG < 0.9f)
            {
                outLog += "초록 사각형 자리가 비었다 — 화면 좌표 변환이 틀렸다\n";
                passed = false;
            }
            if (outsideA > 0.01f)
            {
                outLog += "아무것도 없어야 할 곳이 채워졌다 — 사각형 크기가 틀렸다\n";
                passed = false;
            }

            // ② 순서 — 겹친 곳은 나중에 그린 파랑이어야 한다.
            //
            //   목록에서는 파랑이 빨강보다 뒤에 있지 않다(일부러 뒤섞었다).
            //   layerOrder가 순서를 정해야만 이 단정이 선다.
            if (overlapB < 0.9f || overlapR > 0.1f)
            {
                outLog += "겹친 곳이 파랑이 아니다 — layerOrder가 순서를 못 정한다\n";
                passed = false;
            }

            // ③ 블렌딩 — 반투명 빨강 아래로 초록이 비쳐야 한다.
            //
            //   블렌딩이 죽으면 빨강이 초록을 통째로 덮어 G가 0이 된다.
            //   알파 0.5면 대략 반반이다.
            if (blendG < 0.2f || blendG > 0.8f)
            {
                outLog += "반투명 아래로 초록이 안 비친다 — 알파 블렌딩이 죽었다\n";
                passed = false;
            }
            if (blendR < 0.2f)
            {
                outLog += "반투명 빨강이 안 보인다 — 블렌딩 계수가 뒤집혔다\n";
                passed = false;
            }

            readback->Unmap(0, nullptr);
        }
    }

    // ── 배칭이 실제로 갈리는지 ──
    //
    // 배치 하나가 나온 것만으로는 '묶었다'와 '애초에 하나였다'가 구분되지
    // 않는다. 텍스처를 번갈아 두면 배치가 늘어야 한다.
    if (passed)
    {
        // 흰색과 '흰색이 아닌 것'을 번갈아 두려면 텍스처가 둘 필요한데,
        // 이 검증에는 텍스처 자원이 없다. 대신 포인터만 다르게 둔 가짜
        // 주소로 '텍스처가 다르다'는 판단만 확인한다 — 배칭이 보는 것은
        // 포인터의 같고 다름뿐이라 이것으로 충분하고, 실제 업로드는
        // 씬 연결(2단계)에서 확인한다.
        Texture* const fakeA = reinterpret_cast<Texture*>(0x1);
        Texture* const fakeB = reinterpret_cast<Texture*>(0x2);

        std::vector<EnhancedUIPass::Rect> mixed = rects;
        mixed[0].texture = fakeA;
        mixed[1].texture = fakeB;
        mixed[2].texture = fakeA;
        mixed[3].texture = fakeB;

        ui.SetRects(&mixed);
        if (!ui.PrepareFrame(frameContext, error))
        {
            outLog += "[4/4] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            char line[192]{};
            std::snprintf(line, sizeof(line),
                "[4/4] 텍스처를 번갈아 두면 — 사각형 %u · 배치 %u\n",
                ui.GetLastRectCount(), ui.GetLastBatchCount());
            outLog += line;

            if (ui.GetLastBatchCount() <= 1)
            {
                outLog += "텍스처가 다른데 배치가 안 갈린다 — 배치 경계 판단이 죽었다\n";
                passed = false;
            }
        }

        // 다음 사용자를 위해 되돌려 둔다.
        ui.SetRects(&rects);
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    ui.Shutdown();
    textureCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "UI 패스 검증 통과\n" : "UI 패스 검증 실패\n";
    return passed;
}

#endif
