#include "material_probe_compiler.h"

using MaterialProbe::CompilerRuntime;

int wmain(int argc, wchar_t** argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("Expected repository root");
        }
        const std::filesystem::path root = std::filesystem::absolute(argv[1]);
        const auto shaders = root / "Dynamic_CPP" / "Assets" / "Shaders" / "DefaultPassShader";
        CompilerRuntime compiler;
        compiler.Initialize(root);
        unsigned compiled = 0;
        unsigned rejected = 0;
        std::string error;
        const auto requireCompile = [&](const char* file, const char* entry, SlangStage stage, bool spirv,
                                        const std::vector<std::string>& defines) {
            if (!compiler.Compile(shaders / file, entry, stage, spirv, defines, error))
            {
                throw std::runtime_error(std::string(file) + "/" + entry + (spirv ? "/SPIR-V: " : "/DXIL: ") + error);
            }
            ++compiled;
        };

        for (bool spirv : {false, true})
        {
            for (const char* file : {"GBuffer.slang", "ForwardShade.slang", "ForwardWater.slang", "ForwardWind.slang"})
            {
                for (bool packed : {false, true})
                {
                    for (unsigned quality : {0u, 1u})
                    {
                        std::vector<std::string> defines{"TILE_SIZE=16", "MAX_LIGHTS_PER_TILE=128",
                                                         "SHADING_QUALITY=" + std::to_string(quality)};
                        if (packed)
                        {
                            defines.emplace_back("MODEL_VERTEX_LAYOUT=1");
                            defines.emplace_back("MODEL_VERTEX_UV1=1");
                            defines.emplace_back("MODEL_VERTEX_COLOR=1");
                            defines.emplace_back("MODEL_VERTEX_SKINNING=1");
                        }
                        requireCompile(file, "VSMain", SLANG_STAGE_VERTEX, spirv, defines);
                        requireCompile(file, "PSMain", SLANG_STAGE_FRAGMENT, spirv, defines);
                    }
                }
            }
            requireCompile("Deferred.slang", "VSMain", SLANG_STAGE_VERTEX, spirv, {});
            requireCompile("Deferred.slang", "PSMain", SLANG_STAGE_FRAGMENT, spirv, {});
            requireCompile("ForwardShade.slang", "PSMain", SLANG_STAGE_FRAGMENT, spirv, {"REFERENCE_PATH=1"});

            for (const char* mask : {"0x4000", "0x40000000"})
            {
                if (compiler.Compile(shaders / "GBuffer.slang", "PSMain", SLANG_STAGE_FRAGMENT, spirv,
                                     {std::string("LX_MATERIAL_FEATURE_MASK=") + mask}, error) ||
                    error.find("supports only known features") == std::string::npos)
                {
                    throw std::runtime_error("Unsupported mask was not rejected: " + std::string(mask) + "\n" + error);
                }
                ++rejected;
            }
            for (const char* file : {"GBuffer.slang", "ForwardShade.slang", "Deferred.slang"})
            {
                for (const char* mask : {"0x007F", "0x00FF", "0x017F", "0x027F", "0x047F"})
                {
                    if (compiler.Compile(shaders / file, "PSMain", SLANG_STAGE_FRAGMENT, spirv,
                                         {std::string("LX_MATERIAL_FEATURE_MASK=") + mask}, error) ||
                        error.find("Fixed Standard route cannot bind Principled Specular/IOR") == std::string::npos)
                    {
                        throw std::runtime_error(std::string(file) + " silently accepted unbound features: " + mask +
                                                 "\n" + error);
                    }
                    ++rejected;
                }
                for (const char* mask : {"0x087F", "0x107F", "0x207F", "0x2000"})
                {
                    const char* diagnostic = std::string(mask) == "0x2000"
                                                 ? "Fixed Standard route cannot bind Special features"
                                                 : "Fixed Standard route cannot bind Principled Specular/IOR";
                    if (compiler.Compile(shaders / file, "PSMain", SLANG_STAGE_FRAGMENT, spirv,
                                         {std::string("LX_MATERIAL_FEATURE_MASK=") + mask}, error) ||
                        error.find(diagnostic) == std::string::npos)
                    {
                        throw std::runtime_error(std::string(file) + " silently accepted Special features: " + mask +
                                                 "\n" + error);
                    }
                    ++rejected;
                }
            }
        }
        std::cout << "MATERIAL_ABI_COMPILE_OK compiled=" << compiled << " rejected=" << rejected << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
