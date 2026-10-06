#include "MaterialGraphSceneCompiler.h"
#include "MaterialGraphShaderMeta.h"

#include <algorithm>
#include <atomic>
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

std::vector<ShaderPassDesc> SceneMetaPasses(const LX::LXMaterialProgram& program)
{
    std::vector<ShaderPassDesc> passes;
    const auto graphics = [&](std::string name, std::string vertex, std::string pixel,
                              ShaderPassQueue queue = ShaderPassQueue::Opaque, bool writeDepth = false) {
        ShaderPassDesc pass;
        pass.name = std::move(name);
        pass.vertex = ShaderStageEntry{std::move(vertex)};
        pass.pixel = ShaderStageEntry{std::move(pixel)};
        pass.queue = queue;
        pass.state.cullMode = RHICullMode::None;
        pass.state.depthTest = RHICompareOp::LessEqual;
        pass.state.depthWrite = writeDepth;
        passes.push_back(std::move(pass));
    };
    // ShaderMeta describes generated entry contracts, including the installed
    // common Forward+ alpha consumer.
    graphics("GBuffer", "LXSceneVS", "LXSceneGBufferPS", ShaderPassQueue::Opaque, true);
    graphics("LXSceneColor", "LXSceneVS", "LXSceneColorPS");
    graphics("Forward", "LXSceneVS", "LXSceneColorPS", ShaderPassQueue::Transparent);
    passes.back().state.blendMode = ShaderBlendMode::Alpha;
    graphics("LXSceneLookup0", "LXSceneVS", "LXSceneLookup0PS");
    graphics("LXSceneLookup1", "LXSceneVS", "LXSceneLookup1PS");
    if ((program.features & 0x1800u) != 0)
    {
        graphics("LXSceneRuntimeEffects0", "LXSceneVS", "LXSceneRuntimeEffects0PS");
        graphics("LXSceneRuntimeEffects1", "LXSceneVS", "LXSceneRuntimeEffects1PS");
    }
    if (program.surface) graphics("Shadow", "LXSceneShadowVS", "LXSceneShadowPS", ShaderPassQueue::Shadow, true);
    if ((program.features & 0x1000u) != 0) graphics("Subsurface", "LXSceneVS", "LXSceneSubsurfacePS");
    if ((program.features & 0x0800u) != 0) graphics("Refraction", "LXSceneVS", "LXSceneRefractionPS");
    if (program.volume)
    {
        ShaderPassDesc pass;
        pass.name = "VolumeCoefficients";
        pass.compute = ShaderStageEntry{"LXSceneVolumeCoefficientCS"};
        pass.queue = ShaderPassQueue::Compute;
        passes.push_back(std::move(pass));
    }
    return passes;
}
} // namespace

std::vector<CompileTarget> SceneCompileTargets(const LX::LXMaterialProgram& program,
                                               std::optional<RHIShaderBinary> only)
{
    std::vector<CompileTarget> targets;
    for (const auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        if (only && *only != backend) continue;
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
        if ((program.features & 0x1800u) != 0)
        {
            targets.push_back({backend, "LXSceneRuntimeEffects0PS", "ps_6_0"});
            targets.push_back({backend, "LXSceneRuntimeEffects1PS", "ps_6_0"});
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
                         std::string& error, FileGuid graphGuid, std::optional<RHIShaderBinary> backend)
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
    std::string hostSource = BuildBoundSource(program) + suffix;
    if (program.volume) hostSource += "\n#include \"MaterialGraphSceneVolumeCoefficients.slang\"\n";
    {
        std::ofstream stream(sourceFile, std::ios::binary | std::ios::trunc);
        stream << hostSource;
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
    std::vector<RHIShaderReflection> reflections;
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    if (!VerifyProduct(program, sourceFile, SceneCompileTargets(program, backend), permutation, options, capabilities, budget,
                       candidate, diagnostics, &reflections))
    {
        error.clear();
        for (const auto& diagnostic : diagnostics)
        {
            error += diagnostic.message + "\n";
        }
        return false;
    }
    candidate.program.semanticKey += SceneHostIdentity;
    if (graphGuid != FileGuid{})
    {
        std::ifstream source(sourceFile, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(source), {}};
        if (!source || text != hostSource)
        {
            return Fail(error, "Generated Scene source changed during verification.");
        }
        GeneratedMaterialShader generated;
        if (!PublishMaterialShaderMeta(candidate.program, graphGuid, hostSource,
            SceneMetaPasses(program), reflections,
            sourceFile.parent_path() / (sourceFile.stem().string() + ".generated"), generated, error))
        {
            if (error.empty()) error = "Generated Scene material source cannot be read.";
            return false;
        }
        candidate.materialShader = std::make_shared<GeneratedMaterialShader>(std::move(generated));
    }
    result = std::move(candidate);
    error.clear();
    return true;
}

bool LoadSceneShaders(const VerifiedProduct& product, RHIShaderBinary backend, SceneShaderSet& result,
                      std::string& error)
{
    // Every backend the product carries must be complete; the requested one must be carried.
    std::vector<CompileTarget> expected;
    for (const auto carried : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        if (!HasSceneBackend(product, carried)) continue;
        const auto stages = SceneCompileTargets(product.program, carried);
        expected.insert(expected.end(), stages.begin(), stages.end());
    }
    auto actual = product.targets;
    std::ranges::sort(actual, TargetLess);
    if (!product.program.semanticKey.ends_with(SceneHostIdentity) || actual.size() != expected.size() ||
        product.shaders.size() != expected.size() ||
        (backend != RHIShaderBinary::Dxil && backend != RHIShaderBinary::SpirV))
    {
        return Fail(error, "LX cooked Scene host identity or complete stage set is missing.");
    }
    if (!HasSceneBackend(product, backend))
    {
        return Fail(error, "LX cooked Scene product was compiled without the running backend.");
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
                                                            {"LXSceneRuntimeEffects0PS", &SceneShaderSet::runtimeEffects0},
                                                            {"LXSceneRuntimeEffects1PS", &SceneShaderSet::runtimeEffects1},
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
namespace
{
// 0 = unset, otherwise RHIShaderBinary + 1.
std::atomic<int> authoringBackend{0};
} // namespace

void SetAuthoringSceneBackend(std::optional<RHIShaderBinary> backend)
{
    authoringBackend.store(backend ? static_cast<int>(*backend) + 1 : 0);
}

std::optional<RHIShaderBinary> AuthoringSceneBackend()
{
    const int value = authoringBackend.load();
    if (value == 0) return std::nullopt;
    return static_cast<RHIShaderBinary>(value - 1);
}

bool HasSceneBackend(const VerifiedProduct& product, RHIShaderBinary backend)
{
    return std::ranges::any_of(product.targets, [&](const auto& target) { return target.binary == backend; });
}
} // namespace material_graph
