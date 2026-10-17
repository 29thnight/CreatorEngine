#include "SceneSurfaceTargets.h"
#include "../../../../Utility_Framework/StandardLibrary.h"

RHIFormat SceneSurfaceTargets::GetRenderTargetFormat(uint32_t index)
{
    return index < 4 ? RHIFormat::RGBA16Float : index == 4 ? RHIFormat::R32Uint : index == 5 ? RHIFormat::RG32Uint : RHIFormat::Unknown;
}

void SceneSurfaceTargets::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    DeclareVisibility(graph, context);
    DeclareSurface(graph, context, m_outputs.depth, m_outputs.visibility);
}

void SceneSurfaceTargets::DeclareVisibility(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    m_outputs = {};
    RGTextureDesc visibilityDesc{};
    visibilityDesc.width = context.width;
    visibilityDesc.height = context.height;
    visibilityDesc.format = RHIFormat::RG32Uint;
    visibilityDesc.allowRenderTarget = true;
    visibilityDesc.name = "METEOR.Scene.Visibility";
    auto visibility = graph.CreateTexture(visibilityDesc);
    RGTextureDesc depthDesc{};
    depthDesc.width = context.width;
    depthDesc.height = context.height;
    depthDesc.format = kDepthFormat;
    depthDesc.allowDepthStencil = true;
    depthDesc.name = "GBuffer.Depth";
    auto depth = graph.CreateTexture(depthDesc);
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
    {
        visibility = graph.Write(visibility);
        depth = graph.Write(depth);
    }
    m_outputs.visibility = visibility;
    m_outputs.depth = depth;
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    graph.AddPass("METEOR.Scene.Visibility.Clear",
        {{visibility, RHIResourceState::RenderTarget, access}, {depth, RHIResourceState::DepthWrite, access}},
        [visibility, depth, resources = context.resources](const EnhancedRenderGraph::ExecuteContext& execution)
        {
            const std::array colors{execution.ResolveHandle(visibility)};
            const auto depthTarget = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depth), kDepthFormat);
            const auto targets = resources->CreateRenderTargets(colors, &depthTarget);
            if (!targets.IsValid())
            {
                throw std::runtime_error("METEOR visibility clear targets are unavailable.");
            }
            constexpr float zero[4]{};
            execution.encoder->BindRenderTargets(targets);
            execution.encoder->ClearRenderTargets(targets, zero);
            execution.encoder->ClearDepthTarget(targets, 1.f);
        });
}

void SceneSurfaceTargets::DeclareSurface(EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
    RGHandle depth, RGHandle visibility)
{
    if (!depth.IsValid() || !visibility.IsValid())
    {
        throw std::runtime_error("SurfaceResolve requires explicit depth and visibility inputs.");
    }
    static const char* names[5] = {
        "GBuffer.Diffuse", "GBuffer.MetalRough", "GBuffer.Normal", "GBuffer.Emissive", "GBuffer.Bitmask"};
    std::array<RGHandle, 5> colors{};
    std::vector<EnhancedRenderGraph::RGPassUsage> usages;
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Write;
    for (uint32_t index = 0; index < colors.size(); ++index)
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
    m_outputs = {colors[0], colors[1], colors[2], colors[3], colors[4], depth, visibility};
    graph.AddPass("METEOR.Scene.SurfaceResolve.Clear", usages,
        [colors, resources = context.resources](const EnhancedRenderGraph::ExecuteContext& execution)
        {
            std::array<RHITextureHandle, 5> handles{};
            for (uint32_t index = 0; index < handles.size(); ++index)
            {
                handles[index] = execution.ResolveHandle(colors[index]);
            }
            const auto targets = resources->CreateRenderTargets(handles, nullptr);
            if (!targets.IsValid())
            {
                throw std::runtime_error("METEOR surface clear targets are unavailable.");
            }
            constexpr float zero[4]{};
            execution.encoder->BindRenderTargets(targets);
            execution.encoder->ClearRenderTargets(targets, zero);
        });
}
