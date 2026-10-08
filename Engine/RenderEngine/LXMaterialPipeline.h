#pragma once

#include "LXMaterialRuntime.h"
#include "RHI/RHIGraphicsPipelineRequest.h"
#include "RHI/RHIShaderCompiler.h"
#include "Experiment/Cooked/CookedCodeMaterial.h"
#include <algorithm>

namespace LX::Runtime
{
struct ComputeCompileIdentity
{
    RHIShaderBinary backend{};
    std::string source, entry, profile = "cs_6_0";
    RHIShaderPermutation permutation;
    RHIShaderCompileOptions options;
    std::string dependencies, sealedProgramIdentity;
};

struct ComputeShaderDescription
{
    // Renderer helpers have no material schema; generated material compute does.
    own::shared_owner<const ShaderGeneration> shader;
    RHIShaderPermutationKey materialPermutationKey{};
    ComputeCompileIdentity compile;
};

struct CompiledCompute
{
    ComputeShaderDescription description;
    RHIShaderCompiler::VerifiedShader stage;
};

inline bool CompileCompute(std::string_view source, std::string_view entry,
    const RHIShaderPermutation& permutation, RHIShaderCompileOptions options,
    CompiledCompute& result, std::string& error, std::string_view profile = "cs_6_0")
{
    CompiledCompute candidate;
    auto& id = candidate.description.compile;
    id.backend = RHIShaderCompiler::GetOutput();
    id.source = source; id.entry = entry;
    id.profile = profile;
    id.permutation = permutation; id.options = std::move(options);
    if (!RHIShaderCompiler::VerifyFile(source, entry, id.profile, id.backend,
        permutation, candidate.stage, error, id.options)) return false;
    id.dependencies = candidate.stage.dependencyIdentity;
    result = std::move(candidate);
    error.clear();
    return true;
}

    inline bool RestoreCodeCompute(own::shared_owner<const ShaderGeneration> shader,
        std::uint32_t passIndex, std::span<const std::uint16_t> keywords,
        CompiledCompute& result, std::string& error)
    {
        if (!shader || !shader->codeProgram || shader->meta.codeProgramIdentity.empty())
        {
            error = "Code compute requires its exact verified program owner.";
            return false;
        }
        const auto& program = *shader->codeProgram;
        const auto found = std::ranges::find_if(program.variants, [&](const auto& value)
        {
            return value.backend == RHIShaderCompiler::GetOutput() && value.passIndex == passIndex
                && value.vertexAttributeMask == 0u && !value.referencePath
                && std::ranges::equal(value.keywordSelections, keywords);
        });
        if (found == program.variants.end() || found->stages.size() != 1u
            || found->stages.front().stage != RHIShaderStage::Compute)
        {
            error = "Verified code program has no exact requested compute variant.";
            return false;
        }
        CompiledCompute candidate;
        const auto& stage = found->stages.front();
        auto& identity = candidate.description.compile;
        identity.backend = found->backend;
        identity.source = program.rootSourcePath;
        identity.entry = stage.entry;
        identity.profile = stage.profile;
        identity.permutation = found->permutation;
        identity.dependencies = shader->meta.codeProgramIdentity;
        identity.sealedProgramIdentity = shader->meta.codeProgramIdentity;
        candidate.stage = { stage.bytecode, stage.reflection, shader->meta.codeProgramIdentity };
        candidate.description.materialPermutationKey = found->materialPermutationKey;
        candidate.description.shader = std::move(shader);
        result = std::move(candidate);
        error.clear();
        return true;
    }

class ComputeGeneration
{
public:
    ComputeGeneration() = default;
    ComputeGeneration(const ComputeGeneration&) = delete;
    ComputeGeneration& operator=(const ComputeGeneration&) = delete;
    ComputeShaderDescription shader;
    bool IsValid() const { return handle_.IsValid(); }
    RHIPipelineHandle GetHandle() const { return handle_; }
    const RHIComputePipelineDesc& GetDesc() const { return desc_; }

private:
    friend class ComputePipeline;
    RHIComputePipelineDesc desc_{};
    std::vector<std::uint8_t> bytecode_;
    RHIPipelineHandle handle_{};
};

// The cache owns native PSOs; LX owns the immutable description and bytecode.
// Recorded frames retain GetGeneration(). Retirement remains holder-aware in
// the installing renderer, with the same completion rules as graphics.
class ComputePipeline
{
public:
    ComputePipeline() = default;
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;
    ComputePipeline(ComputePipeline&&) noexcept = default;
    ComputePipeline& operator=(ComputePipeline&&) noexcept = default;

