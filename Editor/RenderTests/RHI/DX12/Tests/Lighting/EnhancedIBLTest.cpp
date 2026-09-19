#include "RHI/DX12/EnhancedIBLGenerator.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// IBL 생성 체인 자가 검증 (PHASE 3-6).
//
// 반구가 갈리는 equirect(위 빨강 16 · 아래 초록 1)를 넣고 넷을 따로 단정한다:
//
//   ① rect→cube — +Y 면이 빨강, -Y 면이 초록인가(구면 매핑의 방향)
//   ② 조도       — 방향(우세)과 **에너지**(절대값). 반구가 각각 균일하므로
//      참값이 해석적으로 나온다: +Y (16, 0) · -Y (0, 1) · +X (8, 0.5).
//      위/아래를 16:1 로 벌린 것이 이 게이트의 이빨이다 — 균일 픽스처에서는
//      적분이 에너지를 흘려도 우세·대칭이 그대로라 내내 초록이었다.
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

    // ★ 위 반구를 16배 밝게 둔다 — 적분이 에너지를 지키는지 보려면 픽스처에
    //   **대비**가 있어야 한다.
    //
    //   예전 픽스처는 위/아래가 같은 밝기였고, 그 균일한 표본 집합에서는
    //   누적값을 눌러 잡음을 잡는 억제(톤맵 공간 평균)가 정확히 항등이다.
    //   그래서 그 억제가 태양 있는 HDRI 에서 조도를 참값의 0.53 배까지
    //   깎는 동안에도 이 게이트는 내내 초록이었다. 16:1 로 벌리면 같은
    //   억제가 +X 조도를 8.0 대신 4.49 로 만들어 단정에 걸린다.
    constexpr uint16_t kIblHalfSixteen = 0x4C00;
    constexpr float    kIblUpperRadiance = 16.f;
    constexpr float    kIblLowerRadiance = 1.f;

    // 구획 열의 자리(장 번호). 배치는 등차라 장 하나가 곧 구획 하나다.
    //   0 큐브+Y · 1 큐브-Y · 2 조도+Y · 3 조도-Y · 4 조도+X
    //   5 프리필터 밉0+Y · 6 밉5+Y · 7 밉0-Y · 8 밉5-Y · 9 LUT
    constexpr uint32_t kIblRegionCount = 10;
}

namespace
{
    bool RunIblGgxRegression(DX12DeviceResources& resources,
        const EnhancedFrameContext& context, std::string& outLog)
    {
        std::string error;
        RHIShaderBlob shader;
        if (!RHIShaderCompiler::CompileFile("Tests/IblGgxProbe.slang", "CSMain", "cs_5_0", shader, error))
        { outLog += error; return false; }
        const RHIPipelineLayoutParam params[] = { RHILayout::UavTable(1, 0) };
        RHIPipelineLayoutDesc layoutDesc{}; layoutDesc.params = params;
        const auto layout = context.rootSignatures->GetOrCreate(layoutDesc, error);
        RHIComputePipelineDesc pipelineDesc{};
        pipelineDesc.csBytecode = shader.Data(); pipelineDesc.csSize = shader.Size();
        pipelineDesc.layout = layout;
        const auto pipeline = context.psoManager->GetOrCreateCompute(pipelineDesc, error);
        RHITextureDesc desc{};
        desc.width = 4; desc.height = 1; desc.format = RHIFormat::RGBA32Float;
        desc.allowUnorderedAccess = true; desc.initialState = RHIResourceState::UnorderedAccess;
        RHITextureHandle output;
        RHIReadback readback;
        if (!layout.IsValid() || !pipeline.IsValid() || !resources.CreateTexture(desc, output, error) ||
            !resources.CreateReadback(4, 1, desc.format, 1, readback, error) ||
            !resources.BeginFrame(error)) { outLog += error; return false; }
        const RHIBindingDesc view[] = { RHIBindingDesc::Uav2D(output, desc.format) };
        const auto bindings = resources.CreateBindings(view);
        auto& encoder = resources.GetImmediateEncoder();
        encoder.SetPipeline(RHIBindPoint::Compute, pipeline);
        encoder.SetBindings(RHIBindPoint::Compute, 0, bindings);
        encoder.Dispatch(1,1,1);
        const RHITransition transition[] = {
            { output, RHIResourceState::UnorderedAccess, RHIResourceState::CopySource } };
        resources.TransitionResources(transition);
        encoder.CopyToReadback(readback, output);
        if (!resources.EndFrame(error)) { outLog += error; return false; }
        resources.WaitForGpu();
        RHIReadbackImage image;
        if (!resources.MapReadback(readback, image, error)) { outLog += error; return false; }
        const double roughness[] = { .032, .05, .1, .2 };
        double maxError = 0.0;
        for (uint32_t x = 0; x < 4; ++x)
            for (uint32_t c = 0; c < 2; ++c)
            {
                const double a2 = std::pow(roughness[x],4.0);
                const double nh = c == 0 ? 1.0 : .99;
                const double d = 1.0 - nh*nh + nh*nh*a2;
                const double expected = a2 / (3.141592653589793 * d*d);
                const double actual = image.At(x,0,c);
                if (!std::isfinite(actual)) { resources.ReleaseTexture(output); return false; }
                maxError = (std::max)(maxError,std::abs(actual-expected)/expected);
            }
        resources.ReleaseTexture(output);
        outLog += "GGX peak/off-peak relative error: " + std::to_string(maxError) + "\n";
        return maxError < 0.0001;
    }

