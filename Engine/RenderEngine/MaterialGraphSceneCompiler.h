#pragma once

#include "MaterialGraphProduct.h"
#include "ShaderMeta.h"

#include <optional>

namespace material_graph
{
struct SceneShaderSet
{
    BindingLayout layout;
    RHIShaderCompiler::VerifiedShader vertex, gbuffer, depth, color, lookup0, lookup1;
    RHIShaderCompiler::VerifiedShader mesh, shadowMesh;
    RHIShaderCompiler::VerifiedShader shadowVertex, shadow, subsurface, refraction, volume, runtimeEffects0, runtimeEffects1;
};

// No backend means both. AssetCooker always cooks both; the editor compiles only
// the backend its renderer runs, because the other half is never consumed there.
std::vector<CompileTarget> SceneCompileTargets(const LX::LXMaterialProgram& program,
                                               std::optional<RHIShaderBinary> backend = std::nullopt);

// Editor preparation and AssetCooker use the same host, permutations and stage set.
// Result publication is atomic; the source file is a caller-owned cache artifact.
bool CompileSceneProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& shaderDirectory,
                         const std::filesystem::path& sourceFile, const Budget& budget, VerifiedProduct& result,
                         std::string& error, FileGuid graphGuid = {},
                         std::optional<RHIShaderBinary> backend = std::nullopt);

// The backend the editor renderer was started with. Empty until the renderer
// starts (and in headless tools), which keeps authoring compiles on both.
void SetAuthoringSceneBackend(std::optional<RHIShaderBinary> backend);
std::optional<RHIShaderBinary> AuthoringSceneBackend();

bool HasSceneBackend(const VerifiedProduct& product, RHIShaderBinary backend);

// Cooked consumers validate the complete stage set and copy bytecode without
// reading shader sources, reflecting or loading the Slang compiler.
bool LoadSceneShaders(const VerifiedProduct& product, RHIShaderBinary backend, SceneShaderSet& result,
                      std::string& error);
} // namespace material_graph
