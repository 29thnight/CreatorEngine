#include "MaterialGraphSceneCompiler.h"

#include <algorithm>
#include <fstream>
#include <tuple>

namespace material_graph
{
namespace
{
bool Fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

bool TargetLess(const CompileTarget& left, const CompileTarget& right)
{
    return std::tie(left.binary, left.entry, left.profile) < std::tie(right.binary, right.entry, right.profile);
}
} // namespace

std::vector<CompileTarget> SceneCompileTargets(const LX::LXMaterialProgram& program)
{
    std::vector<CompileTarget> targets;
    for (const auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        targets.push_back({backend, "LXSceneVS", "vs_6_0"});
        for (const auto entry : {"LXSceneGBufferPS", "LXSceneColorPS", "LXSceneLookup0PS", "LXSceneLookup1PS"})
        {
            targets.push_back({backend, entry, "ps_6_0"});
        }
        if (program.surface)
        {
            targets.push_back({backend, "LXSceneShadowVS", "vs_6_0"});
            targets.push_back({backend, "LXSceneShadowPS", "ps_6_0"});
        }
        if ((program.features & 0x1000u) != 0)
        {
            targets.push_back({backend, "LXSceneSubsurfacePS", "ps_6_0"});
        }
        if ((program.features & 0x0800u) != 0)
        {
            targets.push_back({backend, "LXSceneRefractionPS", "ps_6_0"});
        }
        if (program.volume)
        {
            targets.push_back({backend, "LXSceneVolumeCoefficientCS", "cs_6_0"});
        }
    }
    std::ranges::sort(targets, TargetLess);
    return targets;
}

bool CompileSceneProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& shaderDirectory,
                         const std::filesystem::path& sourceFile, const Budget& budget, VerifiedProduct& result,
                         std::string& error)
{
    if (program.volume && program.slang.find("#define LX_MATERIAL_VOLUME_HOMOGENEOUS 1\n") == std::string::npos)
    {
        return Fail(error,
                    "LX Scene Volume requires homogeneous coefficients; spatially varying Volume is unsupported.");
    }
    std::ifstream host(shaderDirectory / "Includes/MaterialGraphSceneHost.slang", std::ios::binary);
    const std::string suffix{std::istreambuf_iterator<char>(host), {}};
    if (!host || suffix.empty())
    {
        return Fail(error, "LX Scene compiler cannot read its canonical host shader.");
    }
    std::error_code ioError;
    std::filesystem::create_directories(sourceFile.parent_path(), ioError);
    if (ioError)
    {
        return Fail(error, "LX Scene shader cache directory failed: " + ioError.message());
    }
    {
        std::ofstream stream(sourceFile, std::ios::binary | std::ios::trunc);
        stream << BuildBoundSource(program) << suffix;
        if (program.volume)
        {
            stream << "\n#include \"MaterialGraphSceneVolumeCoefficients.slang\"\n";
        }
        if (!stream)
        {
            return Fail(error, "LX Scene shader source write failed.");
        }
    }
    RHIShaderPermutation permutation;
    if (!permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error) ||
        ((program.features & 0x3800u) != 0 && !permutation.Set("LX_MATERIAL_ROUTE", "2", error)))
    {
        return false;
    }
    RHIShaderCompileOptions options;
    options.strictMath = true;
    options.fineDerivatives = true;
    options.includeDirectories.push_back(shaderDirectory / "Includes");
    Capabilities capabilities;
    capabilities.coreForward = capabilities.layeredLookup = capabilities.refraction = capabilities.subsurface =
        capabilities.volume = true;
    VerifiedProduct candidate;
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    if (!VerifyProduct(program, sourceFile, SceneCompileTargets(program), permutation, options, capabilities, budget,
                       candidate, diagnostics))
    {
        error.clear();
        for (const auto& diagnostic : diagnostics)
        {
            error += diagnostic.message + "\n";
        }
        return false;
    }
    candidate.program.semanticKey += SceneHostIdentity;
    result = std::move(candidate);
    error.clear();
    return true;
}

bool LoadSceneShaders(const VerifiedProduct& product, RHIShaderBinary backend, SceneShaderSet& result,
                      std::string& error)
{
    const auto expected = SceneCompileTargets(product.program);
    auto actual = product.targets;
    std::ranges::sort(actual, TargetLess);
    if (!product.program.semanticKey.ends_with(SceneHostIdentity) || actual.size() != expected.size() ||
        product.shaders.size() != expected.size() ||
        (backend != RHIShaderBinary::Dxil && backend != RHIShaderBinary::SpirV))
    {
        return Fail(error, "LX cooked Scene host identity or complete stage set is missing.");
    }
    SceneShaderSet candidate;
    candidate.layout = product.layout;
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const auto& target = expected[index];
        const auto& recorded = actual[index];
        if (std::tie(target.binary, target.entry, target.profile) !=
            std::tie(recorded.binary, recorded.entry, recorded.profile))
        {
            return Fail(error, "LX cooked Scene stage/profile differs from its feature requirements.");
        }
        const auto name = target.binary == RHIShaderBinary::Dxil ? "dxil" : "spirv";
        const auto shader = std::ranges::find_if(product.shaders, [&](const auto& artifact) {
            return artifact.backend == name && artifact.entryPoint == target.entry;
        });
        if (shader == product.shaders.end() || shader->bytecode.empty())
        {
            return Fail(error, "LX cooked Scene bytecode is missing.");
        }
        if (target.binary != backend)
        {
            continue;
        }
        using Member = RHIShaderCompiler::VerifiedShader SceneShaderSet::*;
        const std::pair<std::string_view, Member> members[]{{"LXSceneVS", &SceneShaderSet::vertex},
                                                            {"LXSceneGBufferPS", &SceneShaderSet::gbuffer},
                                                            {"LXSceneColorPS", &SceneShaderSet::color},
                                                            {"LXSceneLookup0PS", &SceneShaderSet::lookup0},
                                                            {"LXSceneLookup1PS", &SceneShaderSet::lookup1},
                                                            {"LXSceneShadowVS", &SceneShaderSet::shadowVertex},
                                                            {"LXSceneShadowPS", &SceneShaderSet::shadow},
                                                            {"LXSceneSubsurfacePS", &SceneShaderSet::subsurface},
                                                            {"LXSceneRefractionPS", &SceneShaderSet::refraction},
                                                            {"LXSceneVolumeCoefficientCS", &SceneShaderSet::volume}};
        const auto member = std::ranges::find(members, target.entry, &std::pair<std::string_view, Member>::first);
        if (member == std::end(members))
        {
            return Fail(error, "LX cooked Scene entry has no installed consumer.");
        }
        (candidate.*member->second).bytecode.Assign(shader->bytecode.data(), shader->bytecode.size());
    }
    result = std::move(candidate);
    error.clear();
    return true;
}
} // namespace material_graph
