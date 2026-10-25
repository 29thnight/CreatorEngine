#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "RHI/RHIShaderCompiler.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

int main(int argc,char** argv) try
{
    if(argc!=3) throw std::runtime_error("probe <repo> <generated-source>");
    const auto repo=std::filesystem::absolute(argv[1]);
    auto* paths=(&ce::paths::get_state());
    paths->base_project_path=repo/"Dynamic_CPP";
    paths->shader_source_path=repo/"Dynamic_CPP/Assets/Shaders";
    paths->asset_authoring_enabled=true;
    RHIShaderCompileOptions options;
    options.strictMath=true; options.fineDerivatives=true;
    options.quadOperations=true; options.bufferInt64Atomics=true;
    options.includeDirectories.push_back(repo/"Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    RHIShaderPermutation permutation;std::string error;
    if(!permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT",error))throw std::runtime_error(error);
    RHIShaderCompiler::ModuleReuseScope reuse;
    for(const auto* entry:{"LXSceneColorPS","LXSceneTemporalPS","LXSceneCachedTemporalPS","LXSceneGBufferPS"})
    {
        RHIShaderCompiler::VerifiedShader shader;
        if(!RHIShaderCompiler::VerifyFile(argv[2],entry,"ps_6_0",RHIShaderBinary::Dxil,permutation,shader,error,options))
            throw std::runtime_error(error);
        std::cout<<"TIMING_ENTRY "<<entry<<" bytes="<<shader.bytecode.Size()<<'\n';
    }
    std::cout<<"MATERIAL_COMPILE_TIMING_OK\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
