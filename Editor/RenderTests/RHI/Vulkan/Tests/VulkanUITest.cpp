#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Passes/UI/EnhancedUIPass.h"
#include "Render/Passes/Geometry/EnhancedSpritePass.h"
#include "FontAsset.h"
#include "Texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <limits>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kUiRhiSize = 256;

    struct UiRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct UiRhiFixture
    {
        Texture* redTexture{ nullptr };
        Texture* blueTexture{ nullptr };
        std::shared_ptr<Texture> distanceTexture;
        std::vector<EnhancedUIPass::Rect> baseRects;
        std::vector<EnhancedUIPass::Rect> texturedRects;
        std::vector<EnhancedUIPass::Rect> distanceRects;

        UiRhiFixture()
        {
            const uint8_t red[4] = { 255, 0, 0, 255 };
            const uint8_t blue[4] = { 0, 0, 255, 255 };
            redTexture = Texture::CreateFromPixels(1, 1, "rhi_ui_red",
                RHIFormat::RGBA8Unorm, red);
            blueTexture = Texture::CreateFromPixels(1, 1, "rhi_ui_blue",
                RHIFormat::RGBA8Unorm, blue);
            // Exact outside / contour / inside plateaus make coverage and the
            // straight-alpha blend test independent of font files and rasterizers.
            constexpr std::array<uint8_t, 8> distances{ 0, 0, 128, 128, 255, 255, 0, 0 };
            std::array<uint8_t, 32> distancePixels{};
            for (size_t pixel = 0; pixel < distances.size(); ++pixel)
            {
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    distancePixels[pixel * 4 + channel] = distances[pixel];
                }
            }
            distanceTexture.reset(Texture::CreateFromPixels(8, 1, "rhi_ui_distance",
                RHIFormat::RGBA8Unorm, distancePixels.data()));

            EnhancedUIPass::Rect redRect{};
            redRect.left = 10.f;
            redRect.top = 10.f;
            redRect.right = 60.f;
            redRect.bottom = 60.f;
            redRect.color = { 1.f, 0.f, 0.f, 1.f };
            redRect.layerOrder = 0;
            baseRects.push_back(redRect);

            EnhancedUIPass::Rect blueRect{};
            blueRect.left = 35.f;
            blueRect.top = 35.f;
            blueRect.right = 85.f;
            blueRect.bottom = 85.f;
            blueRect.color = { 0.f, 0.f, 1.f, 1.f };
            blueRect.layerOrder = 1;
            baseRects.push_back(blueRect);

            EnhancedUIPass::Rect greenRect{};
            greenRect.left = 140.f;
            greenRect.top = 10.f;
            greenRect.right = 240.f;
            greenRect.bottom = 110.f;
            greenRect.color = { 0.f, 1.f, 0.f, 1.f };
            greenRect.layerOrder = 0;
            baseRects.push_back(greenRect);

            EnhancedUIPass::Rect blendRect{};
            blendRect.left = 160.f;
            blendRect.top = 30.f;
            blendRect.right = 220.f;
            blendRect.bottom = 90.f;
            blendRect.color = { 1.f, 0.f, 0.f, 0.5f };
            blendRect.layerOrder = 1;
            baseRects.push_back(blendRect);

            // 목록 순서와 layerOrder 순서를 의도적으로 다르게 둔다.
            std::swap(baseRects[1], baseRects[2]);

            for (uint32_t i = 0; i < 4; ++i)
            {
                EnhancedUIPass::Rect rect{};
                rect.left = 10.f + 55.f * static_cast<float>(i);
                rect.top = 150.f;
                rect.right = rect.left + 40.f;
                rect.bottom = 200.f;
                rect.color = { 1.f, 1.f, 1.f, 1.f };
                rect.texture = (0 == (i & 1u)) ? redTexture : blueTexture;
                texturedRects.push_back(rect);
            }

            EnhancedUIPass::Rect distanceBackground{};
            distanceBackground.left = 16.f;
            distanceBackground.top = 16.f;
            distanceBackground.right = 144.f;
            distanceBackground.bottom = 80.f;
            distanceBackground.color = { 0.f, 1.f, 0.f, 1.f };
            distanceRects.push_back(distanceBackground);

            EnhancedUIPass::Rect distanceRect = distanceBackground;
            distanceRect.color = { 1.f, 0.f, 0.f, 0.5f };
            distanceRect.textureOwner = distanceTexture;
            distanceRect.signedDistance = true;
            distanceRects.push_back(distanceRect);

            // Same atlas, adjacent instance, different mode. It must stay an
            // ordinary tinted image even though batching keeps both in one draw.
            EnhancedUIPass::Rect imageRect = distanceRect;
            imageRect.top = 112.f;
            imageRect.bottom = 176.f;
            imageRect.color = { 0.f, 0.f, 1.f, 1.f };
            imageRect.signedDistance = false;
            distanceRects.push_back(imageRect);
        }

        ~UiRhiFixture()
        {
            delete redTexture;
            delete blueTexture;
        }

        bool IsValid() const
        {
            return nullptr != redTexture && nullptr != blueTexture && nullptr != distanceTexture;
        }
    };

    struct UiRhiFrame
    {
        RHIReadbackImage image;
        EnhancedRenderGraph::Stats graph{};
        uint32_t rects{ 0 };
        uint32_t batches{ 0 };
    };

    struct UiRhiCapture
    {
        UiRhiFrame base;
        UiRhiFrame textured;
        UiRhiFrame distance;

        float redOnly{ 0.f };
        float overlapR{ 0.f };
        float overlapB{ 0.f };
        float greenOnly{ 0.f };
        float blendR{ 0.f };
        float blendG{ 0.f };
        float outsideA{ 0.f };
        float textureRed{ 0.f };
        float textureBlue{ 0.f };
        float distanceOutsideR{ 0.f };
        float distanceOutsideG{ 0.f };
        float distanceContourR{ 0.f };
        float distanceContourG{ 0.f };
        float distanceInsideR{ 0.f };
        float distanceInsideG{ 0.f };
        float distanceImageB{ 0.f };

        uint32_t textureUploads{ 0 };
        uint32_t textureHits{ 0 };
        uint32_t textureFailures{ 0 };
        uint32_t fromCpuPixels{ 0 };
    };

    template <typename TResources, typename TTextureCache>
    bool CaptureUiRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        TTextureCache& textures, const UiRhiFixture& fixture,
        UiRhiCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.textureCache = &textures;
        context.width = kUiRhiSize;
        context.height = kUiRhiSize;

        EnhancedUIPass ui;
        RHIReadback readback{};
        bool frameOpen = false;
        uint64_t frameIndex = 0;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            ui.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!ui.Initialize(context, outError)) return fail(outError);
        if (!resources.CreateReadback(kUiRhiSize, kUiRhiSize,
                EnhancedUIPass::kOutputFormat, 1, readback, outError))
            return fail(outError);

        const auto render = [&](const std::vector<EnhancedUIPass::Rect>& rects,
            UiRhiFrame& frame) -> bool
        {
            ui.SetRects(&rects);
            if (!resources.BeginFrame(outError)) return false;
            frameOpen = true;
            textures.BeginFrame(frameIndex++);
            if (!ui.PrepareFrame(context, outError)) return false;

            EnhancedRenderGraph graph(
                static_cast<IRenderDeviceServices&>(resources));
            ui.Declare(graph, context);
            const RGHandle output = ui.GetOutput();
            if (!output.IsValid())
            {
                outError = "UI 출력이 선언되지 않았다";
                return false;
            }
            graph.AddPass("UIRHI.Readback",
                { { output, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback(
                        readback, executeContext.ResolveHandle(output));
                }, true);
            if (!graph.Compile(outError) || !graph.Execute(outError)) return false;
            frame.graph = graph.GetStats();
            frame.rects = ui.GetLastRectCount();
            frame.batches = ui.GetLastBatchCount();

            if (!resources.EndFrame(outError)) return false;
            frameOpen = false;
            resources.WaitForGpu();
            return resources.MapReadback(readback, frame.image, outError);
        };

        if (!render(fixture.baseRects, outCapture.base)) return fail(outError);
        if (!render(fixture.texturedRects, outCapture.textured)) return fail(outError);
        if (!render(fixture.distanceRects, outCapture.distance))
        {
            return fail(outError);
        }

        outCapture.redOnly = outCapture.base.image.At(20, 20, 0);
        outCapture.overlapR = outCapture.base.image.At(50, 50, 0);
        outCapture.overlapB = outCapture.base.image.At(50, 50, 2);
        outCapture.greenOnly = outCapture.base.image.At(150, 20, 1);
        outCapture.blendR = outCapture.base.image.At(190, 60, 0);
        outCapture.blendG = outCapture.base.image.At(190, 60, 1);
        outCapture.outsideA = outCapture.base.image.At(120, 200, 3);
        outCapture.textureRed = outCapture.textured.image.At(30, 175, 0);
        outCapture.textureBlue = outCapture.textured.image.At(85, 175, 2);
        outCapture.distanceOutsideR = outCapture.distance.image.At(24, 48, 0);
        outCapture.distanceOutsideG = outCapture.distance.image.At(24, 48, 1);
        outCapture.distanceContourR = outCapture.distance.image.At(56, 48, 0);
        outCapture.distanceContourG = outCapture.distance.image.At(56, 48, 1);
        outCapture.distanceInsideR = outCapture.distance.image.At(88, 48, 0);
        outCapture.distanceInsideG = outCapture.distance.image.At(88, 48, 1);
        outCapture.distanceImageB = outCapture.distance.image.At(56, 144, 2);

        const auto stats = textures.GetStats();
        outCapture.textureUploads = stats.uploads;
        outCapture.textureHits = stats.hits;
        outCapture.textureFailures = stats.failures;
        outCapture.fromCpuPixels = stats.fromCpuPixels;

        ui.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }

    bool UiRhiFunctional(const UiRhiCapture& capture)
    {
        const auto graphOk = [](const UiRhiFrame& frame)
        {
            return 2 == frame.graph.passesExecuted &&
                0 == frame.graph.passesCulled &&
                1 == frame.graph.transientCreated;
        };
        return graphOk(capture.base) && graphOk(capture.textured) && graphOk(capture.distance) &&
            4 == capture.base.rects && 1 == capture.base.batches &&
            4 == capture.textured.rects && 4 == capture.textured.batches &&
            3 == capture.distance.rects && 2 == capture.distance.batches &&
            capture.redOnly > 0.9f &&
            capture.overlapR < 0.1f && capture.overlapB > 0.9f &&
            capture.greenOnly > 0.9f &&
            capture.blendR > 0.2f && capture.blendR < 0.8f &&
            capture.blendG > 0.2f && capture.blendG < 0.8f &&
            capture.outsideA < 0.01f &&
            capture.textureRed > 0.9f && capture.textureBlue > 0.9f &&
            capture.distanceOutsideR < 0.01f && capture.distanceOutsideG > 0.99f &&
            std::fabs(capture.distanceContourR - 0.25f) < 0.02f &&
            std::fabs(capture.distanceContourG - 0.75f) < 0.02f &&
            std::fabs(capture.distanceInsideR - 0.5f) < 0.02f &&
            std::fabs(capture.distanceInsideG - 0.5f) < 0.02f &&
            std::fabs(capture.distanceImageB - (128.f / 255.f) * (128.f / 255.f)) < 0.02f &&
            3 == capture.textureUploads && capture.textureHits >= 2 &&
            0 == capture.textureFailures && 3 == capture.fromCpuPixels;
    }

    bool UiRhiTextureLifetime(std::string& outError)
    {
        EnhancedUIPass ui;
        EnhancedFrameContext context{};
        std::vector<EnhancedUIPass::Rect> rects(1);
        rects.front().textureOwner = std::make_shared<Texture>();
        const std::weak_ptr<Texture> weakTexture = rects.front().textureOwner;
        ui.SetRects(&rects);
        // CPU-only: no texture cache, device or fake upload is involved.
        if (!ui.PrepareFrame(context, outError))
        {
            return false;
        }
        rects.clear();
        if (weakTexture.expired())
        {
            outError = "UI batch did not retain the source texture owner";
            return false;
        }
        ui.Shutdown();
        if (!weakTexture.expired())
        {
            outError = "UI shutdown did not release the source texture owner";
            return false;
        }
        return true;
    }

    bool UiRhiTextRects(std::string& outError)
    {
        auto layout = std::make_shared<TextLayout>();
        layout->width = 100.f;
        layout->height = 40.f;
        TextGlyph glyph{};
        glyph.left = 10.f;
        glyph.top = 4.f;
        glyph.right = 30.f;
        glyph.bottom = 24.f;
        glyph.uvLeft = 0.1f;
        glyph.uvTop = 0.2f;
        glyph.uvRight = 0.3f;
        glyph.uvBottom = 0.6f;
        glyph.texture = std::make_shared<Texture>();
        layout->glyphs.push_back(glyph);
        glyph.left = 50.f;
        glyph.right = 80.f;
        layout->glyphs.push_back(glyph);

        UIRenderProxy::TextData text{};
        text.layout = layout;
        text.position = { 200.f, 100.f };
        text.canvasOrder = 7;
        text.layerOrder = 3;
        std::vector<EnhancedUIPass::Rect> rects(1);
        if (!EnhancedUIPass::AppendTextRects(text, rects, 20.f, 30.f) || rects.size() != 3 ||
            rects[1].left != 180.f || rects[1].top != 114.f ||
            rects[1].right != 200.f || rects[1].bottom != 134.f ||
            rects[2].left != 220.f || rects[1].canvasOrder != 7 ||
            rects[1].layerOrder != 3 || !rects[1].signedDistance ||
            rects[1].textureOwner != glyph.texture)
        {
            outError = "Text rectangles lost center anchor, glyph order, append semantics or ownership";
            return false;
        }
        rects.clear();
        text.alignment = TextAlignment::Left;
        if (!EnhancedUIPass::AppendTextRects(text, rects) || rects.size() != 2 || rects[0].left != 210.f)
        {
            outError = "Text left anchor is incorrect";
            return false;
        }
        rects.clear();
        text.alignment = TextAlignment::Right;
        if (!EnhancedUIPass::AppendTextRects(text, rects) || rects.size() != 2 || rects[0].left != 110.f)
        {
            outError = "Text right anchor is incorrect";
            return false;
        }
        rects.clear();
        text.filpEffect = static_cast<UIEffects>(3);
        if (!EnhancedUIPass::AppendTextRects(text, rects) || rects.size() != 2 ||
            rects[0].left != 170.f || rects[0].top != 96.f ||
            rects[0].right != 190.f || rects[0].bottom != 116.f ||
            rects[0].uvLeft != 0.3f || rects[0].uvRight != 0.1f ||
            rects[0].uvTop != 0.6f || rects[0].uvBottom != 0.2f)
        {
            outError = "Text flip did not mirror both block geometry and glyph UVs";
            return false;
        }
        rects.clear();
        text.position.x = (std::numeric_limits<float>::quiet_NaN)();
        if (EnhancedUIPass::AppendTextRects(text, rects) || !rects.empty())
        {
            outError = "Text accepted a nonfinite anchor";
            return false;
        }
        text.position.x = 200.f;
        layout->glyphs.front().uvLeft = (std::numeric_limits<float>::infinity)();
        if (EnhancedUIPass::AppendTextRects(text, rects) || rects.size() != 1)
        {
            outError = "Text accepted nonfinite UVs or discarded valid adjacent glyphs";
            return false;
        }

        // Planar text uses the same owner and signed-distance fields, including
        // when the source list is rebuilt before graph recording.
        EnhancedSpritePass sprite;
        EnhancedFrameContext context{};
        std::vector<EnhancedSpritePass::Item> items(1);
        items.front().textureOwner = std::make_shared<Texture>();
        items.front().signedDistance = true;
        const std::weak_ptr<Texture> weakTexture = items.front().textureOwner;
        sprite.SetItems(&items);
        if (!sprite.PrepareFrame(context, outError))
        {
            return false;
        }
        items.clear();
        if (weakTexture.expired() || sprite.GetLastItemCount() != 1 || sprite.GetLastBatchCount() != 1)
        {
            outError = "Planar text did not preserve batch ownership";
            return false;
        }
        sprite.Shutdown();
        if (!weakTexture.expired())
        {
            outError = "Planar text shutdown did not release batch ownership";
            return false;
        }
        return true;
    }

    struct UiRhiComparison
    {
        float maxDelta{ 0.f };
        double sumDelta{ 0.0 };
        uint64_t samples{ 0 };
        uint64_t overThreshold{ 0 };
        bool shapeMatches{ true };
    };

    void CompareUiRhiImage(const RHIReadbackImage& lhs,
        const RHIReadbackImage& rhs, UiRhiComparison& comparison)
    {
        if (!lhs.IsValid() || !rhs.IsValid() ||
            lhs.width != rhs.width || lhs.height != rhs.height)
        {
            comparison.shapeMatches = false;
            return;
        }
        for (uint32_t y = 0; y < lhs.height; ++y)
        {
            for (uint32_t x = 0; x < lhs.width; ++x)
            {
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    const float delta = std::fabs(
                        lhs.At(x, y, channel) - rhs.At(x, y, channel));
                    comparison.maxDelta =
                        (std::max)(comparison.maxDelta, delta);
                    comparison.sumDelta += delta;
                    ++comparison.samples;
                    if (delta > 0.003f) ++comparison.overThreshold;
                }
            }
        }
    }
}

