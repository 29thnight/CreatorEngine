#include "MaterialPipelineSlot.h"
#include <algorithm>
namespace material_graph
{
namespace
{
bool Fail(std::vector<LX::LXMaterialDiagnostic>& diagnostics, std::string code, std::string message)
{
    diagnostics.push_back({std::move(code), std::move(message), {}});
    return false;
}
}
bool PipelineSlot::Publish(IRenderPipelineCache& cache, const VerifiedProduct& product, RHIShaderBinary backend,
                           const Capabilities& capabilities, const Budget& budget,
                           std::span<const ParameterOverride> parameters, std::span<const TextureBinding> textures,
                           const RHIGraphicsPipelineDesc& pipeline, RHICompletionPoint retireAfter,
                           std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    std::string error;
    std::vector<std::uint8_t> verifiedEnvelope;
    if (!WriteCookedProgram(product, budget, verifiedEnvelope, error))
        return Fail(diagnostics, "product.generation", error);
    const auto& program = product.program;
    auto candidate = std::make_shared<PipelineGeneration>();
    candidate->layout = product.layout;
    if (!SelectRoute(program, capabilities, budget, candidate->selection, diagnostics) ||
        candidate->selection.route != product.selection.route ||
        !PrepareResources(candidate->layout, parameters, textures, candidate->resources, diagnostics))
        return Fail(diagnostics, "product.route", "Verified route does not match installed renderer/bindings.");
    if (!pipeline.layout.IsValid() || pipeline.vsBytecode || pipeline.psBytecode || pipeline.vsSize ||
        pipeline.psSize || program.volume || (active_ && active_->generation == UINT64_MAX) ||
        (backend != RHIShaderBinary::Dxil && backend != RHIShaderBinary::SpirV))
        return Fail(
            diagnostics, "product.pipeline",
            "Graphics publication requires a surface-only generation; Volume needs a paired composition pipeline.");
    auto description = pipeline;
    if (std::ranges::count_if(
            product.targets,
            [&](const auto& target) { return target.binary == backend && target.profile.starts_with("vs_"); }) != 1 ||
        std::ranges::count_if(product.targets, [&](const auto& target) {
            return target.binary == backend && target.profile.starts_with("ps_");
        }) != 1)
    {
        return Fail(diagnostics, "product.pipeline", "A multi-pass product requires its owning Scene host.");
    }
    for (const auto& target : product.targets)
    {
        if (target.binary != backend || (!target.profile.starts_with("vs_") && !target.profile.starts_with("ps_")))
            continue;
        const std::string name = backend == RHIShaderBinary::Dxil ? "dxil" : "spirv";
        const auto shader = std::ranges::find_if(product.shaders, [&](const auto& artifact) {
            return artifact.backend == name && artifact.entryPoint == target.entry;
        });
        if (target.profile.starts_with("vs_"))
        {
            description.vsBytecode = shader->bytecode.data();
            description.vsSize = shader->bytecode.size();
        }
        else
        {
            description.psBytecode = shader->bytecode.data();
            description.psSize = shader->bytecode.size();
        }
    }
    candidate->semanticKey = program.semanticKey;
    candidate->generation = active_ ? active_->generation + 1 : 1;
    if (active_)
        retired_.reserve(retired_.size() + 1);
    LX::Runtime::GraphicsShaderDescription shader;
    if (!DescribeGraphicsShader(product, backend, {}, {}, shader, error) ||
        !candidate->pipeline.Create(cache, description, std::move(shader), error))
        return Fail(diagnostics, "product.pipeline", error.empty() ? "Material pipeline creation failed." : error);
    const auto previous = active_;
    active_ = std::move(candidate);
    if (previous)
        retired_.push_back({previous, retireAfter});
    if (previous && previous->pipeline.GetHandle() != active_->pipeline.GetHandle())
        cache.InvalidatePipeline(previous->pipeline.GetHandle(), retireAfter);
    return true;
}

void PipelineSlot::CollectRetired(RHICompletionPoint completed)
{
    std::erase_if(retired_, [&](const auto& retired) {
        // A zero fence means completion is unknown, as in IRenderPipelineCache.
        // Such owners remain live until this slot is destroyed after device idle.
        return retired.after.value != 0 && retired.after.value <= completed.value;
    });
}
} // namespace material_graph
