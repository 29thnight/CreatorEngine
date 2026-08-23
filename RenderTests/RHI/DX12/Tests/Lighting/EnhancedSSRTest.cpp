#include "Render/Passes/Lighting/EnhancedSSRPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "../DX12TestTextureRegistration.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// SSR 패스 자가 검증 (PHASE 3-6, 미구현 패스 이식 3차).
//
// ── 합성 장면 ──
//
//   정사영으로 두고 화면 전체를 깊이 0.5의 평면으로 채운다. 법선은 반사
//   광선이 화면 오른쪽으로 거의 수평하게, 아주 조금 아래로 파고들도록
//   골랐다 — 그래야 광선이 표면 바로 뒤(0 < 차이 < 두께)로 들어가 적중한다.
//   화면 오른쪽 절반만 초록으로 칠해 두면, 경계 왼쪽의 좁은 띠가 그 초록을
//   반사해 온다. 반사가 도는지를 그 띠로 읽는다.
//
//   물리적으로 그럴듯한 법선이 아니라 광선을 겨누기 위한 법선이다.
//
// ── 넷을 따로 단정한다 ──
//
//   ① 반사 발생 — 경계 왼쪽 띠에 초록이 실려 오는가(기대 0.3).
//   ② 금속 마스크 — 아래 절반은 금속도 0이라 같은 광선인데도 반사가
//      실리면 안 된다. 대조군이 없으면 '아무 데나 초록을 칠하는' 구현도
//      통과한다.
//   ③ 두께 게이트 — 두께를 0으로 두면 반사가 사라지는가.
//   ④ ★ 비트플래그 게이트가 텍셀 (0,0) 하나에 걸린다.
//      screenSize를 DX11이 채우지 않아 모든 픽셀이 (0,0)을 짚는다. 그래서
//      (0,0)에만 플래그를 세우면 전 화면의 반사가 사라지고, (0,0)만 빼고
//      전부 세우면 아무것도 걸리지 않는다. 뒤집힌 이 한 쌍이 곧 증거다.
//
//   그리고 패스 수 2(SSR + 리드백)가 복사가 없다는 증거다 — DX11은 화면
//   크기 복사를 둘 했다.
namespace
{
    constexpr uint32_t kSsrWidth = 256;
    constexpr uint32_t kSsrHeight = 256;

    // 초록 절반이 시작하는 화면 x.
    constexpr uint32_t kSsrGreenStartX = 128;

    // 반사를 읽는 띠. 광선이 한 걸음에 14.6~29.1픽셀 오른쪽을 짚으므로
    // (스텝 0.114 x 잡음 [1,2) x 화면 128픽셀/월드), 116~127에서 출발하면
    // 잡음이 어떤 값이어도 초록 영역에 떨어진다.
    constexpr uint32_t kSsrBandMinX = 116;
    constexpr uint32_t kSsrBandMaxX = 128;

    // 위 절반은 금속도 1, 아래 절반은 0.
    constexpr uint32_t kSsrMetalRowY = 64;
    constexpr uint32_t kSsrPlainRowY = 192;

    uint16_t SsrFloatToHalf(float value)
    {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        const uint32_t sign = (bits >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
        const uint32_t mantissa = bits & 0x7FFFFFu;

        if (exponent <= 0) return static_cast<uint16_t>(sign);
        if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10)
            | (mantissa >> 13));
    }

    /// 띠 안에서 가장 밝은 값. 반사가 실렸는지를 재는 자다.
    ///
    /// 디코드·행 간격은 RHIReadbackImage가 안다(R2c-b2) — 이 검사에 남는 것은
    /// "어디를 어떻게 재는가"뿐이다.
    float SsrBandMax(const RHIReadbackImage& image, uint32_t y, uint32_t channel)
    {
        float peak = 0.f;
        for (uint32_t x = kSsrBandMinX; x < kSsrBandMaxX; ++x)
            peak = (std::max)(peak, image.At(x, y, channel));
        return peak;
    }
}