bool RunVulkanUITest(std::string& outLog)
{
    outLog += "── UI 공용 패스 — DX12/Vulkan order·blend·texture·SDF 대조 ──\n";
    UiRhiFixture fixture;
    if (!fixture.IsValid())
    {
        outLog += "[1/6] 합성 UI 텍스처 생성 실패\n";
        return false;
    }

    UiRhiCapture dx12Capture{};
    UiRhiCapture vkCapture{};
    std::string error;
    if (!UiRhiTextureLifetime(error) || !UiRhiTextRects(error))
    {
        outLog += "UI text/lifetime check failed: " + error + "\n";
        return false;
    }
    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12TextureCache textures;
        if (!resources.Initialize(kUiRhiSize, kUiRhiSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_ui.cache", error) ||
            !roots.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            outLog += "[1/6] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureUiRhiBackend(
            resources, pipelines, roots, textures, fixture, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        textures.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/6] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    char dx12Line[384]{};
    std::snprintf(dx12Line, sizeof(dx12Line),
        "[1/6] DX12 — rect/batch %u/%u · red %.3f · overlap R/B %.3f/%.3f · "
        "green %.3f · blend R/G %.3f/%.3f · outside A %.3f\n"
        "      texture R/B %.3f/%.3f · mixed batch %u · upload/hit %u/%u\n",
        dx12Capture.base.rects, dx12Capture.base.batches,
        dx12Capture.redOnly, dx12Capture.overlapR, dx12Capture.overlapB,
        dx12Capture.greenOnly, dx12Capture.blendR, dx12Capture.blendG,
        dx12Capture.outsideA, dx12Capture.textureRed, dx12Capture.textureBlue,
        dx12Capture.textured.batches,
        dx12Capture.textureUploads, dx12Capture.textureHits);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/6] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanTextureCache textures;
    if (!resources.Initialize(kUiRhiSize, kUiRhiSize, true, error))
    {
        outLog += "[2/6] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!textures.Initialize(&resources, error))
    {
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/6] Vulkan texture cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        UiRhiSpirvScope spirv;
        captured = CaptureUiRhiBackend(
            resources, pipelines, pipelines, textures, fixture, vkCapture, error);
    }
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[416]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/6] Vulkan — rect/batch %u/%u · red %.3f · overlap R/B %.3f/%.3f · "
            "green %.3f · blend R/G %.3f/%.3f · outside A %.3f\n"
            "      texture R/B %.3f/%.3f · mixed batch %u · upload/hit/fail %u/%u/%u · "
            "그래프 %u패스/%u transient\n",
            vkCapture.base.rects, vkCapture.base.batches,
            vkCapture.redOnly, vkCapture.overlapR, vkCapture.overlapB,
            vkCapture.greenOnly, vkCapture.blendR, vkCapture.blendG,
            vkCapture.outsideA, vkCapture.textureRed, vkCapture.textureBlue,
            vkCapture.textured.batches,
            vkCapture.textureUploads, vkCapture.textureHits,
            vkCapture.textureFailures,
            vkCapture.base.graph.passesExecuted,
            vkCapture.base.graph.transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        UiRhiFunctional(dx12Capture) && UiRhiFunctional(vkCapture);

    UiRhiComparison comparison{};
    CompareUiRhiImage(dx12Capture.base.image,
        vkCapture.base.image, comparison);
    CompareUiRhiImage(dx12Capture.textured.image,
        vkCapture.textured.image, comparison);
    CompareUiRhiImage(dx12Capture.distance.image,
        vkCapture.distance.image, comparison);
    const double meanDelta = (0 == comparison.samples) ? 0.0 :
        comparison.sumDelta / static_cast<double>(comparison.samples);

    char compareLine[320]{};
    std::snprintf(compareLine, sizeof(compareLine),
        "[3/6] base/textured/SDF 전체 RGBA %llu표본 — 최대 %.6f · 평균 %.8f · "
        "0.003 초과 %llu\n",
        static_cast<unsigned long long>(comparison.samples),
        comparison.maxDelta, meanDelta,
        static_cast<unsigned long long>(comparison.overThreshold));
    outLog += compareLine;
    if (!comparison.shapeMatches || 0 == comparison.samples ||
        comparison.maxDelta > 0.003f || meanDelta > 0.00002 ||
        0 != comparison.overThreshold)
    {
        passed = false;
        outLog += "UI 전체 픽셀 대조 허용 범위를 벗어났다\n";
    }

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.redOnly - vkCapture.redOnly),
        std::fabs(dx12Capture.overlapR - vkCapture.overlapR),
        std::fabs(dx12Capture.overlapB - vkCapture.overlapB),
        std::fabs(dx12Capture.greenOnly - vkCapture.greenOnly),
        std::fabs(dx12Capture.blendR - vkCapture.blendR),
        std::fabs(dx12Capture.blendG - vkCapture.blendG),
        std::fabs(dx12Capture.outsideA - vkCapture.outsideA),
        std::fabs(dx12Capture.textureRed - vkCapture.textureRed),
        std::fabs(dx12Capture.textureBlue - vkCapture.textureBlue),
        std::fabs(dx12Capture.distanceOutsideR - vkCapture.distanceOutsideR),
        std::fabs(dx12Capture.distanceOutsideG - vkCapture.distanceOutsideG),
        std::fabs(dx12Capture.distanceContourR - vkCapture.distanceContourR),
        std::fabs(dx12Capture.distanceContourG - vkCapture.distanceContourG),
        std::fabs(dx12Capture.distanceInsideR - vkCapture.distanceInsideR),
        std::fabs(dx12Capture.distanceInsideG - vkCapture.distanceInsideG),
        std::fabs(dx12Capture.distanceImageB - vkCapture.distanceImageB),
    });
    char predicateLine[288]{};
    std::snprintf(predicateLine, sizeof(predicateLine),
        "[4/6] pixel coordinate·layer order·alpha blend·texture batch 판정 최대 편차 "
        "%.6f · texture %u upload/%u hit/%u failure\n",
        predicateDelta, vkCapture.textureUploads,
        vkCapture.textureHits, vkCapture.textureFailures);
    outLog += predicateLine;
    char distanceLine[384]{};
    std::snprintf(distanceLine, sizeof(distanceLine),
        "      SDF contour R/G DX12 %.3f/%.3f Vulkan %.3f/%.3f · "
        "inside R/G %.3f/%.3f · image B %.3f · rect/batch %u/%u\n",
        dx12Capture.distanceContourR, dx12Capture.distanceContourG,
        vkCapture.distanceContourR, vkCapture.distanceContourG,
        vkCapture.distanceInsideR, vkCapture.distanceInsideG,
        vkCapture.distanceImageB, vkCapture.distance.rects, vkCapture.distance.batches);
    outLog += distanceLine;
    if (predicateDelta > 0.003f) passed = false;

    outLog += "[5/6] PrepareFrame texture lifetime·root t0·sampled t1·static s0 · 미구현 " +
        std::to_string(stubs) + "\n";
    outLog += "[6/6] Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "UI 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "UI 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}
