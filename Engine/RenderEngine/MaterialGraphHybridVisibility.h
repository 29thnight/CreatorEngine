#pragma once
#include "MaterialGraphFrameGeometry.h"
#include "GpuGeometryVisibility.h"
#include "RHI/RHIShaderSource.h"
#include "MaterialGraphSceneAccess.h"
#include "Render/Passes/Geometry/SceneSurfaceTargets.h"

namespace material_graph
{
struct HybridVisibilityPipelines final
{
    enum Stage : unsigned { Initialize, PrepareClassify, Classify, PrepareSoftware, Rasterize, Count };
    std::array<LX::Runtime::ComputePipeline, Count> compute;
    // Final surface writes visibility + depth; the occluder stage writes depth only.
    LX::Runtime::GraphicsPipeline resolve, resolveDepth;
    bool Prepare(const EnhancedFrameContext& context, std::string& error)
    {
        if (resolve.IsValid() && resolveDepth.IsValid())
            return true;
        const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0),
                                              RHILayout::Srv(0),
                                              RHILayout::Srv(1),
                                              RHILayout::Srv(2),
                                              RHILayout::SrvBufferTable(4096, 16),
                                              RHILayout::SrvBufferTable(4096, 4112),
                                              RHILayout::Uav(0),
                                              RHILayout::Uav(1),
                                              RHILayout::Uav(2),
                                              RHILayout::Uav(3),
                                              RHILayout::Srv(5)};
        const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
        if (!layout.IsValid())
            return false;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        options.bufferInt64Atomics = true;
        const auto source = RHIShaderSource::Resolve("MeteorHybridVisibility.slang").string();
        const char* entries[]{"Initialize", "PrepareClassify", "Classify", "PrepareSoftware", "Rasterize"};
        static_assert(std::size(entries) == Count);
        for (unsigned i = 0; i < Count; ++i)
        {
            if (!compute[i].IsValid())
            {
                LX::Runtime::CompiledCompute shader;
                if (!LX::Runtime::CompileCompute(source, entries[i], {}, options, shader, error, "cs_6_6"))
                    return false;
                RHIComputePipelineDesc desc;
                desc.layout = layout;
                desc.csBytecode = shader.stage.bytecode.Data();
                desc.csSize = shader.stage.bytecode.Size();
                if (!compute[i].Create(*context.psoManager, desc, std::move(shader.description), error))
                    return false;
            }
        }
        const RHIPipelineLayoutParam finalParams[]{RHILayout::Cbv(0), RHILayout::Srv(3, RHIShaderVisibility::Pixel)};
        const auto finalLayout = context.rootSignatures->GetOrCreate({finalParams, {}}, error);
        if (!finalLayout.IsValid())
            return false;
        const auto create = [&](LX::Runtime::GraphicsPipeline& pipeline, const char* pixel, bool visibility) {
            if (pipeline.IsValid())
                return true;
            LX::Runtime::CompiledGraphics shader;
            if (!LX::Runtime::CompileGraphics(source, "ResolveVS", pixel, {}, options, shader, error))
                return false;
            RHIGraphicsPipelineDesc desc;
            desc.layout = finalLayout;
            desc.vsBytecode = shader.vertex.bytecode.Data();
            desc.vsSize = shader.vertex.bytecode.Size();
            desc.psBytecode = shader.pixel.bytecode.Data();
            desc.psSize = shader.pixel.bytecode.Size();
            desc.numRenderTargets = visibility ? 1 : 0;
            if (visibility)
                desc.rtvFormats[0] = RHIFormat::RG32Uint;
            desc.dsvFormat = RHIFormat::D32Float;
            desc.depthEnable = true;
            desc.depthWriteMask = RHIDepthWrite::All;
            desc.depthFunc = RHICompareOp::Always;
            desc.cullMode = RHICullMode::None;
            LX::Runtime::GraphicsShaderDescription description;
            description.compile = std::move(shader.identity);
            return pipeline.Create(*context.psoManager, desc, std::move(description), error);
        };
        return create(resolve, "ResolvePS", true) && create(resolveDepth, "ResolveDepthPS", false);
    }
};
// The frame prepares storage and immutable binding tables. Declaration only
// publishes producers and exact consumers; no hidden producer on a read.
// One owner per stage: the occluder stage consumes the first culling list and
// resolves depth only; the final surface consumes the recovered union.
struct SceneHybridVisibility final
{
    using Pipeline = HybridVisibilityPipelines;
    // Keep in sync with MeteorHybridVisibility.slang. The work buffer contains
    // four header words, recordCount+1 prefixes and two words per software item.
    static constexpr uint32_t WorkHeaderWords = 4;
    static constexpr uint64_t SoftwareArgumentsOffset = sizeof(RHIDispatchIndirectArguments);
    static constexpr uint64_t DispatchArgumentBytes = 2 * sizeof(RHIDispatchIndirectArguments);
    // Pass/resource prefix, e.g. METEOR.Scene.Occluder or METEOR.Scene.Visibility.
    std::string name;
    bool depthOnly{};
    IRenderDeviceServices* device{};
    std::shared_ptr<TransientBufferPool> pool;
    std::shared_ptr<const SceneFrameGeometry> geometry;
    std::shared_ptr<const GpuGeometryVisibility::Frame> visible;
    std::array<std::shared_ptr<const LX::Runtime::ComputeGeneration>, Pipeline::Count> compute;
    std::shared_ptr<const LX::Runtime::GraphicsGeneration> resolve;
    std::vector<std::array<uint32_t, 4>> records;
    RHIBufferSlice constants, metadata;
    RHIBufferHandle keys, routes, work, dispatchArguments;
    RHIBindingTable vertices, topologies;
    uint32_t width{}, height{}, meshlets{};
    uint64_t recording{}, descriptorVersion{};
    math::matrix4x4 preparedProjection{};
    mutable RGHandle graphKeys, graphRoutes, graphWork, graphDispatchArguments, graphDepth;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable uint64_t graphEpoch{};
    ~SceneHybridVisibility()
    {
        if (device)
        {
            ReleaseSceneBuffer(*device, pool, keys);
            ReleaseSceneBuffer(*device, pool, routes);
            ReleaseSceneBuffer(*device, pool, work);
            ReleaseSceneBuffer(*device, pool, dispatchArguments);
        }
    }
    uint64_t KeyBytes() const { return uint64_t(width) * height * 8; }
    RHIBufferSlice KeySlice() const
    {
        auto s = RHIBufferSlice::Whole(keys);
        s.size = KeyBytes();
        return s;
    }
    RHIBufferSlice RouteSlice() const
    {
        auto s = RHIBufferSlice::Whole(routes);
        s.size = uint64_t(meshlets) * 4;
        return s;
    }
    uint64_t WorkBytes() const
    {
        return (uint64_t(WorkHeaderWords) + records.size() + 1 + uint64_t(meshlets) * 2) * sizeof(uint32_t);
    }
    RHIBufferSlice WorkSlice() const
    {
        auto s = RHIBufferSlice::Whole(work);
        s.size = WorkBytes();
        return s;
    }
    RHIBufferSlice DispatchArgumentSlice() const
    {
        auto s = RHIBufferSlice::Whole(dispatchArguments);
        s.size = DispatchArgumentBytes;
        return s;
    }
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& projection,
                 HybridVisibilityPipelines& pipelines, std::string& error,
                 const SceneHybridVisibility* sharedPreparation = nullptr)
    {
        device = context.resources;
        if (!device || !geometry)
        {
            error = "METEOR hybrid visibility requires a device and frame geometry.";
            return false;
        }
        width = context.width;
        height = context.height;
        recording = device->GetCurrentUploadRecordingId();
        descriptorVersion = device->GetDescriptorVersionToken();
        preparedProjection = projection;
        meshlets = (geometry->nextPrimitiveAddress - 1) / 128;
        if (name.empty() || !device->GetAtomicCapabilities().bufferInt64Min || !visible || !meshlets || !width ||
            !height || uint64_t(width) * height > UINT32_MAX || geometry->records.size() < 2 ||
            geometry->records.size() > 4097 || geometry->geometry.size() != geometry->records.size() ||
            records.size() != geometry->records.size())
        {
            error = "METEOR hybrid visibility requires atomic64, bounded frame geometry and GPU culling; no fallback.";
            return false;
        }
        if (!pipelines.Prepare(context, error))
            return false;
        for (unsigned i = 0; i < Pipeline::Count; ++i)
        {
            compute[i] = pipelines.compute[i].GetGeneration();
        }
        resolve = (depthOnly ? pipelines.resolveDepth : pipelines.resolve).GetGeneration();
        struct Constants
        {
            math::matrix4x4 projection;
            uint32_t width, height, records, meshlets;
        };
        static_assert(sizeof(Constants) == 80);
        const Constants values{math::transpose(projection), width, height, static_cast<uint32_t>(records.size()),
                               meshlets};
        // First/final culling own different results. Only immutable preparation
        // can be shared, and only inside the same recording/descriptor lifetime.
        const bool shareGeometry = sharedPreparation && sharedPreparation->device == device &&
            sharedPreparation->geometry == geometry && sharedPreparation->recording == recording &&
            sharedPreparation->descriptorVersion == descriptorVersion && sharedPreparation->vertices.IsValid() &&
            sharedPreparation->topologies.IsValid();
        const bool shareConstants = shareGeometry && sharedPreparation->width == width &&
            sharedPreparation->height == height &&
            std::memcmp(&sharedPreparation->preparedProjection, &projection, sizeof(projection)) == 0;
        const bool shareMetadata = shareGeometry && sharedPreparation->records == records;
        constants = shareConstants ? sharedPreparation->constants : device->UploadConstants(&values, sizeof(values));
        metadata = shareMetadata ? sharedPreparation->metadata : device->AllocateUpload(records.size() * 16, 16);
        if (!constants.IsValid() || !metadata.IsValid() || (!shareMetadata && !metadata.IsWritable()))
        {
            error = "METEOR raster metadata upload failed.";
            return false;
        }
        if (!shareMetadata)
        {
            std::memcpy(metadata.cpuAddress, records.data(), records.size() * 16);
        }
        RHIBufferDesc desc;
        desc.bytes = KeyBytes();
        desc.allowUnorderedAccess = true;
        const std::wstring keyName(std::wstring(name.begin(), name.end()) + L".Keys");
        const std::wstring routeName(std::wstring(name.begin(), name.end()) + L".Routes");
        desc.debugName = keyName.c_str();
        if (!CreateSceneBuffer(*device, pool.get(), desc, keys, error))
            return false;
        desc.bytes = uint64_t(meshlets) * 4;
        desc.debugName = routeName.c_str();
        if (!CreateSceneBuffer(*device, pool.get(), desc, routes, error))
            return false;
        const std::wstring workName(std::wstring(name.begin(), name.end()) + L".Work");
        const std::wstring argumentsName(std::wstring(name.begin(), name.end()) + L".DispatchArguments");
        desc.bytes = WorkBytes();
        desc.debugName = workName.c_str();
        if (!CreateSceneBuffer(*device, pool.get(), desc, work, error))
        {
            return false;
        }
        desc.bytes = DispatchArgumentBytes;
        desc.allowIndirectArguments = true;
        desc.debugName = argumentsName.c_str();
        if (!CreateSceneBuffer(*device, pool.get(), desc, dispatchArguments, error))
        {
            return false;
        }
        if (shareGeometry)
        {
            vertices = sharedPreparation->vertices;
            topologies = sharedPreparation->topologies;
            return true;
        }
        std::vector<RHIBindingDesc> v, t;
        v.reserve(geometry->geometry.size() - 1);
        t.reserve(geometry->geometry.size() - 1);
        for (size_t i = 1; i < geometry->geometry.size(); ++i)
        {
            const auto& g = geometry->geometry[i];
            const auto topology = g->Meshlets();
            v.push_back(RHIBindingDesc::SrvBuffer(g->Buffer(), g->GeometryViewElements(),
                                                  MeshSurfaceBatch::GeometryViewStride));
            t.push_back(RHIBindingDesc::SrvBuffer(topology.buffer, static_cast<uint32_t>(topology.size / 4), 0,
                                                  static_cast<uint32_t>(topology.offset / 4)));
        }
        vertices = device->CreateBindings(v);
        topologies = device->CreateBindings(t);
        if (!vertices.IsValid() || !topologies.IsValid())
        {
            error = "METEOR raster geometry tables failed.";
            return false;
        }
        return true;
    }
    static void DispatchGroups(RHIEncoder& e, uint32_t count)
    {
        e.Dispatch((std::min)(count, 65535u), (count + 65534u) / 65535u, 1);
    }
    void Bind(RHIEncoder& e, Pipeline::Stage stage) const
    {
        e.SetPipeline(RHIBindPoint::Compute, compute[stage]->GetHandle());
        e.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
        if (stage == Pipeline::PrepareClassify || stage == Pipeline::Classify || stage == Pipeline::Rasterize)
        {
            e.SetRootBuffer(RHIBindPoint::Compute, 1, RHIBufferSlice::Whole(geometry->buffer));
            e.SetRootBuffer(RHIBindPoint::Compute, 2, metadata);
        }
        if (stage == Pipeline::Classify || stage == Pipeline::Rasterize)
        {
            e.SetBindings(RHIBindPoint::Compute, 4, vertices);
            e.SetBindings(RHIBindPoint::Compute, 5, topologies);
        }
        if (stage == Pipeline::PrepareClassify || stage == Pipeline::Classify)
        {
            e.SetRootBuffer(RHIBindPoint::Compute, 3, RHIBufferSlice::Whole(visible->VisibleIdBuffer()));
        }
        if ((stage == Pipeline::Initialize || stage == Pipeline::Classify) &&
            !e.SetRootUnorderedBuffer(RHIBindPoint::Compute, 6, RouteSlice()))
        {
            throw std::runtime_error("METEOR raster route UAV binding failed.");
        }
        if ((stage == Pipeline::Initialize || stage == Pipeline::Rasterize) &&
            !e.SetRootUnorderedBuffer(RHIBindPoint::Compute, 7, KeySlice()))
        {
            throw std::runtime_error("METEOR raster key UAV binding failed.");
        }
        if ((stage == Pipeline::PrepareClassify || stage == Pipeline::Classify) &&
            !e.SetRootUnorderedBuffer(RHIBindPoint::Compute, 8, WorkSlice()))
        {
            throw std::runtime_error("METEOR raster work UAV binding failed.");
        }
        if ((stage == Pipeline::PrepareClassify || stage == Pipeline::PrepareSoftware) &&
            !e.SetRootUnorderedBuffer(RHIBindPoint::Compute, 9, DispatchArgumentSlice()))
        {
            throw std::runtime_error("METEOR raster dispatch UAV binding failed.");
        }
        if (stage == Pipeline::PrepareSoftware || stage == Pipeline::Rasterize)
        {
            e.SetRootBuffer(RHIBindPoint::Compute, 10, WorkSlice());
        }
    }
    void GeometryReads(EnhancedRenderGraph& graph, auto& uses) const
    {
        uses.push_back({geometry->GraphTable(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        for (size_t i = 1; i < records.size(); ++i)
        {
            if ((records[i][1] & 3u) == 3u)
            {
                uses.push_back(
                    {geometry->geometry[i]->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
                uses.push_back({geometry->geometry[i]->GraphMeshlets(graph), RHIResourceState::ShaderResource,
                                RGAccessMode::Read});
            }
        }
        NormalizeSceneReads(graph, uses);
    }
    void Declare(EnhancedRenderGraph& graph, std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        if (graphOwner || graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            throw std::runtime_error("METEOR raster requires one explicit versioned declaration per frame.");
        graphOwner = graph.DeclarationGraph();
        graphEpoch = graph.DeclarationEpoch();
        graphKeys = graph.ImportBuffer(keys, RHIResourceState::Common, (name + ".Keys").c_str());
        graphRoutes = graph.ImportBuffer(routes, RHIResourceState::Common, (name + ".Routes").c_str());
        graphWork = graph.ImportBuffer(work, RHIResourceState::Common, (name + ".Work").c_str());
        graphDispatchArguments =
            graph.ImportBuffer(dispatchArguments, RHIResourceState::Common, (name + ".DispatchArguments").c_str());
        graph.RequireImportedFinalState(graphKeys, RHIResourceState::Common);
        graph.RequireImportedFinalState(graphRoutes, RHIResourceState::Common);
        graph.RequireImportedFinalState(graphWork, RHIResourceState::Common);
        graph.RequireImportedFinalState(graphDispatchArguments, RHIResourceState::Common);
        graphKeys = graph.Write(graphKeys);
        graphRoutes = graph.Write(graphRoutes);
        const auto initializePass = graph.AddPass(
            (name + ".Initialize").c_str(),
            {{graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
             {graphRoutes, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [owner](const auto& x) {
                owner->Bind(*x.encoder, Pipeline::Initialize);
                DispatchGroups(
                    *x.encoder,
                    static_cast<uint32_t>(
                        ((std::max)(uint64_t(owner->width) * owner->height, uint64_t(owner->meshlets)) + 63) / 64));
            });
        // 콜백은 준비된 입력과 자기 encoder만 사용한다. GPU 버전/배리어는
        // 그대로 유지하고, GPU 의존성만을 근거로 한 CPU recording join을 생략한다.
        graph.DeclareRecordingIndependent(initializePass);
        graphWork = graph.Write(graphWork);
        graphDispatchArguments = graph.Write(graphDispatchArguments);
        // The graph copies usages synchronously; no callback borrows this scratch.
        auto uses = graph.CreateDeclarationUsages();
        uses.assign({{graphWork, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
                     {graphDispatchArguments, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
                     {geometry->GraphTable(graph), RHIResourceState::ShaderResource, RGAccessMode::Read}});
        visible->AddVisibleIdsRead(graph, uses);
        const auto preparePass = graph.AddPass((name + ".PrepareClassify").c_str(), std::span{uses},
                                               [owner](const auto& x)
                                               {
                                                   owner->Bind(*x.encoder, Pipeline::PrepareClassify);
                                                   x.encoder->Dispatch(1, 1, 1);
                                               });
        graph.DeclareRecordingIndependent(preparePass);
        graphKeys = graph.AtomicMin64(graphKeys);
        graphRoutes = graph.Modify(graphRoutes);
        graphWork = graph.Modify(graphWork);
        uses.assign({{graphRoutes, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite},
                     {graphWork, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite},
                     {graphDispatchArguments, RHIResourceState::IndirectArgument, RGAccessMode::Read}});
        GeometryReads(graph, uses);
        visible->AddVisibleIdsRead(graph, uses);
        const auto classifyPass =
            graph.AddPass((name + ".Classify").c_str(), std::span{uses},
                          [owner](const auto& x)
                          {
                              owner->Bind(*x.encoder, Pipeline::Classify);
                              if (!x.encoder->DispatchIndirect(owner->dispatchArguments))
                              {
                                  throw std::runtime_error("METEOR classifier indirect dispatch failed.");
                              }
                          });
        graph.DeclareRecordingIndependent(classifyPass);
    }
    void AddRasterUses(EnhancedRenderGraph& graph, auto& uses) const
    {
        if (graphOwner != graph.DeclarationGraph() || graphEpoch != graph.DeclarationEpoch())
        {
            throw std::runtime_error("METEOR raster consumer requires explicit classification.");
        }
        uses.push_back({graphRoutes, RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::AtomicMin64});
    }
    void DeclareSoftware(EnhancedRenderGraph& graph, std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        if (graphOwner != graph.DeclarationGraph() || graphEpoch != graph.DeclarationEpoch())
        {
            throw std::runtime_error("METEOR software raster requires explicit classification.");
        }
        graphDispatchArguments = graph.Modify(graphDispatchArguments);
        const auto preparePass =
            graph.AddPass((name + ".PrepareSoftware").c_str(),
                          {{graphWork, RHIResourceState::ShaderResource, RGAccessMode::Read},
                           {graphDispatchArguments, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite}},
                          [owner](const auto& x)
                          {
                              owner->Bind(*x.encoder, Pipeline::PrepareSoftware);
                              x.encoder->Dispatch(1, 1, 1);
                          });
        graph.DeclareRecordingIndependent(preparePass);
        auto uses = graph.CreateDeclarationUsages();
        uses.assign({{graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::AtomicMin64},
                     {graphWork, RHIResourceState::ShaderResource, RGAccessMode::Read},
                     {graphDispatchArguments, RHIResourceState::IndirectArgument, RGAccessMode::Read}});
        GeometryReads(graph, uses);
        const auto rasterPass =
            graph.AddPass((name + ".SW").c_str(), std::span{uses},
                          [owner](const auto& x)
                          {
                              owner->Bind(*x.encoder, Pipeline::Rasterize);
                              if (!x.encoder->DispatchIndirect(owner->dispatchArguments, SoftwareArgumentsOffset))
                              {
                                  throw std::runtime_error("METEOR software raster indirect dispatch failed.");
                              }
                          });
        graph.DeclareRecordingIndependent(rasterPass);
    }
    // Occluder stage: the merged atomic keys become the HZB source depth.
    RGHandle DeclareDepthResolve(EnhancedRenderGraph& graph, RGHandle depth,
                                 std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        if (!depthOnly)
            throw std::runtime_error("METEOR visibility stage resolves its visibility target too.");
        depth = graph.Write(depth);
        graphDepth = depth;
        const auto resolvePass = graph.AddPass(
            (name + ".Resolve").c_str(),
            {{graphKeys, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
             {depth, RHIResourceState::DepthWrite, RGAccessMode::Write}},
            [owner, depth](const auto& x) {
                auto& e = *x.encoder;
                const auto target = RHIDepthTargetDesc::Depth(x.ResolveHandle(depth), RHIFormat::D32Float);
                const auto targets =
                    owner->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &target);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("METEOR occluder resolve target binding failed.");
                }
                owner->DrawResolve(e, targets);
            });
        graph.DeclareRecordingIndependent(resolvePass);
        return depth;
    }
    RGHandle ResolvedDepth(const EnhancedRenderGraph& graph) const
    {
        if (graphOwner != graph.DeclarationGraph() || graphEpoch != graph.DeclarationEpoch() || !graphDepth.IsValid())
        {
            throw std::runtime_error("METEOR depth requires its explicit resolve producer.");
        }
        return graphDepth;
    }
    void DrawResolve(RHIEncoder& e, const RHIRenderTargetBinding& targets) const
    {
        e.BindRenderTargets(targets);
        e.SetPipeline(RHIBindPoint::Graphics, resolve->pipeline.GetHandle());
        e.SetViewportAndScissor(width, height);
        e.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        e.SetConstantBuffer(RHIBindPoint::Graphics, 0, constants);
        e.SetRootBuffer(RHIBindPoint::Graphics, 1, KeySlice());
        e.Draw(3, 1);
    }
    SceneSurfaceTargets::Outputs DeclareResolve(EnhancedRenderGraph& graph, SceneSurfaceTargets::Outputs outputs,
                                                std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        if (depthOnly)
            throw std::runtime_error("METEOR occluder stage has no visibility target.");
        outputs.visibility = graph.Write(outputs.visibility);
        outputs.depth = graph.Write(outputs.depth);
        graphDepth = outputs.depth;
        const auto resolvePass = graph.AddPass(
            (name + ".Resolve").c_str(),
            {{graphKeys, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
             {outputs.visibility, RHIResourceState::RenderTarget, RGAccessMode::Write},
             {outputs.depth, RHIResourceState::DepthWrite, RGAccessMode::Write}},
            [owner, outputs](const auto& x) {
                auto& e = *x.encoder;
                const auto depth = RHIDepthTargetDesc::Depth(x.ResolveHandle(outputs.depth), RHIFormat::D32Float);
                const std::array colors{x.ResolveHandle(outputs.visibility)};
                const auto targets = owner->device->CreateRenderTargets(colors, &depth);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("METEOR visibility resolve target binding failed.");
                }
                owner->DrawResolve(e, targets);
            });
        graph.DeclareRecordingIndependent(resolvePass);
        return outputs;
    }
};
} // namespace material_graph
