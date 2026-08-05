#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSSPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// SSS 패스 자가 검증 (PHASE 3-6, 미구현 패스 이식 1차).
//
// ── 넷을 따로 단정한다 ──
//
//   ① 번짐   — 점 하나가 실제로 퍼지는가(블러가 도는가)
//   ② 두 축  — 가로 중간 결과는 가로로만, 최종은 양쪽으로 퍼졌는가.
//      한 축만 도는 실패는 최종 그림만 보면 '좀 덜 번졌네'로 지나간다.
//   ③ 표면 추종 — 깊이가 크게 다른 이웃으로는 안 번지는가. 이것이 없으면
//      실루엣 밖으로 피부색이 새는데, 그 증상은 인물 주변의 옅은 후광이라
//      알아채기 어렵다.
//   ④ 에너지 — 커널 합이 1 근처라 밝기가 보존되는가.
//
// ★ ③이 이 패스의 정체성이다. 단순 블러와 SSS를 가르는 것이 그 항이고,
//   빠뜨려도 화면은 '부드러워' 보인다.
namespace
{
    constexpr uint32_t kSSSWidth = 256;
    constexpr uint32_t kSSSHeight = 256;

    constexpr uint64_t kSSSRowPitch =
        ((static_cast<uint64_t>(kSSSWidth) * 8ull) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    constexpr uint16_t kSSSHalfOne = 0x3C00;

    float SSSHalfToFloat(uint16_t bits)
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

    struct SSSCapture
    {
        std::vector<uint8_t> data;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                data.data() + static_cast<size_t>(y) * kSSSRowPitch);
            return SSSHalfToFloat(row[x * 4 + channel]);
        }

        /// 한 행에서 임계 이상인 구간의 폭. 가로 번짐을 재는 자다.
        uint32_t RowSpread(uint32_t y, uint32_t channel, float threshold) const
        {
            uint32_t count = 0;
            for (uint32_t x = 0; x < kSSSWidth; ++x)
            {
                if (At(x, y, channel) > threshold) ++count;
            }
            return count;
        }

        /// 한 열에서 임계 이상인 구간의 폭. 세로 번짐.
        uint32_t ColumnSpread(uint32_t x, uint32_t channel, float threshold) const
        {
            uint32_t count = 0;
            for (uint32_t y = 0; y < kSSSHeight; ++y)
            {
                if (At(x, y, channel) > threshold) ++count;
            }
            return count;
        }

        double Sum(uint32_t channel) const
        {
            double total = 0.0;
            for (uint32_t y = 0; y < kSSSHeight; ++y)
                for (uint32_t x = 0; x < kSSSWidth; ++x)
                    total += At(x, y, channel);
            return total;
        }
    };
}