    // Independent solid-angle quadrature, not the shader's Hammersley sequence.
    // A small 60000:0.125 emitter exposes coherent sampling error and mip0 blur.
    bool RunIblHdrRegression(DX12DeviceResources& resources,
        const EnhancedFrameContext& context, EnhancedIBLGenerator& generator,
        std::string& outLog)
    {
        constexpr uint32_t cubeSize = 256, grid = 128, width = 1024, height = 512;
        constexpr uint32_t cubeMips = EnhancedIBLGenerator::CubeMipCount(cubeSize);
        constexpr uint32_t prefilterMips = EnhancedIBLGenerator::kPrefilterMips;
        std::string error;
        RHITextureHandle source;
        RHITextureDesc desc{};
        desc.width = width; desc.height = height;
        desc.format = RHIFormat::RGBA16Float;
        desc.initialState = RHIResourceState::CopyDest;
        desc.debugName = L"IBL.HdrRegression";
        if (!resources.CreateTexture(desc, source, error)) { outLog += error; return false; }
        const auto fail = [&]() { outLog += "HDR regression failed: " + error + "\n";
            resources.WaitForGpu(); resources.ReleaseTexture(source); return false; };
        if (!resources.BeginFrame(error)) return fail();
        const auto upload = resources.AllocateUpload(
            RHIUploadRequest{ width * height * 8, RHIUploadUsage::TextureCopy, 1 });
        if (!upload.IsValid()) { error = "HDR upload allocation"; return fail(); }
        auto* texels = static_cast<uint16_t*>(upload.cpuAddress);
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
            {
                const bool sun = x >= 620 && x < 626 && y >= 200 && y < 206;
                const uint16_t value = sun ? 0x7b53 : 0x3000; // 60000, 0.125
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
                texels[offset] = texels[offset + 1] = texels[offset + 2] = value;
                texels[offset + 3] = 0x3c00;
            }
        D3D12_TEXTURE_COPY_LOCATION from{};
        from.pResource = resources.Resolve(upload.buffer);
        from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        from.PlacedFootprint.Offset = upload.offset;
        from.PlacedFootprint.Footprint = { DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, width * 8 };
        D3D12_TEXTURE_COPY_LOCATION to{};
        to.pResource = resources.Resolve(source);
        to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        resources.GetCommandList()->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        const RHITransition inputReady[] = { { source, RHIResourceState::CopyDest,
            RHIResourceState::PixelShaderResource } };
        resources.TransitionResources(inputReady);
        if (!generator.Generate(context, source, RHIFormat::RGBA16Float, cubeSize, 32, error) ||
            !resources.EndFrame(error)) return fail();
        resources.WaitForGpu();

        RHIReadback maps{}, mirrors{};
        if (!resources.CreateReadback(grid, grid, RHIFormat::RGBA16Float, 30, maps, error) ||
            !resources.CreateReadback(cubeSize, cubeSize, RHIFormat::RGBA16Float, 12, mirrors, error) ||
            !resources.BeginFrame(error)) return fail();
        const RHITransition copyReady[] = {
            { generator.GetCubeMap(), RHIResourceState::PixelShaderResource, RHIResourceState::CopySource },
            { generator.GetIrradianceMap(), RHIResourceState::PixelShaderResource, RHIResourceState::CopySource },
            { generator.GetPrefilteredMap(), RHIResourceState::PixelShaderResource, RHIResourceState::CopySource },
        };
        resources.TransitionResources(copyReady);
        auto& encoder = resources.GetImmediateEncoder();
        const uint32_t roughMips[] = { 1, 3, 5 };
        for (uint32_t face = 0; face < 6; ++face)
        {
            encoder.CopyToReadback(maps, generator.GetCubeMap(), face, face * cubeMips + 1);
            encoder.CopyToReadback(maps, generator.GetIrradianceMap(), 6 + face, face);
            encoder.CopyToReadback(mirrors, generator.GetCubeMap(), face, face * cubeMips);
            encoder.CopyToReadback(mirrors, generator.GetPrefilteredMap(), 6 + face, face * prefilterMips);
            for (uint32_t r = 0; r < 3; ++r)
                encoder.CopyToReadback(maps, generator.GetPrefilteredMap(), 12 + r * 6 + face,
                    face * prefilterMips + roughMips[r]);
        }
        if (!resources.EndFrame(error)) return fail();
        resources.WaitForGpu();
        RHIReadbackImage image{}, mirror{};
        if (!resources.MapReadback(maps, image, error) ||
            !resources.MapReadback(mirrors, mirror, error)) return fail();
        double mirrorError = 0.0, peak = 0.0;
        for (uint32_t f = 0; f < 6; ++f)
            for (uint32_t y = 0; y < cubeSize; ++y)
                for (uint32_t x = 0; x < cubeSize; ++x)
                {
                    const double value = mirror.At(x, y, 0, f);
                    const double reflected = mirror.At(x, y, 0, f + 6);
                    if (!std::isfinite(value) || !std::isfinite(reflected))
                    { error = "non-finite mirror"; return fail(); }
                    peak = (std::max)(peak, value);
                    mirrorError = (std::max)(mirrorError, std::abs(value - reflected) / (1.0 + value));
                }
        if (peak < 10000.0 || mirrorError > 0.01)
        { error = "HDR mirror lost source detail"; return fail(); }

        struct Direction { double x, y, z; };
        const auto direction = [](uint32_t face, double u, double v)
        {
            Direction d;
            switch (face)
            {
            case 0: d = { 1, -v, -u }; break;
            case 1: d = { -1, -v, u }; break;
            case 2: d = { u, 1, v }; break;
            case 3: d = { u, -1, -v }; break;
            case 4: d = { u, -v, 1 }; break;
            default: d = { -u, -v, -1 }; break;
            }
            const double length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            return Direction{ d.x / length, d.y / length, d.z / length };
        };
        const auto area = [](double u, double v)
        { return std::atan2(u * v, std::sqrt(u * u + v * v + 1.0)); };
        struct Cell { Direction d; double radiance, solidAngle; };
        std::vector<Cell> cells;
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t y = 0; y < grid; ++y)
                for (uint32_t x = 0; x < grid; ++x)
                {
                    const double u = (x + 0.5) * 2.0 / grid - 1.0;
                    const double v = (y + 0.5) * 2.0 / grid - 1.0;
                    const double h = 1.0 / grid;
                    const double omega = area(u+h,v+h)-area(u-h,v+h)-area(u+h,v-h)+area(u-h,v-h);
                    cells.push_back({ direction(face,u,v), image.At(x,y,0,face), omega });
                }
        bool passed = true;
        for (uint32_t test = 0; test < 4; ++test)
        {
            const uint32_t size = test == 0 ? generator.GetIrradianceSize() : cubeSize >> roughMips[test-1];
            const double roughness = test == 0 ? 1.0 : roughMips[test-1] / 5.0;
            const double a2 = std::pow(roughness, 4.0);
            double error2 = 0.0, reference2 = 0.0, maxError = 0.0, maxReference = 0.0;
            for (uint32_t face = 0; face < 6; ++face)
                for (uint32_t iy = 0; iy < 4; ++iy)
                    for (uint32_t ix = 0; ix < 4; ++ix)
                    {
                        const uint32_t x = (2 * ix + 1) * size / 8;
                        const uint32_t y = (2 * iy + 1) * size / 8;
                        const Direction n = direction(face,(x+0.5)*2.0/size-1.0,(y+0.5)*2.0/size-1.0);
                        double sum = 0.0, weight = 0.0;
                        for (const Cell& cell : cells)
                        {
                            const double nl = n.x * cell.d.x + n.y * cell.d.y + n.z * cell.d.z;
                            if (nl <= 0.0) continue;
                            double w = nl * cell.solidAngle / 3.141592653589793;
                            if (test != 0)
                            {
                                const double h2 = 0.5 * (1.0 + nl);
                                const double denominator = 1.0 - h2 + h2 * a2;
                                w *= a2 / (4.0 * denominator * denominator);
                            }
                            sum += cell.radiance * w;
                            weight += w;
                        }
                        const double expected = test == 0 ? sum : sum / weight;
                        const double actual = image.At(x,y,0,6 + test*6 + face);
                        if (!std::isfinite(actual)) { error = "non-finite convolution"; return fail(); }
                        const double delta = std::abs(actual - expected);
                        error2 += delta * delta; reference2 += expected * expected;
                        maxError = (std::max)(maxError, delta);
                        maxReference = (std::max)(maxReference, expected);
                    }
            const double relativeRms = std::sqrt(error2 / reference2);
            const double relativeMax = maxError / maxReference;
            char line[192]{};
            std::snprintf(line, sizeof(line), "HDR quadrature %u rough=%.1f: relative RMS %.6f, max/peak %.6f\n",
                test, roughness, relativeRms, relativeMax);
            outLog += line;
            passed &= relativeRms < 0.03 && relativeMax < 0.05;
        }
        outLog += "HDR mirror relative maximum error: " + std::to_string(mirrorError) + "\n";
        RHITextureHandle black;
        RHITextureDesc blackDesc{};
        blackDesc.width = blackDesc.height = 1;
        blackDesc.format = RHIFormat::RGBA16Float;
        blackDesc.allowRenderTarget = true;
        blackDesc.initialState = RHIResourceState::RenderTarget;
        if (!resources.CreateTexture(blackDesc, black, error) || !resources.BeginFrame(error)) return fail();
        const RHITextureHandle colors[] = { black };
        const auto target = resources.CreateRenderTargets(colors);
        const float zero[] = {0.f,0.f,0.f,0.f};
        resources.GetImmediateEncoder().ClearRenderTargets(target, zero);
        const RHITransition blackReady[] = { { black, RHIResourceState::RenderTarget,
            RHIResourceState::PixelShaderResource } };
        resources.TransitionResources(blackReady);
        if (!generator.Generate(context, black, blackDesc.format, 64, 32, error)) return fail();
        const RHITransition blackCopy[] = {
            { generator.GetIrradianceMap(), RHIResourceState::PixelShaderResource, RHIResourceState::CopySource },
            { generator.GetPrefilteredMap(), RHIResourceState::PixelShaderResource, RHIResourceState::CopySource } };
        resources.TransitionResources(blackCopy);
        for (uint32_t f=0; f<6; ++f)
        {
            resources.GetImmediateEncoder().CopyToReadback(maps, generator.GetIrradianceMap(), f, f);
            resources.GetImmediateEncoder().CopyToReadback(maps, generator.GetPrefilteredMap(), f+6, f*prefilterMips);
            resources.GetImmediateEncoder().CopyToReadback(maps, generator.GetPrefilteredMap(), f+12, f*prefilterMips+3);
            resources.GetImmediateEncoder().CopyToReadback(maps, generator.GetPrefilteredMap(), f+18, f*prefilterMips+5);
        }
        if (!resources.EndFrame(error)) return fail();
        resources.WaitForGpu();
        if (!resources.MapReadback(maps, image, error)) return fail();
        for (uint32_t slice=0; slice<24; ++slice)
        {
            const uint32_t size = slice < 12 ? 64 : slice < 18 ? 8 : 2;
            for (uint32_t y=0; y<size; ++y)
                for (uint32_t x=0; x<size; ++x)
                    for (uint32_t c=0; c<3; ++c)
                        passed &= image.At(x,y,c,slice) == 0.f; // NaN also fails.
        }
        resources.ReleaseTexture(black);
        outLog += "Black environment finite zero: " + std::string(passed ? "PASS\n" : "FAIL\n");
        resources.ReleaseTexture(source);
        return passed;
    }
}