    bool Create(IRenderPipelineCache& cache, const RHIComputePipelineDesc& desc,
        ComputeShaderDescription shader, std::string& error)
    {
        if (generation_) { error = "LX compute generation was already published."; return false; }
        const auto& id = shader.compile;
        // An empty identity is reserved for external precompiled helper fixtures.
        if ((shader.shader || !id.entry.empty() || !id.source.empty() || !id.sealedProgramIdentity.empty()) &&
            ((id.backend != RHIShaderBinary::Dxil && id.backend != RHIShaderBinary::SpirV) ||
                id.entry.empty() || !id.profile.starts_with("cs_") ||
                (id.sealedProgramIdentity.empty() && id.dependencies.empty())))
        { error = "LX compute generation requires a complete stage and dependency identity."; return false; }
        if (!desc.csBytecode || !desc.csSize || !desc.layout.IsValid())
        { error = "LX compute generation requires owned bytecode and a pipeline layout."; return false; }
        auto candidate = std::make_shared<ComputeGeneration>();
        candidate->shader = std::move(shader);
        candidate->desc_ = desc;
        const auto* bytes = static_cast<const std::uint8_t*>(desc.csBytecode);
        candidate->bytecode_.assign(bytes, bytes + desc.csSize);
        candidate->desc_.csBytecode = candidate->bytecode_.data();
        candidate->handle_ = cache.GetOrCreateCompute(candidate->desc_, error);
        if (!candidate->handle_.IsValid()) return false;
        generation_ = std::move(candidate);
        error.clear();
        return true;
    }

    bool Create(IRenderPipelineCache& cache, const RHIComputePipelineDesc& desc, std::string& error)
    { return Create(cache, desc, {}, error); }

    bool Replace(IRenderPipelineCache& cache, const RHIComputePipelineDesc& desc,
        ComputeShaderDescription shader, RHICompletionPoint after, std::string& error,
        bool invalidatePrevious = true)
    {
        ComputePipeline candidate;
        if (!candidate.Create(cache, desc, std::move(shader), error)) return false;
        const auto previous = GetHandle();
        if (previous.IsValid() && previous != candidate.GetHandle() && invalidatePrevious)
            cache.InvalidatePipeline(previous, after);
        *this = std::move(candidate);
        return true;
    }
    bool IsValid() const { return generation_ && generation_->IsValid(); }
    RHIPipelineHandle GetHandle() const { return generation_ ? generation_->GetHandle() : RHIPipelineHandle{}; }
    std::shared_ptr<const ComputeGeneration> GetGeneration() const { return generation_; }
    const RHIComputePipelineDesc& GetDesc() const
    {
        static const RHIComputePipelineDesc empty;
        return generation_ ? generation_->GetDesc() : empty;
    }

private:
    std::shared_ptr<ComputeGeneration> generation_;
};

// Full compile identity is separate from the material keyword selection key.
// Renderer defines, vertex masks and reference variants belong here as well.
struct GraphicsCompileIdentity
{
    RHIShaderBinary backend{};
    std::string source, vertexEntry, pixelEntry;
    std::string vertexProfile, pixelProfile;
    std::uint32_t vertexAttributeMask{};
    bool referencePath{};
    // Binding shape and semantic opt-in belong to this exact shader generation,
    // never to a root-layout ID that unrelated shaders can share.
    bool visibleInstanceIds{};
    // Optional, strictly reflected GBufferSkinPaletteExtentV1 uniform ABI.
    // Its root index is retained with this exact accepted generation; it grants
    // no deformation/culling contract and changes no custom instance bytes.
    std::uint32_t gbufferSkinPaletteExtentV1Root{UINT32_MAX};
    ShaderGeometryVisibility geometryVisibility{ShaderGeometryVisibility::Direct};
    RHIShaderPermutation permutation;
    RHIShaderCompileOptions options;
    std::string vertexDependencies, pixelDependencies;
    // Cooked programs already seal compiler/options/include identities with
    // their complete target set; no source or compiler is needed to restore it.
    std::string sealedProgramIdentity;
};

struct CompiledGraphics
{
    GraphicsCompileIdentity identity;
    RHIShaderCompiler::VerifiedShader vertex, pixel;
};