bool EnhancedSceneRenderer::RunSSSTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── SSS 패스 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kSSSWidth, kSSSHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_sss.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kSSSWidth;
    frameContext.height = kSSSHeight;

    FrameCameraSnapshot camera{};
    camera.fov = DirectX::XM_PIDIV4;   // 45도 — 셰이더가 도로 환산해 쓴다
    frameContext.camera = &camera;

    EnhancedSSSPass sss;
    if (!sss.Initialize(frameContext, error))
    {
        outLog += "[1/4] SSS 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    sss.SetKeepAlive(true);
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과\n";

    // ── [2/4] 합성 입력 — 두 점과 깊이 계단 ──
    //
    // 왼쪽 점(빨강)은 깊이가 평평한 영역 한가운데에, 오른쪽 점(초록)은
    // 깊이 계단 바로 옆에 둔다. 표면 추종이 돌면 오른쪽 점은 계단을
    // 넘어가지 못한다 — 그것이 ③의 재료다.
    constexpr uint32_t kLeftX = 64;
    constexpr uint32_t kRightX = 180;
    constexpr uint32_t kPointY = 128;
    constexpr uint32_t kStepX = 190;   // 오른쪽 점의 10픽셀 오른쪽에 계단

    ComPtr<ID3D12Resource> colorSource;
    ComPtr<ID3D12Resource> depthSource;
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        const auto makeTexture = [&](DXGI_FORMAT format, ComPtr<ID3D12Resource>& out) -> bool
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = kSSSWidth;
            desc.Height = kSSSHeight;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;

            return SUCCEEDED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&out)));
        };

        if (!makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, colorSource) ||
            !makeTexture(DXGI_FORMAT_R32_FLOAT, depthSource))
        {
            outLog += "[2/4] 입력 텍스처 생성 실패\n";
            resources.Shutdown();
            return false;
        }

        constexpr uint32_t kColorPitch = kSSSWidth * 8;
        constexpr uint32_t kDepthPitch = kSSSWidth * 4;

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // 색 — 검은 배경에 점 둘.
        {
            const auto upload = resources.GetUploadRing().Allocate(
                static_cast<uint64_t>(kColorPitch) * kSSSHeight,
                D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            if (!upload.IsValid())
            {
                outLog += "[2/4] 색 업로드 할당 실패\n";
                resources.Shutdown();
                return false;
            }

            memset(upload.cpuAddress, 0, static_cast<size_t>(kColorPitch) * kSSSHeight);
            auto* base = static_cast<uint8_t*>(upload.cpuAddress);
            const auto setPixel = [&](uint32_t x, uint32_t y, uint32_t channel)
            {
                auto* row = reinterpret_cast<uint16_t*>(base + static_cast<size_t>(y) * kColorPitch);
                row[x * 4 + channel] = kSSSHalfOne;
                row[x * 4 + 3] = kSSSHalfOne;
            };
            setPixel(kLeftX, kPointY, 0);    // 빨강
            setPixel(kRightX, kPointY, 1);   // 초록

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.resource;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = upload.offset;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            src.PlacedFootprint.Footprint.Width = kSSSWidth;
            src.PlacedFootprint.Footprint.Height = kSSSHeight;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = kColorPitch;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = colorSource.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        // 깊이 — 왼쪽은 평평(0.5), kStepX부터 멀어짐(0.9). 계단이 표면 추종을
        // 촉발한다.
        {
            const auto upload = resources.GetUploadRing().Allocate(
                static_cast<uint64_t>(kDepthPitch) * kSSSHeight,
                D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            if (!upload.IsValid())
            {
                outLog += "[2/4] 깊이 업로드 할당 실패\n";
                resources.Shutdown();
                return false;
            }

            auto* base = static_cast<uint8_t*>(upload.cpuAddress);
            for (uint32_t y = 0; y < kSSSHeight; ++y)
            {
                auto* row = reinterpret_cast<float*>(base + static_cast<size_t>(y) * kDepthPitch);
                for (uint32_t x = 0; x < kSSSWidth; ++x)
                {
                    row[x] = (x >= kStepX) ? 0.9f : 0.5f;
                }
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.resource;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = upload.offset;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
            src.PlacedFootprint.Footprint.Width = kSSSWidth;
            src.PlacedFootprint.Footprint.Height = kSSSHeight;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = kDepthPitch;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = depthSource.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (uint32_t i = 0; i < 2; ++i)
        {
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource = (0 == i) ? colorSource.Get() : depthSource.Get();
            barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        resources.GetCommandList()->ResourceBarrier(2, barriers);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
        outLog += "[2/4] 합성 입력(점 둘 + 깊이 계단) 업로드 완료\n";
    }

    ComPtr<ID3D12Resource> readback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kSSSRowPitch * kSSSHeight * 2;   // 가로 중간 + 최종
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

    bool passed = true;
    SSSCapture horizontalCapture{};
    SSSCapture finalCapture{};
    EnhancedRenderGraph::Stats stats{};

    {
        if (!resources.BeginFrame(error) || !sss.PrepareFrame(frameContext, error))
        {
            outLog += "[3/4] 준비 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph;

        // 외부 텍스처를 그래프에 들인다 — 이미 SHADER_RESOURCE 상태다.
        const RGHandle colorHandle = graph.ImportTexture(colorSource.Get(),
            RGResourceState::ShaderResource, "SSS.SourceColor");
        const RGHandle depthHandle = graph.ImportTexture(depthSource.Get(),
            RGResourceState::ShaderResource, "SSS.SourceDepth");

        EnhancedSSSPass::Inputs inputs{};
        inputs.color = colorHandle;
        inputs.depth = depthHandle;
        sss.SetInputs(inputs);
        sss.Declare(graph, frameContext);

        const RGHandle horizontal = sss.GetHorizontal();
        const RGHandle output = sss.GetOutput();
        if (!horizontal.IsValid() || !output.IsValid())
        {
            outLog += "[3/4] SSS 출력이 선언되지 않았다\n";
            resources.Shutdown();
            return false;
        }

        graph.AddPass("SSS.Readback",
            { { horizontal, RGResourceState::CopySource },
              { output,     RGResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                const auto copyOne = [&](RGHandle handle, uint64_t offset)
                {
                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(handle);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = readback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Offset = offset;
                    dst.PlacedFootprint.Footprint.Format = EnhancedSSSPass::kOutputFormat;
                    dst.PlacedFootprint.Footprint.Width = kSSSWidth;
                    dst.PlacedFootprint.Footprint.Height = kSSSHeight;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(kSSSRowPitch);

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                };

                copyOne(horizontal, 0);
                copyOne(output, kSSSRowPitch * kSSSHeight);
            }, true);

        if (!graph.Compile(resources.GetDevice(), error) ||
            !graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "[3/4] 그래프 실행 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        stats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "[3/4] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();

        void* mapped = nullptr;
        const SIZE_T bytes = static_cast<SIZE_T>(kSSSRowPitch) * kSSSHeight * 2;
        D3D12_RANGE range{ 0, bytes };
        if (FAILED(readback->Map(0, &range, &mapped)))
        {
            outLog += "[3/4] 리드백 Map 실패\n";
            resources.Shutdown();
            return false;
        }

        const auto* base = static_cast<const uint8_t*>(mapped);
        const size_t sliceBytes = static_cast<size_t>(kSSSRowPitch) * kSSSHeight;
        horizontalCapture.data.assign(base, base + sliceBytes);
        finalCapture.data.assign(base + sliceBytes, base + sliceBytes * 2);
        readback->Unmap(0, nullptr);
    }

    // ── [3/4] 번짐과 두 축 ──
    {
        const uint32_t hRow = horizontalCapture.RowSpread(kPointY, 0, 0.001f);
        const uint32_t hCol = horizontalCapture.ColumnSpread(kLeftX, 0, 0.001f);
        const uint32_t fRow = finalCapture.RowSpread(kPointY, 0, 0.001f);
        const uint32_t fCol = finalCapture.ColumnSpread(kLeftX, 0, 0.001f);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 번짐(빨강 점) — 가로 단계: 행 %u·열 %u · 최종: 행 %u·열 %u"
            " · 그래프 %u패스\n",
            hRow, hCol, fRow, fCol, stats.passesExecuted);
        outLog += line;

        // ① 번짐 — 점 하나가 실제로 퍼졌는가.
        if (hRow < 3)
        {
            outLog += "가로로 안 번졌다 — 블러가 돌지 않는다\n";
            passed = false;
        }
        // ② 두 축 — 가로 단계는 세로로 안 퍼져야 하고(열이 1), 최종은
        //   양쪽으로 퍼져야 한다. 한 축만 도는 실패가 여기서 잡힌다.
        if (hCol > 1)
        {
            outLog += "가로 단계가 세로로도 번졌다 — 축 분리가 깨졌다\n";
            passed = false;
        }
        if (fCol < 3)
        {
            outLog += "최종이 세로로 안 번졌다 — 두 번째 축이 안 돈다\n";
            passed = false;
        }
        if (3 != stats.passesExecuted)
        {
            outLog += "패스가 셋(가로·세로·리드백)이 아니다\n";
            passed = false;
        }
    }

    // ── [4/4] 표면 추종과 에너지 ──
    if (passed)
    {
        // ③ 계단 너머로 번지지 않는가. 오른쪽 점(초록)은 kStepX 왼쪽에 있고,
        //   깊이가 크게 다른 오른쪽으로는 새면 안 된다.
        float beyondStep = 0.f;
        for (uint32_t x = kStepX; x < kSSSWidth; ++x)
        {
            beyondStep = (std::max)(beyondStep, finalCapture.At(x, kPointY, 1));
        }

        // 같은 거리만큼 왼쪽(깊이가 같은 쪽)으로는 번져야 한다 — 대조군이
        // 없으면 '아무 데도 안 번져서' 통과하는 것과 구분되지 않는다.
        float towardFlat = 0.f;
        for (uint32_t x = kRightX - (kStepX - kRightX); x < kRightX; ++x)
        {
            towardFlat = (std::max)(towardFlat, finalCapture.At(x, kPointY, 1));
        }

        const double sourceSum = 2.0;   // 점 둘, 각 1.0
        const double resultSum = finalCapture.Sum(0) + finalCapture.Sum(1);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[4/4] 표면 추종 — 계단 너머 %.4f · 같은 면 %.4f · 에너지 %.2f/%.2f\n",
            beyondStep, towardFlat, resultSum, sourceSum);
        outLog += line;

        if (towardFlat < 0.001f)
        {
            outLog += "같은 면으로도 안 번졌다 — 대조군이 성립하지 않는다\n";
            passed = false;
        }
        if (beyondStep > towardFlat * 0.5f)
        {
            outLog += "깊이 계단을 넘어 번졌다 — 표면 추종이 죽었다\n";
            passed = false;
        }

        // ④ 에너지 — 커널 합이 1 근처라 총량이 보존된다. 두 배가 되거나
        //   반으로 줄면 커널이나 누적이 틀린 것이다.
        if (resultSum < sourceSum * 0.5 || resultSum > sourceSum * 2.0)
        {
            outLog += "에너지가 보존되지 않는다 — 커널 가중이나 누적이 틀렸다\n";
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

    sss.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "SSS 패스 검증 통과\n" : "SSS 패스 검증 실패\n";
    return passed;
}

#endif