bool DX12Test::RunIBLTest(std::string& outLog)
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
    if (!generator.Initialize(frameContext, error))
    {
        outLog += "[1/5] IBL 생성기 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/5] IBL 셰이더 컴파일·PSO 생성 통과\n";

    // ── [2/5] 합성 equirect — 위 반구 빨강 · 아래 반구 초록 ──
    //
    // 구면 매핑에서 v=0이 +Y(천정)다. 반구가 갈려 있으면 조도·프리필터의
    // 방향 적분이 '어느 반구를 봤는가'로 검증된다.
    ComPtr<ID3D12Resource> equirect;
    RHITextureHandle equirectHandle;
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
        equirectHandle = resources.RegisterExternalTexture(equirect.Get());
        if (!equirectHandle.IsValid())
        {
            outLog += "[2/5] equirect 핸들 등록 실패\n";
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
            RHIUploadRequest{ kRowPitch * kIblEquirectHeight,
                RHIUploadUsage::TextureCopy, 1 });
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
                row[x * 4 + 0] = top ? kIblHalfSixteen : 0;
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
        if (!generator.Generate(frameContext, equirectHandle,
            RHIFormat::RGBA16Float, kIblCubeSize, kIblBrdfSize, error))
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
            resources.GetImmediateEncoder().CopyToReadback(
                readback, source, region, subresource);
        };

        // 면 인덱스: +X 0 · -X 1 · +Y 2 · -Y 3. 서브리소스 = 밉 + 면 x 밉수.
        //
        // ★ 환경 큐브도 밉 체인을 갖는다(조도·프리필터가 표본 입체각에 맞는
        //   밉을 읽는다). 면 인덱스를 그대로 서브리소스로 쓰면 엉뚱한 밉을
        //   뜬다 — 밉수를 곱해야 한다.
        constexpr uint32_t kMips = EnhancedIBLGenerator::kPrefilterMips;
        constexpr uint32_t kCubeMips = EnhancedIBLGenerator::CubeMipCount(kIblCubeSize);
        copyRegion(generator.GetCubeMap(), 0 + 2 * kCubeMips, 0);
        copyRegion(generator.GetCubeMap(), 0 + 3 * kCubeMips, 1);
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

        if (upR < kIblUpperRadiance * 0.9f || upG > 0.1f ||
            downG < kIblLowerRadiance * 0.9f || downR > 0.1f)
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

        // ── 방향 ──
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

        // ── 에너지 ──
        //
        // 픽스처가 위/아래 반구 각각 균일하므로 참값이 해석적으로 나온다.
        // 조도는 E/PI, 즉 코사인 가중 표본의 산술 평균이다:
        //   +Y 는 위 반구만 보므로 (16, 0) · -Y 는 아래만 보므로 (0, 1)
        //   +X 는 반반이라 (8, 0.5)
        //
        // ★ 이 단정이 이 게이트의 이빨이다. 절대값을 보지 않던 시절에는
        //   적분이 에너지를 절반 가까이 흘려도 우세·대칭만으로 초록이었다.
        //   누적값을 눌러 잡음을 잡는 억제가 돌아오면 +X 의 R 이 8.0 이
        //   아니라 4.49 로 내려앉아 여기서 잡힌다.
        const auto nearValue = [&](float measured, float expected, float tolerance)
        {
            return std::fabs(measured - expected) <= expected * tolerance;
        };
        constexpr float kEnergyTolerance = 0.12f;

        if (!nearValue(irrUpR, kIblUpperRadiance, kEnergyTolerance) ||
            !nearValue(irrDownG, kIblLowerRadiance, kEnergyTolerance))
        {
            outLog += "극 조도가 반구 라디언스와 다르다 — 적분이 에너지를 잃거나 더한다\n";
            passed = false;
        }
        if (!nearValue(irrSideR, kIblUpperRadiance * 0.5f, kEnergyTolerance) ||
            !nearValue(irrSideG, kIblLowerRadiance * 0.5f, kEnergyTolerance))
        {
            outLog += "+X 조도가 두 반구의 반반이 아니다 — 접선 기저가 기울었거나"
                      " 적분이 밝은 쪽 에너지를 흘린다\n";
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

    if (passed) passed = RunIblGgxRegression(resources, frameContext, outLog);
    if (passed) passed = RunIblHdrRegression(resources, frameContext, generator, outLog);

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    generator.Shutdown();
    resources.ReleaseTexture(equirectHandle);
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "IBL 생성 체인 검증 통과\n" : "IBL 생성 체인 검증 실패\n";
    return passed;
}
