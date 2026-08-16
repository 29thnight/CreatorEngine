#ifndef DYNAMICCPP_EXPORTS
#include "../VulkanSelfTest.h"
#include "../VulkanDeviceResources.h"
#include "../VulkanPipelineCache.h"
#include "../../RHIShaderCompiler.h"
#include "../../DX12/EnhancedIBLGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    constexpr uint32_t kVkIblCubeSize = 64;
    constexpr uint32_t kVkIblBrdfSize = 64;
    constexpr uint32_t kVkIblEquirectWidth = 256;
    constexpr uint32_t kVkIblEquirectHeight = 8;
    constexpr uint32_t kVkIblRegionCount = 10;
    constexpr uint16_t kVkIblHalfOne = 0x3C00;

    struct VkIblSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };
}

bool RunVulkanIBLTest(std::string& outLog)
{
    outLog += "── IBL 생성 체인 — Vulkan/DX12 동일 픽셀 fixture ──\n";
    std::string error;

    VulkanDeviceResources resources;
    if (!VulkanApi::LoadLoader(error) ||
        !resources.Initialize(kVkIblCubeSize, kVkIblCubeSize, true, error))
    {
        outLog += "[1/5] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }

    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelineCache);

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &pipelineCache;
    frameContext.rootSignatures = &pipelineCache;
    frameContext.width = kVkIblCubeSize;
    frameContext.height = kVkIblCubeSize;

    EnhancedIBLGenerator generator;
    RHITextureHandle equirect;
    RHIReadback readback{};

    const auto cleanup = [&]() {
        resources.AbortFrame();
        resources.WaitForGpu();
        generator.Shutdown();
        resources.ReleaseReadback(readback);
        resources.ReleaseTexture(equirect);
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
        outLog += "IBL 생성 체인(Vulkan) 검증 실패\n";
        return false;
    };

    {
        VkIblSpirvScope spirvScope;
        if (!generator.Initialize(frameContext, error))
            return fail("[1/5] IBL SPIR-V/파이프라인 초기화 실패: " + error + "\n");
    }
    outLog += "[1/5] 셰이더 4종 SPIR-V 컴파일·파이프라인 생성 통과\n";

    RHITextureDesc equirectDesc{};
    equirectDesc.width = kVkIblEquirectWidth;
    equirectDesc.height = kVkIblEquirectHeight;
    equirectDesc.format = RHIFormat::RGBA16Float;
    equirectDesc.debugName = L"VkIBL.Equirect";
    if (!resources.CreateTexture(equirectDesc, equirect, error))
        return fail("[2/5] equirect 생성 실패: " + error + "\n");

    if (!resources.BeginFrame(error))
        return fail("[2/5] 업로드 BeginFrame 실패: " + error + "\n");

    constexpr uint32_t kPixelBytes = 8;
    constexpr uint32_t kEquirectBytes =
        kVkIblEquirectWidth * kVkIblEquirectHeight * kPixelBytes;
    const RHIBufferSlice upload = resources.AllocateUpload(
        RHIUploadRequest{ kEquirectBytes, RHIUploadUsage::TextureCopy, 16 });
    if (!upload.IsValid()) return fail("[2/5] 업로드 링 할당 실패\n");

    auto* pixels = static_cast<uint16_t*>(upload.cpuAddress);
    for (uint32_t y = 0; y < kVkIblEquirectHeight; ++y)
    {
        const bool top = y < kVkIblEquirectHeight / 2;
        for (uint32_t x = 0; x < kVkIblEquirectWidth; ++x)
        {
            *pixels++ = top ? kVkIblHalfOne : 0;
            *pixels++ = top ? 0 : kVkIblHalfOne;
            *pixels++ = 0;
            *pixels++ = kVkIblHalfOne;
        }
    }

    const RHITransition toCopyDest{
        equirect, RHIResourceState::Common, RHIResourceState::CopyDest };
    resources.TransitionResources({ &toCopyDest, 1 });

    const VulkanBufferEntry uploadEntry = resources.GetResourceTable().Resolve(upload.buffer);
    const VulkanImageEntry equirectEntry = resources.GetResourceTable().Resolve(equirect);
    if (!uploadEntry.IsValid() || !equirectEntry.IsValid())
        return fail("[2/5] 업로드 리소스 표 해석 실패\n");

    VkBufferImageCopy copy{};
    copy.bufferOffset = upload.offset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { kVkIblEquirectWidth, kVkIblEquirectHeight, 1 };
    VulkanApi::vkCmdCopyBufferToImage(resources.GetCommandBuffer(), uploadEntry.buffer,
        equirectEntry.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    const RHITransition toShader{
        equirect, RHIResourceState::CopyDest, RHIResourceState::PixelShaderResource };
    resources.TransitionResources({ &toShader, 1 });

    if (!generator.Generate(frameContext, equirect, RHIFormat::RGBA16Float,
        kVkIblCubeSize, kVkIblBrdfSize, error))
    {
        return fail("[2/5] IBL 생성 실패: " + error + "\n");
    }
    if (!resources.EndFrame(error))
        return fail("[2/5] 생성 EndFrame 실패: " + error + "\n");
    resources.WaitForGpu();
    outLog += "[2/5] equirect 업로드 + 중립 즉시 인코더 생성 체인 완료\n";

    if (!resources.CreateReadback(kVkIblCubeSize, kVkIblCubeSize,
        EnhancedIBLGenerator::kFormat, kVkIblRegionCount, readback, error))
    {
        return fail("[3/5] 리드백 생성 실패: " + error + "\n");
    }
    if (!resources.BeginFrame(error))
        return fail("[3/5] 리드백 BeginFrame 실패: " + error + "\n");

    const RHITransition toCopySource[] = {
        { generator.GetCubeMap(), RHIResourceState::PixelShaderResource,
            RHIResourceState::CopySource },
        { generator.GetIrradianceMap(), RHIResourceState::PixelShaderResource,
            RHIResourceState::CopySource },
        { generator.GetPrefilteredMap(), RHIResourceState::PixelShaderResource,
            RHIResourceState::CopySource },
        { generator.GetBrdfLut(), RHIResourceState::PixelShaderResource,
            RHIResourceState::CopySource },
    };
    resources.TransitionResources(toCopySource);

    RHIEncoder& encoder = resources.GetImmediateEncoder();
    const auto copyRegion = [&](RHITextureHandle source, uint32_t subresource,
        uint32_t region) {
        encoder.CopyToReadback(readback, source, region, subresource);
    };

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
        return fail("[3/5] 리드백 EndFrame 실패: " + error + "\n");
    resources.WaitForGpu();

    RHIReadbackImage captured{};
    if (!resources.MapReadback(readback, captured, error))
        return fail("[3/5] 리드백 Map 실패: " + error + "\n");

    const auto region = [&](uint32_t index, uint32_t x, uint32_t y, uint32_t channel) {
        return captured.At(x, y, channel, index);
    };
    constexpr uint32_t kMid = kVkIblCubeSize / 2;

    const float upR = region(0, kMid, kMid, 0);
    const float upG = region(0, kMid, kMid, 1);
    const float downR = region(1, kMid, kMid, 0);
    const float downG = region(1, kMid, kMid, 1);
    char rectLine[192]{};
    std::snprintf(rectLine, sizeof(rectLine),
        "[3/5] DX12 픽셀 대조 rect→cube — +Y(R %.2f G %.2f) · -Y(R %.2f G %.2f)\n",
        upR, upG, downR, downG);
    outLog += rectLine;
    if (upR < 0.9f || upG > 0.1f || downG < 0.9f || downR > 0.1f)
        return fail("rect→cube 결과가 DX12 기준과 다르다\n");

    const float irrUpR = region(2, kMid, kMid, 0);
    const float irrUpG = region(2, kMid, kMid, 1);
    const float irrDownR = region(3, kMid, kMid, 0);
    const float irrDownG = region(3, kMid, kMid, 1);
    const float irrSideR = region(4, kMid, kMid, 0);
    const float irrSideG = region(4, kMid, kMid, 1);
    const float sharpGap = region(5, kMid, kMid, 0) - region(5, kMid, kMid, 1);
    const float roughGap = region(6, 0, 0, 0) - region(6, 0, 0, 1);
    const float sharpGapDown = region(7, kMid, kMid, 1) - region(7, kMid, kMid, 0);
    const float roughGapDown = region(8, 0, 0, 1) - region(8, 0, 0, 0);
    char convolutionLine[320]{};
    std::snprintf(convolutionLine, sizeof(convolutionLine),
        "[4/5] DX12 픽셀 대조 조도 +Y(%.2f %.2f) · -Y(%.2f %.2f) · +X(%.2f %.2f)"
        " · 프리필터 +Y %.2f→%.2f · -Y %.2f→%.2f\n",
        irrUpR, irrUpG, irrDownR, irrDownG, irrSideR, irrSideG,
        sharpGap, roughGap, sharpGapDown, roughGapDown);
    outLog += convolutionLine;
    const float sideRatio = irrSideR / (std::max)(irrSideG, 1e-4f);
    if (irrUpR < irrUpG * 2.f || irrDownG < irrDownR * 2.f ||
        sideRatio < 0.5f || sideRatio > 2.f ||
        sharpGap < 0.8f || sharpGapDown < 0.8f ||
        roughGap > sharpGap * 0.95f || roughGapDown > sharpGapDown * 0.95f)
    {
        return fail("조도/프리필터 결과가 DX12 기준과 다르다\n");
    }

    const float cornerA = region(9, kVkIblBrdfSize - 1, 0, 0);
    const float cornerB = region(9, kVkIblBrdfSize - 1, 0, 1);
    const float centerA = region(9, kVkIblBrdfSize / 2, kVkIblBrdfSize / 2, 0);
    const float centerB = region(9, kVkIblBrdfSize / 2, kVkIblBrdfSize / 2, 1);
    char brdfLine[224]{};
    std::snprintf(brdfLine, sizeof(brdfLine),
        "[5/5] DX12 픽셀 대조 BRDF — 모서리(A %.3f B %.3f) · 가운데(A %.3f B %.3f)\n",
        cornerA, cornerB, centerA, centerB);
    outLog += brdfLine;
    if (cornerA < 0.9f || cornerB > 0.05f || centerA < 0.2f || centerA + centerB > 1.1f)
        return fail("BRDF LUT 결과가 DX12 기준과 다르다\n");

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    if (0 != stubs)
    {
        const char* last = resources.GetEncoderLastUnimplemented();
        if (nullptr == last) last = resources.GetLastUnimplemented();
        return fail("미구현 호출 " + std::to_string(stubs) + "건(" +
            (nullptr != last ? last : "-") + ")\n");
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
        return fail("Vulkan validation " + std::to_string(problems) + "건\n" + validation);

    cleanup();
    outLog += "IBL 생성 체인(Vulkan) 검증 통과 — validation 0 · 미구현 0\n";
    return true;
}

#endif