bool DX12Test::RunSSRTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── SSR 패스 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kSsrWidth, kSsrHeight, error))
    {
        outLog += "[1/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_ssr.cache", error) ||
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
    frameContext.width = kSsrWidth;
    frameContext.height = kSsrHeight;

    // 정사영이라 화면 좌표에서 월드 좌표가 선형으로 나온다 — 광선이 몇
    // 픽셀을 건너뛰는지 손으로 계산할 수 있다. 카메라를 멀리 두어 시선이
    // 화면 전체에서 (0,0,1)에 가깝게 만든다(정사영과 앞뒤가 맞는다).
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixOrthographicLH(2.f, 2.f, 0.f, 10.f);
    camera.inverseView = XMMatrixInverse(nullptr, camera.view);
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
    camera.eyePosition = XMVectorSet(0.f, 0.f, -1000.f, 1.f);
    camera.fov = DirectX::XM_PIDIV4;
    frameContext.camera = &camera;

    EnhancedSSRPass ssr;
    if (!ssr.Initialize(frameContext, error))
    {
        outLog += "[1/5] SSR 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    ssr.SetTime(0.f);   // 잡음을 프레임에 묶지 않는다 — 재현되어야 한다
    outLog += "[1/5] 셰이더 컴파일·PSO 생성 통과\n";

    // ── 합성 입력 ──
    ComPtr<ID3D12Resource> colorSource;
    ComPtr<ID3D12Resource> depthSource;
    ComPtr<ID3D12Resource> metalRoughSource;
    ComPtr<ID3D12Resource> normalSource;
    ComPtr<ID3D12Resource> bitmaskZero;
    ComPtr<ID3D12Resource> bitmaskCorner;
    ComPtr<ID3D12Resource> bitmaskExceptCorner;
    DX12TestTextureRegistration colorRegistration;
    DX12TestTextureRegistration depthRegistration;
    DX12TestTextureRegistration metalRoughRegistration;
    DX12TestTextureRegistration normalRegistration;
    DX12TestTextureRegistration bitmaskZeroRegistration;
    DX12TestTextureRegistration bitmaskCornerRegistration;
    DX12TestTextureRegistration bitmaskExceptCornerRegistration;

    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        const auto makeTexture = [&](DXGI_FORMAT format, ComPtr<ID3D12Resource>& out) -> bool
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = kSsrWidth;
            desc.Height = kSsrHeight;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;

            return SUCCEEDED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&out)));
        };

        if (!makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, colorSource) ||
            !makeTexture(DXGI_FORMAT_R32_FLOAT, depthSource) ||
            !makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, metalRoughSource) ||
            !makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, normalSource) ||
            !makeTexture(DXGI_FORMAT_R32_UINT, bitmaskZero) ||
            !makeTexture(DXGI_FORMAT_R32_UINT, bitmaskCorner) ||
            !makeTexture(DXGI_FORMAT_R32_UINT, bitmaskExceptCorner))
        {
            outLog += "[2/5] 입력 텍스처 생성 실패\n";
            resources.Shutdown();
            return false;
        }

        colorRegistration.Register(resources, colorSource.Get());
        depthRegistration.Register(resources, depthSource.Get());
        metalRoughRegistration.Register(resources, metalRoughSource.Get());
        normalRegistration.Register(resources, normalSource.Get());
        bitmaskZeroRegistration.Register(resources, bitmaskZero.Get());
        bitmaskCornerRegistration.Register(resources, bitmaskCorner.Get());
        bitmaskExceptCornerRegistration.Register(resources, bitmaskExceptCorner.Get());
        if (!colorRegistration.IsValid() || !depthRegistration.IsValid() ||
            !metalRoughRegistration.IsValid() || !normalRegistration.IsValid() ||
            !bitmaskZeroRegistration.IsValid() || !bitmaskCornerRegistration.IsValid() ||
            !bitmaskExceptCornerRegistration.IsValid())
        {
            outLog += "[2/5] 입력 텍스처 핸들 등록 실패\n";
            resources.Shutdown();
            return false;
        }

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/5] BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // 반정밀도 4채널 한 장을 픽셀 함수로 채운다.
        const auto uploadHalf4 = [&](ID3D12Resource* target,
            const std::function<void(uint32_t, uint32_t, float(&)[4])>& fill) -> bool
        {
            constexpr uint32_t pitch = kSsrWidth * 8;
            const auto upload = resources.AllocateUpload(
                RHIUploadRequest{ static_cast<uint64_t>(pitch) * kSsrHeight,
                    RHIUploadUsage::TextureCopy, 1 });
            if (!upload.IsValid()) return false;

            auto* base = static_cast<uint8_t*>(upload.cpuAddress);
            for (uint32_t y = 0; y < kSsrHeight; ++y)
            {
                auto* row = reinterpret_cast<uint16_t*>(base + static_cast<size_t>(y) * pitch);
                for (uint32_t x = 0; x < kSsrWidth; ++x)
                {
                    float rgba[4]{};
                    fill(x, y, rgba);
                    for (uint32_t c = 0; c < 4; ++c)
                        row[x * 4 + c] = SsrFloatToHalf(rgba[c]);
                }
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = resources.Resolve(upload.buffer);
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = upload.offset;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            src.PlacedFootprint.Footprint.Width = kSsrWidth;
            src.PlacedFootprint.Footprint.Height = kSsrHeight;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = pitch;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = target;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            return true;
        };

        // 32비트 한 채널(깊이 float / 비트마스크 uint) 한 장.
        const auto uploadRaw32 = [&](ID3D12Resource* target, DXGI_FORMAT format,
            const std::function<uint32_t(uint32_t, uint32_t)>& fill) -> bool
        {
            constexpr uint32_t pitch = kSsrWidth * 4;
            const auto upload = resources.AllocateUpload(
                RHIUploadRequest{ static_cast<uint64_t>(pitch) * kSsrHeight,
                    RHIUploadUsage::TextureCopy, 1 });
            if (!upload.IsValid()) return false;

            auto* base = static_cast<uint8_t*>(upload.cpuAddress);
            for (uint32_t y = 0; y < kSsrHeight; ++y)
            {
                auto* row = reinterpret_cast<uint32_t*>(base + static_cast<size_t>(y) * pitch);
                for (uint32_t x = 0; x < kSsrWidth; ++x) row[x] = fill(x, y);
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = resources.Resolve(upload.buffer);
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = upload.offset;
            src.PlacedFootprint.Footprint.Format = format;
            src.PlacedFootprint.Footprint.Width = kSsrWidth;
            src.PlacedFootprint.Footprint.Height = kSsrHeight;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = pitch;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = target;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            return true;
        };

        const float half = 0.5f;
        (void)half;

        // 색 — 왼쪽 절반 검정, 오른쪽 절반 초록. 반사해 올 원본이다.
        bool ok = uploadHalf4(colorSource.Get(), [](uint32_t x, uint32_t, float (&rgba)[4])
        {
            const bool green = (x >= kSsrGreenStartX);
            rgba[0] = 0.f;
            rgba[1] = green ? 1.f : 0.f;
            rgba[2] = 0.f;
            rgba[3] = 1.f;
        });

        // 금속거칠기 — .r이 금속도(위 절반 1 · 아래 절반 0), .g가 거칠기.
        // GBuffer 배치가 (metallic, roughness, occlusion, ior)다.
        ok = ok && uploadHalf4(metalRoughSource.Get(), [](uint32_t, uint32_t y, float (&rgba)[4])
        {
            rgba[0] = (y < kSsrHeight / 2) ? 1.f : 0.f;
            rgba[1] = 0.5f;
            rgba[2] = 0.f;
            rgba[3] = 1.5f;
        });

        // 노멀 — 반사 광선을 오른쪽으로 거의 수평하게, 아주 조금 아래로
        // 보내는 값. N = (-0.7247, 0, 0.6890)을 인코딩했다.
        // reflect((0,0,1), N) = (0.9986, 0, 0.0506)이 된다.
        ok = ok && uploadHalf4(normalSource.Get(), [](uint32_t, uint32_t, float (&rgba)[4])
        {
            rgba[0] = -0.7247f * 0.5f + 0.5f;
            rgba[1] = 0.f * 0.5f + 0.5f;
            rgba[2] = 0.6890f * 0.5f + 0.5f;
            rgba[3] = 1.f;
        });

        // 깊이 — 화면 전체가 평면(0.5). 월드 z로는 5다.
        ok = ok && uploadRaw32(depthSource.Get(), DXGI_FORMAT_R32_FLOAT,
            [](uint32_t, uint32_t) { float d = 0.5f; uint32_t bits; memcpy(&bits, &d, 4); return bits; });

        // 비트마스크 셋 — 전부 0 / (0,0)만 / (0,0)만 빼고 전부.
        constexpr uint32_t kTerrainBit = 1u << 9;
        ok = ok && uploadRaw32(bitmaskZero.Get(), DXGI_FORMAT_R32_UINT,
            [](uint32_t, uint32_t) { return 0u; });
        ok = ok && uploadRaw32(bitmaskCorner.Get(), DXGI_FORMAT_R32_UINT,
            [](uint32_t x, uint32_t y) { return (0 == x && 0 == y) ? kTerrainBit : 0u; });
        ok = ok && uploadRaw32(bitmaskExceptCorner.Get(), DXGI_FORMAT_R32_UINT,
            [](uint32_t x, uint32_t y) { return (0 == x && 0 == y) ? 0u : kTerrainBit; });

        if (!ok)
        {
            outLog += "[2/5] 입력 업로드 실패\n";
            resources.Shutdown();
            return false;
        }

        ID3D12Resource* const sources[7] = { colorSource.Get(), depthSource.Get(),
            metalRoughSource.Get(), normalSource.Get(), bitmaskZero.Get(),
            bitmaskCorner.Get(), bitmaskExceptCorner.Get() };
        D3D12_RESOURCE_BARRIER barriers[7]{};
        for (uint32_t i = 0; i < 7; ++i)
        {
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource = sources[i];
            barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        resources.GetCommandList()->ResourceBarrier(7, barriers);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/5] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
        outLog += "[2/5] 합성 장면(평면 + 초록 절반 + 금속 상하 분할) 업로드 완료\n";
    }

    RHIReadback readback{};
    {
        std::string readbackError;
        if (!resources.CreateReadback(kSsrWidth, kSsrHeight,
            EnhancedSSRPass::kOutputFormat, 1, readback, readbackError))
        {
            outLog += "[2/5] 리드백 생성 실패: " + readbackError + "\n";
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    uint32_t lastPassCount = 0;

    // 설정 하나를 그려 결과를 돌려준다. 그래프는 매번 새로 만든다 —
    // 재사용하면 앞 프레임의 상태 추적이 섞인다.
    const auto renderOnce = [&](const EnhancedSSRPass::Tuning& tuning,
        RHITextureHandle bitmask, RHIReadbackImage& outCapture) -> bool
    {
        if (!resources.BeginFrame(error)) return false;

        ssr.SetTuning(tuning);
        ssr.SetEnabled(true);
        if (!ssr.PrepareFrame(frameContext, error)) return false;

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);

        EnhancedSSRPass::Inputs inputs{};
        inputs.color = graph.ImportTexture(colorRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSR.Color");
        inputs.depth = graph.ImportTexture(depthRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSR.Depth");
        inputs.metalRough = graph.ImportTexture(metalRoughRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSR.MetalRough");
        inputs.normal = graph.ImportTexture(normalRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSR.Normal");
        inputs.bitmask = graph.ImportTexture(bitmask,
            RHIResourceState::ShaderResource, "SSR.Bitmask");

        ssr.SetInputs(inputs);
        ssr.Declare(graph, frameContext);

        const RGHandle output = ssr.GetOutput();
        if (!output.IsValid()) return false;

        graph.AddPass("SSR.Readback", { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(error)) return false;
        if (!graph.Execute(error)) return false;
        lastPassCount = graph.GetStats().passesExecuted;

        if (!resources.EndFrame(error)) return false;
        resources.WaitForGpu();

        return resources.MapReadback(readback, outCapture, error);
    };

    const EnhancedSSRPass::Tuning defaults{};

    // ── [3/5] 반사 발생과 금속 마스크 ──
    RHIReadbackImage base{};
        if (!renderOnce(defaults, bitmaskZeroRegistration.Handle(), base))
    {
        outLog += "[3/5] 기준 렌더 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    {
        const float metalBand = SsrBandMax(base, kSsrMetalRowY, 1);
        const float plainBand = SsrBandMax(base, kSsrPlainRowY, 1);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/5] 반사 — 금속 띠 %.4f (기대 0.30) · 비금속 띠 %.4f (기대 0)"
            " · 그래프 %u패스\n",
            metalBand, plainBand, lastPassCount);
        outLog += line;

        // ① 반사가 실제로 실려 오는가. 가중치 a가 0.3으로 고정이라
        //   초록 1.0을 반사하면 0.3이 나온다.
        if (metalBand < 0.2f)
        {
            outLog += "금속 띠에 반사가 없다 — 광선 행진이나 적중 판정이 죽었다\n";
            passed = false;
        }
        // ② 금속도 0인 아래 절반은 같은 광선인데도 실리면 안 된다.
        //   이 대조군이 없으면 '아무 데나 칠하는' 구현도 통과한다.
        if (plainBand > 0.01f)
        {
            outLog += "비금속에도 반사가 실렸다 — 금속 마스크가 죽었다\n";
            passed = false;
        }
        // 복사가 없어야 둘(SSR + 리드백)이다. DX11은 화면 크기 복사를 둘 했다.
        if (2 != lastPassCount)
        {
            outLog += "패스가 둘(SSR·리드백)이 아니다 — 복사가 남아 있다\n";
            passed = false;
        }
    }

    // ── [4/5] 두께 게이트 ──
    if (passed)
    {
        EnhancedSSRPass::Tuning noThickness = defaults;
        noThickness.maxThickness = 0.f;

        RHIReadbackImage thin{};
        if (!renderOnce(noThickness, bitmaskZeroRegistration.Handle(), thin))
        {
            outLog += "[4/5] 두께 0 렌더 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        const float band = SsrBandMax(thin, kSsrMetalRowY, 1);

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[4/5] 두께 게이트 — 두께 0일 때 금속 띠 %.4f (기대 0)\n", band);
        outLog += line;

        // ③ 두께를 0으로 두면 적중 조건이 성립할 수 없다.
        if (band > 0.01f)
        {
            outLog += "두께 0인데 반사가 남았다 — 적중 조건이 두께를 안 본다\n";
            passed = false;
        }
    }

    // ── [5/5] 비트플래그 게이트가 텍셀 (0,0) 하나에 걸린다 ──
    if (passed)
    {
        RHIReadbackImage corner{};
        RHIReadbackImage exceptCorner{};
        if (!renderOnce(defaults, bitmaskCornerRegistration.Handle(), corner) ||
            !renderOnce(defaults, bitmaskExceptCornerRegistration.Handle(), exceptCorner))
        {
            outLog += "[5/5] 비트플래그 렌더 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        const float cornerBand = SsrBandMax(corner, kSsrMetalRowY, 1);
        const float exceptBand = SsrBandMax(exceptCorner, kSsrMetalRowY, 1);

        // 꺼 두면 입력을 그대로 흘리는가 — 뒤 패스가 분기 없이 이어지는 근거다.
        ssr.SetEnabled(false);
        EnhancedRenderGraph offGraph(resources);
        EnhancedSSRPass::Inputs offInputs{};
        offInputs.color = offGraph.ImportTexture(colorRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSR.OffColor");
        ssr.SetInputs(offInputs);
        ssr.Declare(offGraph, frameContext);
        const bool passThrough = (ssr.GetOutput().index == offInputs.color.index);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[5/5] 비트플래그 — (0,0)만 세움: %.4f (기대 0) · (0,0)만 빼고 전부:"
            " %.4f (기대 0.30) · 꺼짐 통과 %s\n",
            cornerBand, exceptBand, passThrough ? "예" : "아니오");
        outLog += line;

        // ④ screenSize가 (0,0)이라 모든 픽셀이 텍셀 (0,0)을 짚는다.
        //   그래서 한 점만 세워도 전 화면이 걸리고,
        if (cornerBand > 0.01f)
        {
            outLog += "(0,0)만 세웠는데 반사가 남았다 — 게이트가 (0,0)을 안 본다\n";
            passed = false;
        }
        //   그 한 점만 빼면 나머지가 다 세워져 있어도 아무것도 안 걸린다.
        //   이 뒤집힘이 곧 screenSize가 채워지지 않았다는 증거다.
        if (exceptBand < 0.2f)
        {
            outLog += "(0,0)만 비웠는데 반사가 사라졌다 — 게이트가 픽셀별로 돈다"
                "(원본과 다르다)\n";
            passed = false;
        }
        if (!passThrough)
        {
            outLog += "꺼져 있는데 입력을 그대로 흘리지 않는다\n";
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

    ssr.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    bitmaskExceptCornerRegistration.Reset();
    bitmaskCornerRegistration.Reset();
    bitmaskZeroRegistration.Reset();
    normalRegistration.Reset();
    metalRoughRegistration.Reset();
    depthRegistration.Reset();
    colorRegistration.Reset();
    resources.Shutdown();

    outLog += passed ? "SSR 패스 검증 통과\n" : "SSR 패스 검증 실패\n";
    return passed;
}
