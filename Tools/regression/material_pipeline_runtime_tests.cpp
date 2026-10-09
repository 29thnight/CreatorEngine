#include "LXMaterialPipeline.h"
#include "material_owner_checks.h"
#include "MaterialGraphProduct.h"
#include "MaterialGraphShaderMeta.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "Render/Passes/Geometry/EnhancedForwardPass.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
struct PipelineCache : IRenderPipelineCache
{
    std::uint32_t next{1}, invalidations{};
    RHIPipelineHandle shared;
    bool fail{}, pending{};
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc&, std::string& error) override
    {
        if (fail) { error = "Injected PSO failure"; return {}; }
        error.clear();
        return shared.IsValid() ? shared : RHIPipelineHandle{next++};
    }
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc&, std::string& error) override
    {
        if (fail) { error = "Injected compute PSO failure"; return {}; }
        error.clear();
        return shared.IsValid() ? shared : RHIPipelineHandle{next++};
    }
    RHIPipelineRequestState RequestGraphics(const RHIGraphicsPipelineDesc& desc, RHIPipelineHandle& out,
                                           std::string& error) override
    {
        if (pending) { error.clear(); return RHIPipelineRequestState::Pending; }
        return IRenderPipelineCache::RequestGraphics(desc, out, error);
    }
    bool InvalidatePipeline(RHIPipelineHandle, RHICompletionPoint) override { ++invalidations; return true; }
    std::uint32_t InvalidatePipelines(RHICompletionPoint) override { return 0; }
    std::uint32_t CollectRetiredPipelines(RHICompletionPoint) override { return 0; }
};
struct RootCache : IRenderRootSignatureCache
{
    RHIPipelineLayoutHandle GetOrCreate(const RHIPipelineLayoutDesc&, std::string& error) override
    { error.clear(); return {1}; }
};
}

void RunCookedGraphicsIdentityTests(const material_graph::VerifiedProduct& product)
{
    std::string error;
    for (const auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        LX::Runtime::GraphicsShaderDescription shader;
        if (!material_graph::DescribeGraphicsShader(product, backend, "LXSceneVS", "LXSceneGBufferPS", shader, error) ||
            !material_graph_test::SamePinnedObject(shader.shader, product.materialShader) || shader.compile.sealedProgramIdentity != product.program.semanticKey ||
            shader.compile.backend != backend || !shader.compile.options.strictMath ||
            !shader.compile.options.fineDerivatives || shader.compile.permutation.Empty())
        {
            throw std::runtime_error("Source-free LX graphics identity: " + error);
        }
        const auto accepted = shader;
        if (material_graph::DescribeGraphicsShader(product, backend, "missing", "LXSceneGBufferPS", shader, error) ||
            !material_graph_test::SamePinnedObject(shader.shader, accepted.shader) || shader.compile.sealedProgramIdentity != accepted.compile.sealedProgramIdentity)
        {
            throw std::runtime_error("Bad cooked stage replaced accepted graphics identity");
        }
    }
    std::cout << "MAT7_COOKED_GRAPHICS_OK checks=4 compilerFree=true\n";
}

