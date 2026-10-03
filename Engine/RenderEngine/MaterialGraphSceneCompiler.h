#pragma once

#include "MaterialGraphProduct.h"
#include "ShaderMeta.h"

namespace material_graph
{
struct SceneShaderSet
{
    BindingLayout layout;
    RHIShaderCompiler::VerifiedShader vertex, gbuffer, color, lookup0, lookup1;
    RHIShaderCompiler::VerifiedShader shadowVertex, shadow, subsurface, refraction, volume;
};

std::vector<CompileTarget> SceneCompileTargets(const LX::LXMaterialProgram& program);

// Editor preparation and AssetCooker use the same host, permutations and stage set.
// Result publication is atomic; the source file is a caller-owned cache artifact.
bool CompileSceneProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& shaderDirectory,
                         const std::filesystem::path& sourceFile, const Budget& budget, VerifiedProduct& result,
                         std::string& error, FileGuid graphGuid = {});

// Cooked consumers validate the complete stage set and copy bytecode without
// reading shader sources, reflecting or loading the Slang compiler.
bool LoadSceneShaders(const VerifiedProduct& product, RHIShaderBinary backend, SceneShaderSet& result,
                      std::string& error);
} // namespace material_graph
