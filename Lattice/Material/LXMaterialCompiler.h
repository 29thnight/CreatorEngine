#pragma once

#include "LXMaterialIR.h"
#include "LXMaterialExecution.h"

namespace LX
{

struct LXMaterialSource
{
    Id scope = 0;
    Id node = 0;
    Id pin = 0;
    std::vector<Id> instances;
    std::string property;

    bool operator==(const LXMaterialSource&) const = default;
};

struct LXMaterialDiagnostic
{
    std::string code;
    std::string message;
    LXMaterialSource source;
    Issue::Severity severity = Issue::Severity::Error;
};

enum class LXMaterialResourceKind : std::uint8_t
{
    Texture,
    Sampler
};

struct LXMaterialResource
{
    LXMaterialResourceKind kind{};
    std::uint32_t slot = 0;
    Id parameter = 0;
    std::string reference;
    // Source storage encoding. SRGB textures must use an SRGB view or
    // predecoded linear storage, so decoding precedes interpolation.
    // Alpha stays linear. Numeric Color defaults/parameters decode in shader.
    LXColorSpace colorSpace = LXColorSpace::Data;
    LXMaterialSource source;

    bool operator==(const LXMaterialResource&) const = default;
};

struct LXMaterialSourceRange
{
    std::uint32_t firstLine = 0;
    std::uint32_t lastLine = 0;
    LXMaterialSource source;
};

struct LXMaterialProgram
{
    static constexpr std::uint32_t CompilerVersion = 3;
    std::string slang;
    // Exact canonical payload, not std::hash. MAT-7 adds common shader/compiler
    // dependency digests when deriving the cooked artifact cache identity.
    std::string semanticKey;
    std::uint32_t features = 0;
    bool surface = false;
    bool volume = false;
    std::uint32_t textureSamples = 0;
    std::vector<LXMaterialParameter> parameters;
    std::vector<LXMaterialResource> resources;
    std::vector<LXMaterialSourceRange> sourceMap;
    LXMaterialExecutionRequirements execution;
};

struct LXMaterialCompilerOptions
{
    std::size_t maxExpressions = 4096;
    std::size_t maxResources = 64;
    std::size_t maxParameters = 128;
    // The host supplies common-include digests and backend/profile/options here.
    // Source stays identical; a changed dependency identity forces verification.
    std::string dependencies;
};

std::optional<LXMaterialProgram> GenerateMaterialSlang(const LXMaterialAsset& asset,
                                                       std::vector<LXMaterialDiagnostic>* diagnostics = nullptr,
                                                       LXMaterialCompilerOptions options = {});
std::optional<LXMaterialSource> FindMaterialSource(const LXMaterialProgram& program, std::uint32_t line);
// Maps Slang's file(line[,column]) or file:line[:column] messages. Diagnostics
// from common includes remain visible without a graph location.
std::vector<LXMaterialDiagnostic> MapMaterialCompilerDiagnostics(const LXMaterialProgram& program,
                                                                 const std::filesystem::path& sourceFile,
                                                                 const std::string& text);
// Deterministic, backend-neutral generated metadata. This is not the engine's
// authored ShaderMeta pass schema; MAT-7 supplies its pass/reflection adapter.
std::string WriteMaterialProgramMetadata(const LXMaterialProgram& program);

struct LXMaterialShaderArtifact
{
    std::string backend;
    std::string entryPoint;
    std::vector<std::uint8_t> bytecode;
};

using LXMaterialVerifier = std::function<bool(const LXMaterialProgram&, std::vector<LXMaterialShaderArtifact>&,
                                              std::vector<LXMaterialDiagnostic>&)>;

// A CPU-side publication boundary. The verifier must compile every backend and
// entry required by its host; RHI/PSO lifetime and scene replacement are MAT-7.
class LXMaterialProgramStore
{
  public:
    bool Publish(const LXMaterialAsset& asset, const LXMaterialVerifier& verifier,
                 std::vector<LXMaterialDiagnostic>* diagnostics = nullptr, LXMaterialCompilerOptions options = {});
    const std::optional<LXMaterialProgram>& LastValidProgram() const { return program_; }
    const std::vector<LXMaterialShaderArtifact>& Artifacts() const { return artifacts_; }
    std::uint64_t Generation() const { return generation_; }

  private:
    std::optional<LXMaterialProgram> program_;
    std::vector<LXMaterialShaderArtifact> artifacts_;
    std::uint64_t generation_ = 0;
};

} // namespace LX
