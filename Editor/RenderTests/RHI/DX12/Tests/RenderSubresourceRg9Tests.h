#pragma once
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12DeviceResources.h"

bool DX12Test::RunRg9SubresourceTest(std::string& outLog)
{
    uint32_t checks = 0;
    const auto require = [&](bool valid, const std::string& message)
    {
        ++checks;
        if (!valid)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        class TestDevice final : public DX12DeviceResources
        {
          public:
            bool split{true};
            bool SupportsSplitTransitions() const override { return split; }
        } device;
        std::string error;
        require(device.Initialize(32, 32, error), error);
        require(device.HasDebugMessageQueue(), "RG9 requires GPU validation");
        uint32_t splitFrames = 0, fullFrames = 0;
        for (uint32_t frame = 0; frame < 512; ++frame)
        {
            device.split = (frame & 1) != 0;
            device.SetDeferredImmediateRecording((frame & 2) != 0);
            require(device.BeginFrame(error), error);
            RHITextureDesc desc{};
            desc.width = desc.height = 8;
            desc.mipLevels = 2;
            desc.depthOrArraySize = 2;
            desc.allowRenderTarget = true;
            desc.format = RHIFormat::RGBA8Unorm;
            desc.clearColor[0] = float((frame / 4) & 1);
            desc.clearColor[3] = 1.f;
            RHITextureHandle textures[2];
            require(device.CreateTexture(desc, textures[0], error) && device.CreateTexture(desc, textures[1], error), error);
            RHIReadback readback;
            require(device.CreateReadback(4, 4, desc.format, 1, readback, error), error);
            {
                EnhancedRenderGraph graph{device, RGSchedulingMode::ExplicitVersioned};
                RGTextureDesc transient{};
                transient.width = transient.height = 8;
                transient.arraySize = 2;
                transient.mipLevels = 2;
                transient.allowRenderTarget = true;
                transient.name = "RG9 transient mips";
                std::copy_n(desc.clearColor, 4, transient.clearColor);
                const auto first = graph.Write((frame & 4) != 0 ? graph.CreateTexture(transient) :
                    graph.ImportTexture(textures[0], RHIResourceState::Common, "RG9 first"));
                const auto second = graph.Write(graph.ImportTexture(textures[1], RHIResourceState::Common, "RG9 independent"));
                const RHITextureSubresourceRange range{1, 1, 1, 1};
                const EnhancedRenderGraph::RGPassUsage writeFirst{first, RHIResourceState::RenderTarget, RGAccessMode::Write, range};
                const EnhancedRenderGraph::RGPassUsage writeSecond{second, RHIResourceState::RenderTarget, RGAccessMode::Write, range};
                const EnhancedRenderGraph::RGPassUsage readFirst{first, RHIResourceState::CopySource, RGAccessMode::Read, range};
                const float color[]{float((frame / 4) & 1), 0.f, 0.f, 1.f};
                graph.AddRepeatedPass("RG9 paired range", {writeFirst, writeSecond},
                    {{"write", {writeFirst}}, {"independent", {writeSecond}}, {"read", {readFirst}}}, 3,
                    [&](const auto& context, uint32_t, uint32_t phase)
                    {
                        if (phase == 2)
                        {
                            context.encoder->CopyToReadback(readback, context.ResolveHandle(first), 0, 3);
                        }
                        else
                        {
                            const auto target = RHIColorTargetDesc::Slice(context.ResolveHandle(phase == 0 ? first : second), desc.format, 1, 1);
                            const auto binding = device.CreateRenderTargets(std::span<const RHIColorTargetDesc>{&target, 1});
                            if (!binding.IsValid())
                            {
                                throw std::runtime_error("RG9 slice RTV creation failed");
                            }
                            context.encoder->ClearRenderTargets(binding, color);
                        }
                    }, true);
                require(graph.Compile(error), error);
                EnhancedRenderGraph::DiagnosticSnapshot snapshot;
                require(graph.CaptureDiagnosticSnapshot(snapshot), "RG9 native snapshot");
                const auto& phases = snapshot.passes.front().phases;
                const auto begins = std::ranges::count_if(phases[1].firstBarriers, [](const auto& barrier)
                { return barrier.phase == RHITransitionPhase::Begin; });
                const auto ends = std::ranges::count_if(phases[2].firstBarriers, [](const auto& barrier)
                { return barrier.phase == RHITransitionPhase::End; });
                require(begins == (device.split ? 1 : 0) && ends == begins, "RG9 native split/fallback snapshot mismatch");
                require(graph.Execute(error), error);
                require(device.EndFrame(error), error);
                device.WaitForGpu();
                RHIReadbackImage image;
                require(device.MapReadback(readback, image, error), error);
                require(image.width == 4 && image.height == 4, "RG9 mip readback size");
                for (uint32_t y = 0; y < 4; ++y)
                {
                    for (uint32_t x = 0; x < 4; ++x)
                    {
                        require(image.At(x, y, 0) == color[0] && image.At(x, y, 1) == 0.f &&
                                image.At(x, y, 2) == 0.f && image.At(x, y, 3) == 1.f, "RG9 exact readback pixel");
                    }
                }
                splitFrames += device.split ? 1 : 0;
                fullFrames += device.split ? 0 : 1;
                graph.Reset();
            }
            device.ReleaseReadback(readback);
            device.ReleaseTexture(textures[0]);
            device.ReleaseTexture(textures[1]);
        }
        std::string validation;
        require(device.DrainDebugMessages(validation) == 0, validation);
        outLog = "RG9_SUBRESOURCES_OK checks=" + std::to_string(checks) + " frames=512 splitFrames=" +
                 std::to_string(splitFrames) + " fullFrames=" + std::to_string(fullFrames) +
                 " repetitions=3 mip=1 layer=1 direct=true software=true transientMips=true pixelError=0 validationErrors=0\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog = std::string("RG9_SUBRESOURCES_FAILED ") + exception.what() + "\n";
        return false;
    }
}
