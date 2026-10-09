#include "EnhancedTemporalInputsPass.h"
#include "../../../RHI/RHIShaderCompiler.h"
#include "../../../RHI/RHIEncoder.h"
#include <bit>
#include <stdexcept>

bool EnhancedTemporalInputsPass::Initialize(const EnhancedFrameContext& context, std::string& error)
{
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0),RHILayout::SrvTable(1,0),RHILayout::UavTable(4,0)};
    RHIPipelineLayoutDesc rootDesc; rootDesc.params=params;
    auto root=context.rootSignatures->GetOrCreate(rootDesc,error);
    if (!root.IsValid()) return false;
    RHIShaderBlob shader;
    if (!RHIShaderCompiler::CompileFile("TemporalInputs.slang","CSMain","cs_5_0",shader,error)) return false;
    RHIComputePipelineDesc desc; desc.layout=root; desc.csBytecode=shader.Data();desc.csSize=shader.Size();
    m_pipeline=context.psoManager->GetOrCreateCompute(desc,error);
    return m_pipeline.IsValid();
}
void EnhancedTemporalInputsPass::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
{
    const bool versioned=graph.GetSchedulingMode()==RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess=graph.GetSchedulingMode()!=RGSchedulingMode::DeclarationOrder;
    const auto read=explicitAccess?RGAccessMode::Read:RGAccessMode::LegacyState;
    const auto write=explicitAccess?RGAccessMode::Write:RGAccessMode::LegacyState;
    for (unsigned i=0;i<5;++i)
    {
        RGTextureDesc desc;desc.width=context.width;desc.height=context.height;
        desc.format=i==0?RHIFormat::RG16Float:i==4?RHIFormat::D32Float:RHIFormat::R16Float;
        desc.allowRenderTarget=i<4;desc.allowUnorderedAccess=i<4;desc.allowDepthStencil=i==4;
        const char* names[]{"Temporal.Motion","Temporal.Reactive","Temporal.Transparency","Temporal.Responsive","Temporal.Depth"};
        desc.name=names[i];m_outputs[i]=graph.CreateTexture(desc);
        if (versioned) m_outputs[i]=graph.Write(m_outputs[i]);
    }
    const auto outputs=m_outputs;
    const auto depth=m_depth;
    graph.AddPass("Temporal.DepthCopy",{{depth,RHIResourceState::CopySource,read},{outputs[4],RHIResourceState::CopyDest,write}},
        [depth,outputs](const auto& execute){execute.encoder->CopyTexture(execute.ResolveHandle(outputs[4]),execute.ResolveHandle(depth));});
    struct Constants
    {
        math::matrix4x4 inverseProjection,inverseView,currentProjection,previousProjection;
        uint32_t width,height,reset,pad;
    } constants{};
    constants.inverseProjection=math::transpose(context.camera->inverseProjection);
    constants.inverseView=math::transpose(context.camera->inverseView);
    const auto current=std::bit_cast<math::matrix4x4>(context.temporalFrame.camera.viewMatrix)*
        std::bit_cast<math::matrix4x4>(context.temporalFrame.camera.projectionMatrix);
    constants.currentProjection=math::transpose(current);
    constants.previousProjection=math::transpose(current*std::bit_cast<math::matrix4x4>(context.temporalFrame.camera.clipToPreviousClip));
    constants.width=context.width;constants.height=context.height;constants.reset=context.temporalFrame.reset;
    auto* resources=context.resources;
    auto pipeline=m_pipeline;
    const auto pass=graph.AddPass("Temporal.CameraMotion",{
        {depth,RHIResourceState::ShaderResource,read},
        {outputs[0],RHIResourceState::UnorderedAccess,write},{outputs[1],RHIResourceState::UnorderedAccess,write},
        {outputs[2],RHIResourceState::UnorderedAccess,write},{outputs[3],RHIResourceState::UnorderedAccess,write}},
        [resources,pipeline,depth,outputs,constants](const auto& execute){
            const auto cb=resources->UploadConstants(&constants,sizeof(constants));
            const RHIBindingDesc srv[]{RHIBindingDesc::SrvDepth(execute.ResolveHandle(depth))};
            RHIBindingDesc uav[4];
            for(unsigned i=0;i<4;++i) uav[i]=RHIBindingDesc::Uav2D(execute.ResolveHandle(outputs[i]),i==0?RHIFormat::RG16Float:RHIFormat::R16Float);
            const auto inputs=resources->CreateBindings(srv), targets=resources->CreateBindings(uav);
            if(!cb.IsValid()||!inputs.IsValid()||!targets.IsValid()) throw std::runtime_error("Temporal camera input bindings failed.");
            auto& encoder=*execute.encoder;encoder.SetPipeline(RHIBindPoint::Compute,pipeline);
            encoder.SetConstantBuffer(RHIBindPoint::Compute,0,cb);encoder.SetBindings(RHIBindPoint::Compute,1,inputs);
            encoder.SetBindings(RHIBindPoint::Compute,2,targets);encoder.Dispatch((constants.width+7)/8,(constants.height+7)/8,1);
        }, true);
    graph.DeclareComputeCompatible(pass);
}