void RunMaterialPipelineRuntimeTests(const std::filesystem::path& repository)
{
    std::size_t checks{};
    const auto check = [&](bool valid, const std::string& message) {
        ++checks;
        if (!valid) throw std::runtime_error(message);
    };
    const auto shaderRoot = repository / "Dynamic_CPP/Assets/Shaders/DefaultPassShader";
    std::string error;
    ShaderMeta gbufferMeta, forwardMeta;
    check(ShaderMetaLoader::LoadFile(shaderRoot / "GBuffer.shadermeta",
        FileGuid{"77777777-7777-4777-8777-777777777777"}, gbufferMeta, error), "GBuffer fixture: " + error);
    check(ShaderMetaLoader::LoadFile(shaderRoot / "Forward.shadermeta",
        FileGuid{"88888888-8888-4888-8888-888888888888"}, forwardMeta, error), "Forward fixture: " + error);
    for (const auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        RHIShaderCompiler::ScopedOutput output(backend);
        {
            PipelineCache computeCache;
            LX::Runtime::CompiledCompute compiled;
            const auto file = shaderRoot / "MaterialGraphMeshSurface.slang";
            check(LX::Runtime::CompileCompute(file.string(), "LXTransformMesh", {}, {}, compiled, error),
                "Actual auxiliary compute compile: " + error);
            check(compiled.description.compile.backend == backend &&
                compiled.description.compile.entry == "LXTransformMesh" &&
                !compiled.description.compile.dependencies.empty(), "Compute seals actual stage/dependency identity");
            auto sourceBytes = compiled.stage.bytecode;
            RHIComputePipelineDesc desc;
            desc.layout = {1}; desc.csBytecode = sourceBytes.Data(); desc.csSize = sourceBytes.Size();
            LX::Runtime::ComputePipeline compute;
            check(compute.Create(computeCache, desc, compiled.description, error), "Common compute creation: " + error);
            sourceBytes = {};
            const auto accepted = compute.GetGeneration();
            check(compute.GetDesc().csBytecode != compiled.stage.bytecode.Data() &&
                std::memcmp(compute.GetDesc().csBytecode, compiled.stage.bytecode.Data(), desc.csSize) == 0,
                "Compute owns temporary source bytecode");
            check(!compute.Create(computeCache, desc, compiled.description, error) && compute.GetGeneration() == accepted,
                "Compute cannot overwrite an accepted generation");
            desc.csBytecode = compiled.stage.bytecode.Data();
            auto incomplete = compiled.description;
            incomplete.compile.dependencies.clear();
            check(!compute.Replace(computeCache, desc, incomplete, {}, error) && compute.GetGeneration() == accepted,
                "Incomplete helper identity preserves accepted compute");
            computeCache.fail = true;
            check(!compute.Replace(computeCache, desc, compiled.description, {}, error) &&
                compute.GetGeneration() == accepted && computeCache.invalidations == 0,
                "Compute PSO failure preserves accepted frame and native handle");
            computeCache.fail = false;
            computeCache.shared = compute.GetHandle();
            check(compute.Replace(computeCache, desc, compiled.description, {}, error) &&
                compute.GetGeneration() != accepted && computeCache.invalidations == 0,
                "Shared compute cache handle is not invalidated");
            computeCache.shared = {};
            check(compute.Replace(computeCache, desc, compiled.description, {17}, error) &&
                computeCache.invalidations == 1 && accepted->IsValid(),
                "Distinct compute replacement retires native handle after caller completion");
            check(compute.Replace(computeCache, desc, compiled.description, {}, error, false) &&
                computeCache.invalidations == 1, "Renderer can defer retirement for other compute holders");
            compute = {};
            check(accepted->GetDesc().csSize == compiled.stage.bytecode.Size() &&
                std::memcmp(accepted->GetDesc().csBytecode, compiled.stage.bytecode.Data(), desc.csSize) == 0,
                "Captured compute frame retains identity and bytecode after installing owner is reset");
            auto invalidDesc = desc;
            invalidDesc.csBytecode = nullptr;
            check(!compute.Create(computeCache, invalidDesc, compiled.description, error) && !compute.GetGeneration(),
                "Malformed compute description cannot publish");
            check(!LX::Runtime::CompileCompute("absent.slang", "LXTransformMesh", {}, {}, compiled, error) &&
                compiled.description.compile.entry == "LXTransformMesh" && compiled.stage.bytecode.IsValid(),
                "Compute compile failure retains previous complete artifact");
        }
        PipelineCache cache;
        RootCache roots;
        EnhancedFrameContext context;
        context.psoManager = &cache;
        context.rootSignatures = &roots;
        EnhancedGBufferPass gbuffer;
        EnhancedForwardPass forward;
        const ShaderMetaHandle ghandle{777, 1}, fhandle{888, 1};
        check(gbuffer.ApplyShaderMeta(context, ghandle, gbufferMeta, {}, error), "Actual GBuffer LX compile: " + error);
        check(forward.ApplyShaderMeta(context, fhandle, forwardMeta, {}, error), "Actual Forward LX compile: " + error);
        for (const auto keyword : {std::uint16_t{0}, std::uint16_t{1}})
        {
            EnhancedMaterialDrawSnapshot gs;
            EnhancedForwardMaterialDrawSnapshot fs;
            gs.shaderMetaHandle = ghandle; fs.shaderMetaHandle = fhandle;
            gs.keywordSelections = {keyword}; fs.keywordSelections = {keyword};
            std::shared_ptr<const ShaderMetaBindingLayout> glayout, flayout;
            check(gbuffer.EnsureShaderMetaVariant(context, ghandle, gbufferMeta, gs.keywordSelections,
                gs.permutationKey, glayout, error), "GBuffer native variant: " + error);
            check(forward.EnsureShaderMetaVariant(context, fhandle, forwardMeta, fs.keywordSelections,
                fs.permutationKey, flayout, error), "Forward native variant: " + error);
            gs.bindingLayout = *glayout; fs.bindingLayout = *flayout;
            check(gbuffer.CaptureShaderVariant(gs) && forward.CaptureShaderVariant(fs) &&
                fs.pipelineGenerations.size() == gs.pipelineGenerations.size() * 2,
                "Native Code draws retain common LX pipeline owners");
            for (const auto& generation : gs.pipelineGenerations)
            {
                const auto& identity = generation->shader.compile;
                check(identity.backend == backend && !identity.vertexDependencies.empty() &&
                    !identity.pixelDependencies.empty() && !identity.referencePath &&
                    LX::Runtime::ResolveGraphicsGeneration(gs.pipelineGenerations, ghandle, gs.permutationKey,
                        gs.bindingLayout, identity.vertexAttributeMask, false) == generation,
                    "GBuffer exact backend/dependency/mask owner selection");
            }
            for (const auto& generation : fs.pipelineGenerations)
            {
                const auto& identity = generation->shader.compile;
                check(identity.backend == backend && identity.permutation.Size() >= 3 &&
                    !identity.vertexDependencies.empty() && !identity.pixelDependencies.empty() &&
                    LX::Runtime::ResolveGraphicsGeneration(fs.pipelineGenerations, fhandle, fs.permutationKey,
                        fs.bindingLayout, identity.vertexAttributeMask, identity.referencePath) == generation,
                    "Forward exact host/mask/reference owner selection");
            }
            check(!LX::Runtime::ResolveGraphicsGeneration(gs.pipelineGenerations, ShaderMetaHandle{777,2},
                gs.permutationKey, gs.bindingLayout, 0, false), "Draw rejects a foreign shader generation");
            const auto retained = gs.pipelineGenerations;
            auto invalidMeta = gbufferMeta;
            invalidMeta.source = "absent.slang";
            check(!gbuffer.ApplyShaderMeta(context, ShaderMetaHandle{777,2}, invalidMeta, {}, error) &&
                gbuffer.CaptureShaderVariant(gs) && gs.pipelineGenerations == retained,
                "Compile failure preserves accepted native LX draw owners");

            const auto original = retained.front();
            LX::Runtime::GraphicsPipeline request;
            {
                auto desc = original->pipeline.GetDesc();
                std::vector<std::uint8_t> bytes(desc.vsSize);
                std::memcpy(bytes.data(), desc.vsBytecode, bytes.size());
                std::string semantic = "POSITION";
                RHIInputElement input{semantic.c_str(), 0, RHIFormat::RGB32Float, 0, 0, 0};
                desc.vsBytecode = bytes.data(); desc.inputElements = &input; desc.inputElementCount = 1;
                check(request.Create(cache, desc, original->shader, error), "Common graphics request: " + error);
                std::fill(bytes.begin(), bytes.end(), std::uint8_t{}); semantic.assign(64, 'x');
            }
            const auto accepted = request.GetGeneration();
            check(std::memcmp(request.GetDesc().vsBytecode, original->pipeline.GetDesc().vsBytecode,
                request.GetDesc().vsSize) == 0 && std::string_view(request.GetDesc().inputElements[0].semantic) == "POSITION",
                "Common graphics generation owns temporary bytecode and semantic strings");
            auto invalidShader = original->shader;
            invalidShader.compile.vertexDependencies.clear();
            check(!request.Replace(cache, original->pipeline.GetDesc(), invalidShader, {}, error) &&
                request.GetGeneration() == accepted, "Incomplete identity cannot replace accepted generation");
            cache.fail = true;
            check(!request.Replace(cache, original->pipeline.GetDesc(), original->shader, {}, error) &&
                request.GetGeneration() == accepted && cache.invalidations == 0,
                "PSO failure retains accepted generation without retirement");
            cache.fail = false;
            cache.shared = request.GetHandle();
            check(request.Replace(cache, original->pipeline.GetDesc(), original->shader, {}, error) &&
                request.GetGeneration() != accepted && cache.invalidations == 0 && accepted->pipeline.IsValid(),
                "Shared native handle replacement retains captured owner and avoids invalidation");
            LX::Runtime::GraphicsPipeline async;
            check(async.Prepare(original->pipeline.GetDesc(), original->shader, error), "Async candidate preparation");
            cache.pending = true;
            check(async.Poll(cache,error) == RHIPipelineRequestState::Pending && !async.GetGeneration(),
                "Pending graphics candidate is not published");
            check(!async.Prepare(original->pipeline.GetDesc(), original->shader, error) &&
                !async.Create(cache, original->pipeline.GetDesc(), original->shader, error) &&
                !async.Replace(cache, original->pipeline.GetDesc(), original->shader, {}, error),
                "Pending request storage cannot be replaced while backend may borrow it");
            cache.pending = false;
            cache.fail = true;
            check(async.Poll(cache,error) == RHIPipelineRequestState::Failed && !async.GetGeneration(),
                "Failed async candidate is not published");
            cache.fail = false;
            check(async.Poll(cache,error) == RHIPipelineRequestState::Ready && async.GetGeneration(),
                "Successful async retry publishes complete owned generation");
            cache.shared = {};
        }
        EnhancedMaterialDrawSnapshot oldG;
        EnhancedForwardMaterialDrawSnapshot oldF;
        oldG.shaderMetaHandle = ghandle; oldF.shaderMetaHandle = fhandle;
        oldG.keywordSelections = {0}; oldF.keywordSelections = {0};
        std::shared_ptr<const ShaderMetaBindingLayout> oldGL, oldFL;
        check(gbuffer.EnsureShaderMetaVariant(context, ghandle, gbufferMeta, oldG.keywordSelections,
            oldG.permutationKey, oldGL, error) &&
            forward.EnsureShaderMetaVariant(context, fhandle, forwardMeta, oldF.keywordSelections,
                oldF.permutationKey, oldFL, error), "Prepare retained pre-reload frame");
        oldG.bindingLayout = *oldGL; oldF.bindingLayout = *oldFL;
        check(gbuffer.CaptureShaderVariant(oldG) && forward.CaptureShaderVariant(oldF), "Capture retained pre-reload frame");
        const auto oldGOwners = oldG.pipelineGenerations, oldFOwners = oldF.pipelineGenerations;
        const ShaderMetaHandle nextG{777,2}, nextF{888,2};
        check(gbuffer.ApplyShaderMeta(context, nextG, gbufferMeta, {}, error) &&
            forward.ApplyShaderMeta(context, nextF, forwardMeta, {}, error) && cache.invalidations == 0,
            "New PSO preparation does not retire previous frame before sealing succeeds");
        check(gbuffer.CaptureShaderVariant(oldG) && forward.CaptureShaderVariant(oldF) &&
            oldG.pipelineGenerations == oldGOwners && oldF.pipelineGenerations == oldFOwners,
            "A failed later seal can retain exact pre-reload Code draw generations");
        check(gbuffer.CommitShaderMetaFrame(context, std::span(&ghandle,1), {}) == 0 &&
            forward.CommitShaderMetaFrame(context, std::span(&fhandle,1), {}) == 0 && cache.invalidations == 0,
            "Retained old frame keeps native PSOs live");
        check(gbuffer.CommitShaderMetaFrame(context, std::span(&nextG,1), {}) == 2 &&
            forward.CommitShaderMetaFrame(context, std::span(&nextF,1), {}) == 2 && cache.invalidations > 0,
            "Successful new frame commit retires only unused old variants");
    }
    std::cout << "MAT7_PIPELINE_RUNTIME_OK checks=" << checks
              << " nativeCode=true dxil=true spirv=true capturedOwners=true candidateFirst=true\n";
}