    // Source-free restore for the existing explicit/reference code-material helpers.
    // This selects an already verified CPU variant, never a shader compiler cache.
    inline bool RestoreCodeGraphics(const ShaderMeta& meta, std::uint32_t passIndex,
        std::span<const std::uint16_t> keywords, std::uint32_t vertexMask, bool reference,
        CompiledGraphics& result, std::string& error)
    {
        if (!meta.codeProgram || meta.codeProgramIdentity.empty())
        {
            error = "Code graphics requires its exact verified program owner.";
            return false;
        }
        const auto& program = *meta.codeProgram;
        const auto variant = std::ranges::find_if(program.variants, [&](const auto& value)
        {
            return value.backend == RHIShaderCompiler::GetOutput() && value.passIndex == passIndex
                && value.vertexAttributeMask == vertexMask && value.referencePath == reference
                && std::ranges::equal(value.keywordSelections, keywords);
        });
        if (variant == program.variants.end())
        {
            error = "Verified code program has no exact requested graphics variant.";
            return false;
        }
        CompiledGraphics candidate;
        auto& identity = candidate.identity;
        identity.backend = variant->backend;
        identity.source = program.rootSourcePath; // Diagnostic value, never opened.
        identity.vertexAttributeMask = vertexMask;
        identity.referencePath = reference;
        identity.permutation = variant->permutation;
        identity.sealedProgramIdentity = meta.codeProgramIdentity;
        bool vertex{}, pixel{};
        for (const auto& stage : variant->stages)
        {
            if (stage.stage == RHIShaderStage::Vertex && !vertex)
            {
                candidate.vertex = { stage.bytecode, stage.reflection, meta.codeProgramIdentity };
                identity.vertexEntry = stage.entry;
                identity.vertexProfile = stage.profile;
                identity.vertexDependencies = meta.codeProgramIdentity;
                vertex = true;
            }
            else if (stage.stage == RHIShaderStage::Pixel && !pixel)
            {
                candidate.pixel = { stage.bytecode, stage.reflection, meta.codeProgramIdentity };
                identity.pixelEntry = stage.entry;
                identity.pixelProfile = stage.profile;
                identity.pixelDependencies = meta.codeProgramIdentity;
                pixel = true;
            }
            else
            {
                error = "Code graphics variant contains duplicate or unexpected stages.";
                return false;
            }
        }
        if (!vertex || !pixel)
        {
            error = "Code graphics variant is missing a linked stage.";
            return false;
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }

inline bool CompileGraphics(std::string_view source, std::string_view vertex, std::string_view pixel,
    const RHIShaderPermutation& permutation, RHIShaderCompileOptions options,
    CompiledGraphics& result, std::string& error)
{
    CompiledGraphics candidate;
    auto& identity = candidate.identity;
    identity.backend = RHIShaderCompiler::GetOutput();
    identity.source = source;
    identity.vertexEntry = vertex;
    identity.pixelEntry = pixel;
    identity.vertexProfile = "vs_6_0";
    identity.pixelProfile = "ps_6_0";
    identity.permutation = permutation;
    identity.options = std::move(options);
    if (!RHIShaderCompiler::VerifyFile(source, vertex, identity.vertexProfile, identity.backend,
            permutation, candidate.vertex, error, identity.options) ||
        !RHIShaderCompiler::VerifyFile(source, pixel, identity.pixelProfile, identity.backend,
            permutation, candidate.pixel, error, identity.options)) return false;
    identity.vertexDependencies = candidate.vertex.dependencyIdentity;
    identity.pixelDependencies = candidate.pixel.dependencyIdentity;
    result = std::move(candidate);
    error.clear();
    return true;
}

struct GraphicsShaderDescription
{
    own::shared_owner<const ShaderGeneration> shader;
    RHIShaderPermutationKey materialPermutationKey{};
    GraphicsCompileIdentity compile;
};

struct GraphicsGeneration
{
    GraphicsShaderDescription shader;
    // Owns bytecode, input semantics, fixed state and the accepted PSO handle.
    RHIGraphicsPipelineRequest pipeline;
};

inline std::shared_ptr<const GraphicsGeneration> ResolveGraphicsGeneration(
    std::span<const std::shared_ptr<const GraphicsGeneration>> generations,
    ShaderMetaHandle handle, RHIShaderPermutationKey materialKey, const ShaderMetaBindingLayout& layout,
    std::uint32_t vertexMask, bool reference)
{
    for (const auto& generation : generations)
        if (generation && generation->pipeline.IsValid() && generation->shader.shader &&
            generation->shader.shader->codeHandle == handle &&
            generation->shader.materialPermutationKey == materialKey &&
            generation->shader.shader->layout == layout &&
            generation->shader.compile.vertexAttributeMask == vertexMask &&
            generation->shader.compile.referencePath == reference) return generation;
    return nullptr;
}

// LX owns the compiled request and its contract together. The installing
// renderer still owns retirement policy and audits shared native cache holders.
class GraphicsPipeline
{
public:
    GraphicsPipeline() = default;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) noexcept = default;
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept = default;

    bool Prepare(const RHIGraphicsPipelineDesc& desc, GraphicsShaderDescription shader, std::string& error)
    {
        if (generation_) { error = "Cannot prepare an existing LX graphics generation."; return false; }
        if (!Validate(shader, error)) return false;
        auto candidate = std::make_shared<GraphicsGeneration>();
        candidate->shader = std::move(shader);
        if (!candidate->pipeline.Prepare(desc, error)) return false;
        generation_ = std::move(candidate);
        return true;
    }

    // Non-material bootstrap and old standalone fixtures carry no contract.
    bool Prepare(const RHIGraphicsPipelineDesc& desc, std::string& error)
    { return Prepare(desc, {}, error); }

    bool Create(IRenderPipelineCache& cache, const RHIGraphicsPipelineDesc& desc,
        GraphicsShaderDescription shader, std::string& error)
    {
        if (generation_) { error = "LX graphics generation was already prepared or published."; return false; }
        if (!Validate(shader, error)) return false;
        auto candidate = std::make_shared<GraphicsGeneration>();
        candidate->shader = std::move(shader);
        if (!candidate->pipeline.Create(cache, desc, error)) return false;
        generation_ = std::move(candidate);
        return true;
    }

    bool Create(IRenderPipelineCache& cache, const RHIGraphicsPipelineDesc& desc, std::string& error)
    { return Create(cache, desc, {}, error); }

    RHIPipelineRequestState Poll(IRenderPipelineCache& cache, std::string& error)
    {
        if (!generation_) { error = "LX graphics generation was not prepared."; return RHIPipelineRequestState::Failed; }
        return generation_->pipeline.Poll(cache, error);
    }

    bool Replace(IRenderPipelineCache& cache, const RHIGraphicsPipelineDesc& desc,
        GraphicsShaderDescription shader, RHICompletionPoint after, std::string& error,
        bool invalidatePrevious = true)
    {
        if (generation_ && !IsValid())
        { error = "Cannot replace a pending LX graphics generation."; return false; }
        GraphicsPipeline candidate;
        if (!candidate.Create(cache, desc, std::move(shader), error)) return false;
        const auto previous = GetHandle();
        if (previous.IsValid() && previous != candidate.GetHandle() && invalidatePrevious)
            cache.InvalidatePipeline(previous, after);
        *this = std::move(candidate);
        return true;
    }

    bool Replace(IRenderPipelineCache& cache, const RHIGraphicsPipelineDesc& desc,
        RHICompletionPoint after, std::string& error, bool invalidatePrevious = true)
    { return Replace(cache, desc, {}, after, error, invalidatePrevious); }

    bool IsValid() const { return generation_ && generation_->pipeline.IsValid(); }
    RHIPipelineHandle GetHandle() const
    { return generation_ ? generation_->pipeline.GetHandle() : RHIPipelineHandle{}; }
    const RHIGraphicsPipelineDesc& GetDesc() const
    {
        static const RHIGraphicsPipelineDesc empty;
        return generation_ ? generation_->pipeline.GetDesc() : empty;
    }
    std::shared_ptr<const GraphicsGeneration> GetGeneration() const
    { return IsValid() ? generation_ : nullptr; }

private:
    static bool Validate(const GraphicsShaderDescription& shader, std::string& error)
    {
        const auto& id = shader.compile;
        if (shader.shader && ((id.backend != RHIShaderBinary::Dxil && id.backend != RHIShaderBinary::SpirV) ||
            id.vertexEntry.empty() || id.pixelEntry.empty() || !id.vertexProfile.starts_with("vs_") ||
            !id.pixelProfile.starts_with("ps_") || (id.sealedProgramIdentity.empty() &&
                (id.vertexDependencies.empty() || id.pixelDependencies.empty()))))
        { error = "LX material graphics generation requires complete stage and dependency identities."; return false; }
        error.clear();
        return true;
    }
    std::shared_ptr<GraphicsGeneration> generation_;
};
} // namespace LX::Runtime
