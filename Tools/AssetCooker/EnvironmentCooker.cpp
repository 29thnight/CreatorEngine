#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Sha256.h"
#else
import Creator.Core.Sha256;
#endif
#include "../../Editor/EngineEntry/Assets/Textures/TextureSourceProcessing.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Platform.Windows.h"
#else
import Creator.Platform.Windows;
#endif
#include "Assets/CookedEnvironment.h"
#include "RHI/DX12/EnhancedIBLGenerator.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/RHISubmissionThread.h"
#include "Texture.h"
#include "RHI/RHIShaderCompiler.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
void EnvironmentRequire(bool valid,const std::string& error)
{
    if (!valid) throw std::runtime_error(error);
}
double EnvironmentElapsed(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
}
int wmain(int argc,wchar_t** argv)
{
    Authoring::InstallTextureSourceProcessing();
    struct TextureSourceScope
    {
        ~TextureSourceScope() { Authoring::UninstallTextureSourceProcessing(); }
    } textureSourceScope;

    if (argc<4 || argc>5) { std::cerr<<"EnvironmentCooker <repo> <source-HDR/EXR> <output.ceibl> [decoded-RGBA32F|--check-cache|--check-shaders]\n"; return 2; }
    const ce::platform::windows::apartment_scope apartmentScope{ ce::platform::windows::apartment_model::mta };
    const HRESULT com = apartmentScope.result;
    try
    {
        EnvironmentRequire(SUCCEEDED(com),"COM initialization failed");
        const auto root=std::filesystem::absolute(argv[1]), source=std::filesystem::absolute(argv[2]), output=std::filesystem::absolute(argv[3]);
        auto* paths=(&ce::paths::get_state());
        paths->base_project_path=root/"Dynamic_CPP"; paths->shader_source_path=root/"Dynamic_CPP/Assets/Shaders";
        paths->cache_root=root/"Build/Obj/EnvironmentCooker/ShaderCache";
        paths->asset_authoring_enabled=true;
        constexpr uint32_t cube=512,brdf=512;
        std::string error;
        assets::EnvironmentIdentity identity;
        EnvironmentRequire(assets::EnvironmentSourceIdentity(source,identity.source,error),error);
        EnvironmentRequire(assets::EnvironmentRecipeIdentity(paths->shader_source_path/"DefaultPassShader",cube,brdf,identity.recipe,error),error);
        if(argc==5 && std::wstring_view(argv[4])==L"--check-shaders")
        {
            RHIShaderCompileOptions options;options.strictMath=true;
            for(const auto backend:{RHIShaderBinary::Dxil,RHIShaderBinary::SpirV})
            {
                RHIShaderCompiler::VerifiedShader shader;
                EnvironmentRequire(RHIShaderCompiler::VerifyFile(
                    (paths->shader_source_path/"DefaultPassShader/IblSceneImportance.slang").string(),
                    "CSMain","cs_6_0",backend,{},shader,error,options),error);
                options.strictMath=false;
                for (const auto* stage : {"IblRectToCube.slang", "IblSourceCopy.slang", "IblCubeDownsample.slang",
                     "IblIrradiance.slang", "IblPrefilter.slang", "IblImportanceSamples.slang"})
                    EnvironmentRequire(RHIShaderCompiler::VerifyFile(
                        (paths->shader_source_path/"DefaultPassShader"/stage).string(),
                        "PSMain","ps_5_0",backend,{},shader,error,options),error);
                options.strictMath=true;
            }
            std::cout<<"ENVIRONMENT_IMPORTANCE_SHADER_OK DXIL=7 SPIRV=7\n";
            return 0;
        }
        assets::CookedEnvironment warm;
        const auto cacheStart=std::chrono::steady_clock::now();
        if (assets::ReadCookedEnvironment(output,warm,error,&identity))
        {
            std::cout<<"ENVIRONMENT_COOK_CACHE_HIT ms="<<EnvironmentElapsed(cacheStart)<<" key="<<assets::EnvironmentCacheName(identity)<<'\n';
            return 0;
        }
        if (argc==5 && std::wstring_view(argv[4])==L"--check-cache")
        { std::cout<<"ENVIRONMENT_COOK_CACHE_MISS\n"; return 3; }
        error.clear();
        const auto start=std::chrono::steady_clock::now();
        own::shared_owner<const Texture> texture;
        if (argc==5)
        {
            std::ifstream input(argv[4],std::ios::binary);
            uint32_t size[2]{}; input.read(reinterpret_cast<char*>(size),sizeof(size));
            EnvironmentRequire(bool(input) && size[0]>0 && size[0]<=16384 && size[1]>0 && size[1]<=8192,"Invalid decoded EXR dimensions");
            auto image=TextureImage::Allocate(RHIFormat::RGBA32Float,size[0],size[1],1,1);
            const auto& slice=*image.Find(0,0);
            input.read(reinterpret_cast<char*>(image.MutablePixelsAt(slice)),slice.slicePitch);
            EnvironmentRequire(bool(input) && input.peek()==std::char_traits<char>::eof(),"Decoded EXR pixel bytes differ");
            texture=Texture::CreateSharedFromImage("EnvironmentCookSource",std::move(image));
        }
        else texture=Texture::LoadSharedFromPath(source);
        EnvironmentRequire(!!texture,"Environment decode failed");
        DX12DeviceResources device;
        EnvironmentRequire(device.Initialize(64,64,error),error);
        DX12RootSignatureCache roots; DX12PSOManager pipelines; DX12TextureCache textures;
        EnvironmentRequire(roots.Initialize(&device,error) && pipelines.Initialize(&device,L"",error) && textures.Initialize(&device,error),error);
        EnhancedFrameContext context;
        context.resources=&device; context.rootSignatures=&roots; context.psoManager=&pipelines; context.textureCache=&textures;
        EnhancedIBLGenerator generator;
        EnvironmentRequire(generator.Initialize(context,error),error);
        EnvironmentRequire(device.BeginFrame(error),error);
        const auto uploaded=textures.GetOrUpload(&*texture.borrow(),texture->NonRehydratableImage(),error);
        EnvironmentRequire(error.empty() && uploaded.IsValid() && !uploaded.isCube,"Environment source upload: "+error);
        EnvironmentRequire(generator.Generate(context,uploaded.handle,uploaded.format,cube,brdf,error),error);
        EnvironmentRequire(generator.QueueCookedCapture(output,identity,error),error);
        EnvironmentRequire(device.EndFrame(error),error);
        EnvironmentRequire(GetRHISubmissionThread().DrainSubmissions(&device,error),error);
        device.WaitForGpu();
        const auto generationMs=EnvironmentElapsed(start);
        do
        {
            EnvironmentRequire(generator.FinishCookedCapture(device.GetCompletedFenceValue(),error),error);
            if (generator.HasPendingCookedCapture()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (generator.HasPendingCookedCapture());
        assets::CookedEnvironment verify;
        EnvironmentRequire(assets::ReadCookedEnvironment(output,verify,error,&identity),error);
        EnvironmentRequire(verify.importancePersisted && verify.source.IsValid(),"Cook must persist Scene importance maps and decoded source");
        // Re-upload every mip/face through the product texture cache and require
        // a second GPU readback to produce byte-identical cooked pixels.
        auto roundtrip=output; roundtrip += ".roundtrip";
        const auto uploadStart=std::chrono::steady_clock::now();
        EnvironmentRequire(device.BeginFrame(error),error);
        EnvironmentRequire(generator.InstallCooked(context,std::move(verify),error),error);
        EnvironmentRequire(generator.QueueCookedCapture(roundtrip,identity,error),error);
        EnvironmentRequire(device.EndFrame(error),error);
        EnvironmentRequire(GetRHISubmissionThread().DrainSubmissions(&device,error),error);
        device.WaitForGpu();
        const auto uploadMs=EnvironmentElapsed(uploadStart);
        do { EnvironmentRequire(generator.FinishCookedCapture(device.GetCompletedFenceValue(),error),error);
            if (generator.HasPendingCookedCapture()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while(generator.HasPendingCookedCapture());
        ce::hash::sha256_digest original{},copy{};
        EnvironmentRequire(assets::EnvironmentSourceIdentity(output,original,error) &&
            assets::EnvironmentSourceIdentity(roundtrip,copy,error) && original==copy,"Environment GPU upload roundtrip differs: "+error);
        std::filesystem::remove(roundtrip);
        EnvironmentRequire(device.DrainDebugMessages(error)==0,"Environment GPU validation: "+error);
        const auto bytes=std::filesystem::file_size(output);
        generator.Shutdown(); textures.Shutdown(); pipelines.Shutdown(); roots.Shutdown(); device.Shutdown();
        std::cout<<"ENVIRONMENT_COOK_OK generated_ms="<<generationMs<<" upload_readback_ms="<<uploadMs
            <<" total_ms="<<EnvironmentElapsed(start)<<" bytes="<<bytes<<" maps=8 roundtrip=exact importancePersisted=true validation=0 key="<<assets::EnvironmentCacheName(identity)<<'\n';
        return 0;
    }
    catch(const std::exception& failure)
    { std::cerr<<"ENVIRONMENT_COOK_FAILED "<<failure.what()<<'\n'; return 1; }
}
