#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/RHIShaderCompiler.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#if defined(_DEBUG)
#include <crtdbg.h>
namespace {
thread_local bool observing = false;
thread_local unsigned allocations = 0, frees = 0;
int AllocationHook(int kind, void*, size_t, int, long, const unsigned char*, int) {
    if(observing && (kind==_HOOK_ALLOC || kind==_HOOK_REALLOC)) ++allocations;
    if(observing && kind==_HOOK_FREE) ++frees;
    return 1;
}
struct AllocationScope {
    _CRT_ALLOC_HOOK previous;
    AllocationScope():previous(_CrtSetAllocHook(AllocationHook)) { allocations=frees=0; observing=true; }
    ~AllocationScope() { observing=false; _CrtSetAllocHook(previous); }
};
}
#endif

static void Check(bool value,const char* name) { if(!value)throw std::runtime_error(name); std::cout<<"CHECK "<<name<<'\n'; }
int main(int argc,char** argv) try {
    Check(argc==3,"repository and private output arguments");
    const auto repo=std::filesystem::absolute(argv[1]),output=std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(output);
    auto* paths=(&ce::paths::get_state()); paths->base_project_path=repo/"Dynamic_CPP";
    paths->shader_source_path=repo/"Dynamic_CPP/Assets/Shaders"; paths->cache_root=output/"cache"; paths->asset_authoring_enabled=true;
    const auto source=output/"mesh.slang";
    { std::ofstream file(source); file<<R"(
struct Vertex { float4 position : SV_Position; };
[outputtopology("triangle")][numthreads(1,1,1)]
void MS(out vertices Vertex vertices[3],out indices uint3 triangles[1]) {
 SetMeshOutputCounts(3,1);
 vertices[0].position=float4(-.5,-.5,0,1);vertices[1].position=float4(0,.5,0,1);vertices[2].position=float4(.5,-.5,0,1);
 triangles[0]=uint3(0,1,2);
}
float4 PS(Vertex vertex):SV_Target0 { return float4(1,0,0,1); }
)"; }
    std::string error;RHIShaderCompiler::VerifiedShader ms,ps;
    Check(RHIShaderCompiler::VerifyFile(source.string(),"MS","ms_6_5",RHIShaderBinary::Dxil,{},ms,error),error.c_str());
    Check(RHIShaderCompiler::VerifyFile(source.string(),"PS","ps_6_0",RHIShaderBinary::Dxil,{},ps,error),error.c_str());
    thread_pool threads;job_scheduler jobs(threads);jobs.start(1);
    DX12DeviceResources device;Check(device.Initialize(64,64,error),error.c_str());
    Check(device.GetMeshShaderCapabilities().meshShader,"native mesh shader capability");
    DX12RootSignatureCache roots;DX12PSOManager pipelines(jobs);
    Check(roots.Initialize(&device,error)&&pipelines.Initialize(&device,(output/"native.cache").wstring(),error),error.c_str());
    const auto layout=roots.GetOrCreate({{},{}},error);Check(layout.IsValid(),"mesh root signature");
    RHIMeshPipelineDesc desc;desc.layout=layout;desc.msBytecode=ms.bytecode.Data();desc.msSize=ms.bytecode.Size();
    desc.psBytecode=ps.bytecode.Data();desc.psSize=ps.bytecode.Size();desc.numRenderTargets=1;desc.rtvFormats[0]=RHIFormat::RGBA8Unorm;
    desc.cullMode=RHICullMode::Back;desc.depthEnable=false;desc.depthWriteMask=RHIDepthWrite::Zero;desc.dsvFormat=RHIFormat::Unknown;
    std::promise<void> release;auto gate=release.get_future().share();std::atomic<bool> entered{};
    auto blocker=jobs.submit([&]{entered.store(true);entered.notify_all();gate.wait();});
    entered.wait(false);
    std::vector<std::byte> mesh(desc.msSize),pixel(desc.psSize);
    std::memcpy(mesh.data(),desc.msBytecode,mesh.size());std::memcpy(pixel.data(),desc.psBytecode,pixel.size());
    auto temporary=desc;temporary.msBytecode=mesh.data();temporary.psBytecode=pixel.data();
    RHIPipelineHandle handle;
    const auto state=pipelines.RequestMesh(temporary,handle,error);
    const auto duplicate=pipelines.RequestMesh(desc,handle,error);
    bool duplicatesPending=true;
#if defined(_DEBUG)
    unsigned pendingAllocations=0;
    {
        AllocationScope scope;
#endif
        for(unsigned i=0;i<4096;++i)
            duplicatesPending &= pipelines.RequestMesh(desc,handle,error)==RHIPipelineRequestState::Pending;
#if defined(_DEBUG)
        pendingAllocations=allocations;
    }
#endif
    std::fill(mesh.begin(),mesh.end(),std::byte{});std::fill(pixel.begin(),pixel.end(),std::byte{});
    release.set_value();blocker.wait();
    Check(duplicatesPending,"blocked worker keeps duplicate pending requests nonblocking and deduplicated");
#if defined(_DEBUG)
    Check(pendingAllocations==0,"4096 pending cache hits allocate zero on calling thread");
#endif
    Check(state==RHIPipelineRequestState::Pending&&duplicate==RHIPipelineRequestState::Pending&&pipelines.GetStats().m_asyncSubmissions==1,
        "nonblocking mesh admission deduplicates and owns borrowed shader bytes");
    const auto ready=[&]{
        for(unsigned i=0;i<30000;++i){const auto s=pipelines.RequestMesh(desc,handle,error);if(s==RHIPipelineRequestState::Failed)throw std::runtime_error(error);
            if(s==RHIPipelineRequestState::Ready)return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        throw std::runtime_error("mesh preparation timeout");
    };
    ready();Check(handle.IsValid()&&pipelines.GetStats().m_asyncWorkerExecutions>0,"worker compilation with owner handle publication");
    const auto first=handle;
    const auto compiled=pipelines.GetStats().compiles;
    bool sameReady=true;
#if defined(_DEBUG)
    unsigned readyAllocations=0;
    {
        AllocationScope scope;
#endif
        for(unsigned i=0;i<4096;++i)
            sameReady &= pipelines.RequestMesh(desc,handle,error)==RHIPipelineRequestState::Ready && handle==first;
#if defined(_DEBUG)
        readyAllocations=allocations;
    }
    Check(readyAllocations==0,"4096 completed cache hits allocate zero on calling thread");
#endif
    Check(sameReady && pipelines.GetStats().compiles==compiled,"completed cache hits preserve identity and compile count");
    // These test PSOs were never submitted to the GPU. Synthetic completion
    // points exercise retirement policy without claiming actual fence progress.
    bool invalidated=false;
#if defined(_DEBUG)
    unsigned invalidationFrees=0;
    {
        AllocationScope scope;
#endif
        invalidated=pipelines.InvalidatePipeline(first,{7});
#if defined(_DEBUG)
        invalidationFrees=frees;
    }
    Check(invalidationFrees==1,"single invalidation immediately frees its tracking node");
#endif
    Check(invalidated && !device.Resolve(first).IsValid(),"invalidation removes the old non-owning pipeline handle");
    Check(pipelines.GetStats().retiredPipelines==1 && pipelines.CollectRetiredPipelines({6})==0 &&
          pipelines.CollectRetiredPipelines({7})==1,"native PSO ownership waits for its retirement completion");
    ready();Check(handle!=first,"recreated pipeline has a fresh generation");
    const auto old=handle;desc.cullMode=RHICullMode::None;
    Check(pipelines.RequestMesh(desc,handle,error)==RHIPipelineRequestState::Pending,"second native mesh request admitted");
    pipelines.InvalidatePipelines();ready();Check(handle.IsValid()&&handle!=old,"invalidation drains pending mesh work before re-admission");
    Check(!device.Resolve(old).IsValid() && pipelines.CollectRetiredPipelines({UINT64_MAX})==0 &&
          pipelines.GetStats().retiredPipelines==1,"completion-zero quarantine survives all ordinary collections");
    auto invalid=desc;invalid.msBytecode=nullptr;
    Check(pipelines.RequestMesh(invalid,handle,error)==RHIPipelineRequestState::Failed&&!handle.IsValid(),"invalid mesh request publishes no handle");
    desc.cullMode=RHICullMode::Front;
    Check(pipelines.RequestMesh(desc,handle,error)==RHIPipelineRequestState::Pending,
        "shutdown starts with an owned pending compile");
    pipelines.Shutdown();roots.Shutdown();device.Shutdown();jobs.shutdown();
    std::cout<<"EDITOR_MESH_PSO_OK pendingHits=4096 readyHits=4096 ownership=1 quarantine=1";
#if defined(_DEBUG)
    std::cout<<" allocationChecks=2 nodeFreeChecks=1";
#else
    std::cout<<" allocationChecks=0 nodeFreeChecks=0";
#endif
    std::cout<<"\n";return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
