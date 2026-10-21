#include "RHI/RHIShaderCompiler.h"
#include "PathFinder.h"
#include "../../Engine/Utility_Framework/StandardLibrary.h"

int main(int argc,char** argv) try
{
    if(argc!=3) throw std::runtime_error("probe <repo> <generated-source>");
    const auto repo=std::filesystem::absolute(argv[1]);
    auto* paths=InternalPath::GetInstance();
    paths->BaseProjectPath=repo/"Dynamic_CPP";
    paths->ShaderSourcePath=repo/"Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled=true;
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
