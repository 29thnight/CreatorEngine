#include "../../../../Containers/ContainerAPI.h"
#include "EnhancedTemporalUpscalePass.h"

void EnhancedTemporalUpscalePass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    if (!m_host || !m_applied) throw std::runtime_error("Temporal upscale pass has no view owner.");
    m_output = m_inputs[0];
    if (m_host->Provider() == TemporalProvider::None)
    {
        if (!m_spatialScales && (context.width != context.displayWidth || context.height != context.displayHeight))
            throw std::runtime_error("Native temporal fallback must render at display resolution.");
        return;
    }
    RGTextureDesc desc;
    desc.width = context.displayWidth; desc.height = context.displayHeight;
    desc.format = RHIFormat::RGBA16Float;
    desc.allowUnorderedAccess = true; desc.allowRenderTarget = true;
    desc.name = "Temporal.ReconstructedHDR";
    m_output = graph.CreateTexture(desc);
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned) m_output = graph.Write(m_output);
    RGHandle exposure;
    if (m_host->Provider() == TemporalProvider::Dlss)
    {
        // The engine uses manual post-chain exposure. Give DLSS that same
        // value instead of a separate auto-exposure history surviving cuts.
        RGTextureDesc exposureDesc;
        exposureDesc.width = exposureDesc.height = 1;
        exposureDesc.format = RHIFormat::R32Float;
        exposureDesc.allowRenderTarget = true;
        const float exposureValue = std::max(m_exposure, 0.0001f);
        for (auto& channel : exposureDesc.clearColor) channel = exposureValue;
        exposureDesc.name = "Temporal.Exposure";
        exposure = graph.CreateTexture(exposureDesc);
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned) exposure = graph.Write(exposure);
        graph.AddPass("Temporal.Exposure", {{exposure, RHIResourceState::RenderTarget,
            explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState}},
            [exposure, resources = context.resources, value = exposureValue](const auto& execute)
        {
            const RHITextureHandle colors[]{execute.ResolveHandle(exposure)};
            const auto targets = resources->CreateRenderTargets(colors, nullptr);
            if (!targets.IsValid()) throw std::runtime_error("Temporal exposure target creation failed.");
            const float clear[]{value, value, value, value};
            execute.encoder->ClearRenderTargets(targets, clear);
        });
    }
    ce::dynamic_array<EnhancedRenderGraph::RGPassUsage> uses;
    uses.reserve(m_inputs.size() + 1);
    for (const auto input : m_inputs)
        uses.push_back({input, RHIResourceState::ShaderResource,
            explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState});
    uses.push_back({m_output, RHIResourceState::UnorderedAccess,
        explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState});
    if (exposure.IsValid()) uses.push_back({exposure, RHIResourceState::ShaderResource,
        explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState});
    // Snapshot binding state: another view may declare through this pass before
    // this graph executes. No callback reads the pass's mutable members.
    graph.AddPass(GetName(), uses, [host = m_host, applied = m_applied,
        inputs = m_inputs, output = m_output, exposure, frame = context.temporalFrame](const auto& execute)
    {
        TemporalUpscaleInputs packet;
        packet.frame = frame;
        packet.color = execute.ResolveHandle(inputs[0]);
        packet.depth = execute.ResolveHandle(inputs[1]);
        packet.motionVectors = execute.ResolveHandle(inputs[2]);
        packet.reactiveMask = execute.ResolveHandle(inputs[3]);
        packet.transparencyMask = execute.ResolveHandle(inputs[4]);
        packet.responsiveMask = execute.ResolveHandle(inputs[5]);
        packet.output = execute.ResolveHandle(output);
        if (exposure.IsValid()) packet.exposure = execute.ResolveHandle(exposure);
        if (!host->Evaluate(packet, *execute.encoder).IsSuccess())
            throw std::runtime_error("Temporal upscale recording failed; discard this frame and rebuild at fallback extent.");
        *applied = true;
    });
}
