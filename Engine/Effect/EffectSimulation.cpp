#include "EffectSimulation.h"
#include "../RenderEngine/RHI/IRenderDeviceServices.h"

namespace ce::effects
{
bool AddEffectSimulationPass(EnhancedRenderGraph& graph, RGHandle particles, IRenderDeviceServices& device,
    RHIPipelineHandle pipeline, EffectGpuPhase phase, const EffectGpuStep& step, std::string& error)
{
    const auto finite3 = [](Float3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); };
    RGResourceDescription resource;
    if (!step.capacity || step.capacity>EffectGpuMaxParticles || (step.flags&~3u) || !pipeline.IsValid() ||
        (phase!=EffectGpuPhase::Initialize && phase!=EffectGpuPhase::Update) ||
        !std::isfinite(step.deltaSeconds) || step.deltaSeconds<0 || !std::isfinite(step.lifetime) || step.lifetime<=0 ||
        !std::isfinite(step.size) || step.size<0 || !std::isfinite(step.drag) || step.drag<0 ||
        !finite3(step.velocity) || !finite3(step.acceleration) ||
        !graph.DescribeResource(particles,resource) || resource.kind!=RGResourceKind::Buffer ||
        resource.bytes != std::uint64_t{step.capacity}*sizeof(EffectGpuParticle)) {
        error="Invalid minimal Effect simulation parameters, pipeline or particle buffer extent"; return false;
    }
    graph.AddPass(phase==EffectGpuPhase::Initialize ? "Effect.Initialize" : "Effect.Update",
        {{particles,RHIResourceState::UnorderedAccess}},
        [particles,&device,pipeline,step](const EnhancedRenderGraph::ExecuteContext& context) {
            const auto buffer=context.graph->ResolveBufferHandle(particles);
            const auto binding=RHIBindingDesc::UavBuffer(buffer,step.capacity,sizeof(EffectGpuParticle));
            const auto table=device.CreateBindings({&binding,1});
            const auto constants=device.UploadConstants(&step,sizeof(step));
            if (!table.IsValid() || !constants.IsValid()) throw std::runtime_error("Effect simulation binding/upload allocation failed");
            context.encoder->SetPipeline(RHIBindPoint::Compute,pipeline);
            context.encoder->SetBindings(RHIBindPoint::Compute,0,table);
            context.encoder->SetConstantBuffer(RHIBindPoint::Compute,1,constants);
            context.encoder->Dispatch((step.capacity+63)/64,1,1);
        },true);
    return true;
}
}
