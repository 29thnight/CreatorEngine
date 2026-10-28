#include "../../Engine/Effect/EffectSimulation.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12CommandListPool.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHISubmissionThread.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include <iostream>

using namespace ce::effects;
namespace
{
void Check(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
bool Near(float a,float b) { return std::isfinite(a)&&std::abs(a-b)<=2.e-5f*std::max(1.f,std::abs(b)); }
}
int main(int argc,char** argv)
{
    try {
        Check(argc==3 || argc==4,"Absolute shader and cache paths required; optional --bad-oracle");
        const bool badOracle=argc==4 && std::string_view(argv[3])=="--bad-oracle";
        std::cout<<std::unitbuf;
        const auto repo=std::filesystem::path(argv[1]).parent_path().parent_path().parent_path().parent_path();
        auto* paths=&ce::paths::get_state();
        paths->base_project_path=repo/"Dynamic_CPP"; paths->shader_source_path=repo/"Dynamic_CPP/Assets/Shaders";
        paths->cache_root=argv[2]; paths->asset_authoring_enabled=false;
        ce::get_job_scheduler().start(2);
        std::string error;
        DX12DeviceResources device;
        Check(device.Initialize(8,8,error),error);
        DX12RootSignatureCache roots; DX12PSOManager pipelines; DX12CommandListPool pool;
        Check(roots.Initialize(&device,error)&&pipelines.Initialize(&device,L"",error)&&pool.Initialize(device,1,DX12DeviceResources::kFrameCount,error),error);
        const RHIPipelineLayoutParam parameters[]{RHILayout::UavBufferTable(1),RHILayout::Cbv(0)};
        const auto layout=roots.GetOrCreate({parameters,{}},error);
        RHIPipelineHandle shaders[2];
        unsigned shaderIndex{};
        for (const auto* entry : {"Initialize","Update"}) {
            RHIShaderCompiler::VerifiedShader shader;
            RHIShaderCompileOptions options; options.strictMath=true;
            Check(RHIShaderCompiler::VerifyFile(argv[1],entry,"cs_6_0",RHIShaderBinary::Dxil,{},shader,error,options),error);
            RHIComputePipelineDesc desc; desc.layout=layout; desc.csBytecode=shader.bytecode.Data(); desc.csSize=shader.bytecode.Size();
            shaders[shaderIndex]=pipelines.GetOrCreateCompute(desc,error);
            Check(shaders[shaderIndex++].IsValid(),error);
        }
        unsigned cases{},comparisons{},barriers{};
        for (const auto capacity : {1u,65u,4096u}) {
            RHIBufferHandle buffer;
            RHIBufferDesc bufferDesc; bufferDesc.bytes=std::uint64_t{capacity}*sizeof(EffectGpuParticle);
            bufferDesc.allowUnorderedAccess=true; bufferDesc.debugName=L"Effect.Minimal.Particles";
            Check(device.CreateBuffer(bufferDesc,buffer,error),error);
            RHIResourceState state=RHIResourceState::Common;
            RHIReadback readback;
            Check(device.CreateBufferReadback(bufferDesc.bytes,readback,error),error);
            for (const auto requested : {0u,1u,capacity,capacity+17}) for (const auto mode : {0u,1u,2u,3u,4u}) {
                const auto flags=mode==4 ? 0u : mode;
                EffectGpuStep step; step.capacity=capacity; step.requested=requested; step.flags=flags;
                step.deltaSeconds=mode==4 ? 0.f : .0625f; step.lifetime=flags==3 ? .125f : 1.f;
                step.velocity={2,3,-1}; step.acceleration={0,-9.8f,1}; step.drag=.8f; step.size=.4f;
                Check(device.BeginFrame(error),error); pool.BeginFrame(0);
                {
                    EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(device));
                    const auto particles=graph.ImportBuffer(buffer,state,"Effect.Particles",&state);
                    auto invalid=step; invalid.capacity++;
                    Check(!AddEffectSimulationPass(graph,particles,device,shaders[0],EffectGpuPhase::Initialize,invalid,error),"Bad buffer extent accepted");
                    error.clear();
                    Check(AddEffectSimulationPass(graph,particles,device,shaders[0],EffectGpuPhase::Initialize,step,error),error);
                    for (unsigned tick=0;tick<3;++tick)
                        Check(AddEffectSimulationPass(graph,particles,device,shaders[1],EffectGpuPhase::Update,step,error),error);
                    graph.AddPass("Effect.Readback",{{particles,RHIResourceState::CopySource}},
                        [particles,&readback](const auto& context) {
                            context.encoder->CopyBufferToReadback(readback,context.graph->ResolveBufferHandle(particles));
                        },true);
                    Check(graph.Compile(error),error);
                    RHIRecordedBatchDesc batchDesc; batchDesc.frameId=cases+1;
                    batchDesc.backendGeneration=GetRHISubmissionThread().GetOwnerGeneration(&device);
                    batchDesc.lifetimeToken=own::make_shared<unsigned>(cases+1);
                    RHIRecordedBatch batch; RHISubmissionTicket ticket;
                    Check(graph.RecordParallel(pool,1,batchDesc,batch,error)&&
                        GetRHISubmissionThread().EnqueueRecordedBatch(&device,device,std::move(batch),ticket,error)&&
                        device.EndFrame(error)&&GetRHISubmissionThread().Wait(ticket,error),error);
                    device.WaitForGpu(); // Both graph resources and readback remain owned until completion.
                    barriers+=graph.GetStats().barriersEmitted;
                }
                RHIReadbackImage image;
                Check(device.MapReadback(readback,image,error),error);
                Check(image.data.size()==bufferDesc.bytes,"Readback extent mismatch");
                const auto gravity=CompileBuiltinEffectModule({"gravity","ce.gravity",1,(flags&1)!=0,
                    {{"acceleration",ConstantInput{step.acceleration}}}});
                const auto drag=CompileBuiltinEffectModule({"drag","ce.drag",1,(flags&2)!=0,
                    {{"coefficient",ConstantInput{step.drag}}}});
                Check(gravity.module.has_value()&&drag.module.has_value(),"CPU oracle compile failed");
                for (unsigned i=0;i<capacity;++i) {
                    EffectGpuParticle actual;
                    std::memcpy(&actual,image.data.data()+std::size_t{i}*sizeof(actual),sizeof(actual));
                    ParticleValues expected; expected.initialized=63; expected.velocity=step.velocity;
                    expected.lifetime=step.lifetime; expected.size=step.size;
                    bool alive=i<std::min(requested,capacity);
                    for (unsigned tick=0;tick<3;++tick)
                        Check(StepEffectParticle({*gravity.module,*drag.module},expected,alive,{step.deltaSeconds}).empty(),"CPU oracle step failed");
                    if (badOracle && requested && i==0) expected.position.x+=1;
                    Check(actual.id==i && actual.reserved==0 && actual.alive==static_cast<unsigned>(alive),"GPU particle identity/alive mismatch");
                    const float observed[]{actual.position.x,actual.position.y,actual.position.z,actual.age,
                        actual.velocity.x,actual.velocity.y,actual.velocity.z,actual.lifetime,actual.size,
                        actual.color.r,actual.color.g,actual.color.b,actual.color.a};
                    const float reference[]{expected.position.x,expected.position.y,expected.position.z,expected.age,
                        expected.velocity.x,expected.velocity.y,expected.velocity.z,expected.lifetime,expected.size,1,1,1,1};
                    for (unsigned component=0;component<13;++component) {
                        Check(Near(observed[component],reference[component]),"GPU/CPU mismatch case="+std::to_string(cases)+" particle="+std::to_string(i)+" component="+std::to_string(component));
                        ++comparisons;
                    }
                }
                ++cases;
                std::cout<<"EFFECT_GPU_CASE_OK capacity="<<capacity<<" requested="<<requested<<" flags="<<flags<<'\n';
            }
            device.WaitForGpu(); device.ReleaseReadback(readback); device.ReleaseBuffer(buffer);
        }
        device.WaitForGpu(); pool.Shutdown(); pipelines.Shutdown(); roots.Shutdown(); device.Shutdown();
        std::string validation;
        Check(device.DrainDebugMessages(validation)==0,validation);
        Check(barriers>0,"No graph barriers recorded");
        ce::get_job_scheduler().shutdown();
        std::cout<<"EFFECT_SIMULATION_GPU_OK cases="<<cases<<" components="<<comparisons<<" barriers="<<barriers<<" validation=0\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; ce::get_job_scheduler().shutdown(); return 1; }
}
