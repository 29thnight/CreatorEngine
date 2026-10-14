#include "SceneSurfaceTargets.h"
#include <stdexcept>

RHIFormat SceneSurfaceTargets::GetRenderTargetFormat(uint32_t index)
{
    return index < 4 ? RHIFormat::RGBA16Float : index == 4 ? RHIFormat::R32Uint : index == 5 ? RHIFormat::RG32Uint : RHIFormat::Unknown;
}

void SceneSurfaceTargets::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    static const char* names[kRenderTargetCount] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal",
        "GBuffer.Emissive", "GBuffer.Bitmask", "METEOR.Scene.Visibility"};
    std::array<RGHandle, kRenderTargetCount> colors{};
    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    for (uint32_t index = 0; index < kRenderTargetCount; ++index)
    {
        RGTextureDesc desc{};
        desc.width = context.width;
        desc.height = context.height;
        desc.format = GetRenderTargetFormat(index);
        desc.allowRenderTarget = true;
        desc.name = names[index];
        colors[index] = graph.CreateTexture(desc);
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            colors[index] = graph.Write(colors[index]);
        }
        usages.push_back({colors[index], RHIResourceState::RenderTarget, access});
    }
    RGTextureDesc desc{};
    desc.width = context.width;
    desc.height = context.height;
    desc.format = kDepthFormat;
    desc.allowDepthStencil = true;
    desc.name = "GBuffer.Depth";
    auto depth = graph.CreateTexture(desc);
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        depth = graph.Write(depth);
    }
    usages.push_back({depth, RHIResourceState::DepthWrite, access});
    m_outputs = {colors[0], colors[1], colors[2], colors[3], colors[4], depth, colors[5]};
    graph.AddPass("METEOR.Scene.Surface.Clear", usages,
        [colors, depth, &context](const EnhancedRenderGraph::ExecuteContext& execution)
        {
            std::array<RHITextureHandle, kRenderTargetCount> handles{};
            for (uint32_t index = 0; index < kRenderTargetCount; ++index)
            {
                handles[index] = execution.ResolveHandle(colors[index]);
            }
            const auto depthTarget = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depth), kDepthFormat);
            const auto targets = context.resources->CreateRenderTargets(handles, &depthTarget);
            if (!targets.IsValid())
            {
                throw std::runtime_error("Graph GBuffer clear targets are unavailable.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            constexpr float zero[4]{};
            encoder.ClearRenderTargets(targets, zero);
            encoder.ClearDepthTarget(targets, 1.f);
        });
}

