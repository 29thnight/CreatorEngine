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
    std::array<LX::Runtime::ComputePipeline, 3> compute;
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
                                              RHILayout::Srv(4)};
        const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
        if (!layout.IsValid())
            return false;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        options.bufferInt64Atomics = true;
        const auto source = RHIShaderSource::Resolve("MeteorHybridVisibility.slang").string();
        const char* entries[]{"Initialize", "Classify", "Rasterize"};
        for (unsigned i = 0; i < 3; ++i)
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
    // Pass/resource prefix, e.g. METEOR.Scene.Occluder or METEOR.Scene.Visibility.
    std::string name;
    bool depthOnly{};
    IRenderDeviceServices* device{};
    std::shared_ptr<const SceneFrameGeometry> geometry;
    std::shared_ptr<const GpuGeometryVisibility::Frame> visible;
    std::array<std::shared_ptr<const LX::Runtime::ComputeGeneration>, 3> compute;
    std::shared_ptr<const LX::Runtime::GraphicsGeneration> resolve;
    std::vector<std::array<uint32_t, 4>> records;
    RHIBufferSlice constants, metadata;
    RHIBufferHandle keys, routes;
    RHIBindingTable vertices, topologies;
    uint32_t width{}, height{}, meshlets{};
    mutable RGHandle graphKeys, graphRoutes, graphDepth;
    mutable const EnhancedRenderGraph* graphOwner{};
    mutable uint64_t graphEpoch{};
    ~SceneHybridVisibility()
    {
        if (device)
        {
            if (keys.IsValid())
                device->ReleaseBuffer(keys);
            if (routes.IsValid())
                device->ReleaseBuffer(routes);
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
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& projection,
                 HybridVisibilityPipelines& pipelines, std::string& error)
    {
        device = context.resources;
        width = context.width;
        height = context.height;
        meshlets = (geometry->nextPrimitiveAddress - 1) / 128;
        if (name.empty() || !device->GetAtomicCapabilities().bufferInt64Min || !visible || !meshlets || !width ||
            !height || uint64_t(width) * height > UINT32_MAX || geometry->records.size() > 4097 ||
            records.size() != geometry->records.size())
        {
            error = "METEOR hybrid visibility requires atomic64, bounded frame geometry and GPU culling; no fallback.";
            return false;
        }
        if (!pipelines.Prepare(context, error))
            return false;
        for (unsigned i = 0; i < 3; ++i)
            compute[i] = pipelines.compute[i].GetGeneration();
        resolve = (depthOnly ? pipelines.resolveDepth : pipelines.resolve).GetGeneration();
        struct Constants
        {
            math::matrix4x4 projection;
            uint32_t width, height, records, meshlets;
        };
        static_assert(sizeof(Constants) == 80);
        const Constants values{math::transpose(projection), width, height, static_cast<uint32_t>(records.size()),
                               meshlets};
        constants = device->UploadConstants(&values, sizeof(values));
        metadata = device->AllocateUpload(records.size() * 16, 16);
        if (!constants.IsValid() || !metadata.IsWritable())
        {
            error = "METEOR raster metadata upload failed.";
            return false;
        }
        std::memcpy(metadata.cpuAddress, records.data(), records.size() * 16);
        RHIBufferDesc desc;
        desc.bytes = KeyBytes();
        desc.allowUnorderedAccess = true;
        const std::wstring keyName(std::wstring(name.begin(), name.end()) + L".Keys");
        const std::wstring routeName(std::wstring(name.begin(), name.end()) + L".Routes");
        desc.debugName = keyName.c_str();
        if (!device->CreateBuffer(desc, keys, error))
            return false;
        desc.bytes = uint64_t(meshlets) * 4;
        desc.debugName = routeName.c_str();
        if (!device->CreateBuffer(desc, routes, error))
            return false;
        std::vector<RHIBindingDesc> v, t;
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
    void Bind(RHIEncoder& e, unsigned stage) const
    {
        e.SetPipeline(RHIBindPoint::Compute, compute[stage]->GetHandle());
        e.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
        if (stage != 0)
        {
            e.SetRootBuffer(RHIBindPoint::Compute, 1, RHIBufferSlice::Whole(geometry->buffer));
            e.SetRootBuffer(RHIBindPoint::Compute, 2, metadata);
            e.SetBindings(RHIBindPoint::Compute, 4, vertices);
            e.SetBindings(RHIBindPoint::Compute, 5, topologies);
        }
        if (stage == 1)
            e.SetRootBuffer(RHIBindPoint::Compute, 3, RHIBufferSlice::Whole(visible->VisibleIdBuffer()));
        if (stage != 2 && !e.SetRootUnorderedBuffer(RHIBindPoint::Compute, 6, RouteSlice()))
            throw std::runtime_error("METEOR raster route UAV binding failed.");
        if (stage != 1 && !e.SetRootUnorderedBuffer(RHIBindPoint::Compute, 7, KeySlice()))
            throw std::runtime_error("METEOR raster key UAV binding failed.");
        if (stage == 2)
            e.SetRootBuffer(RHIBindPoint::Compute, 8, RouteSlice());
    }
    void GeometryReads(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
    {
        uses.push_back({geometry->GraphTable(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
        for (size_t i = 1; i < records.size(); ++i)
            if (records[i][1] & 1u)
            {
                uses.push_back(
                    {geometry->geometry[i]->GraphOutput(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});
                uses.push_back({geometry->geometry[i]->GraphMeshlets(graph), RHIResourceState::ShaderResource,
                                RGAccessMode::Read});
            }
        NormalizeSceneReads(graph, uses);
    }
    void Declare(EnhancedRenderGraph& graph, std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        if (graphOwner || graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            throw std::runtime_error("METEOR raster requires one explicit versioned declaration per frame.");
        graphOwner = &graph;
        graphEpoch = graph.ResourceEpoch();
        graphKeys = graph.ImportBuffer(keys, RHIResourceState::Common, (name + ".Keys").c_str());
        graphRoutes = graph.ImportBuffer(routes, RHIResourceState::Common, (name + ".Routes").c_str());
        graph.RequireImportedFinalState(graphKeys, RHIResourceState::Common);
        graph.RequireImportedFinalState(graphRoutes, RHIResourceState::Common);
        graphKeys = graph.Write(graphKeys);
        graphRoutes = graph.Write(graphRoutes);
        graph.AddPass(
            (name + ".Initialize").c_str(),
            {{graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
             {graphRoutes, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [owner](const auto& x) {
                owner->Bind(*x.encoder, 0);
                DispatchGroups(
                    *x.encoder,
                    static_cast<uint32_t>(
                        ((std::max)(uint64_t(owner->width) * owner->height, uint64_t(owner->meshlets)) + 63) / 64));
            });
        graphKeys = graph.AtomicMin64(graphKeys);
        graphRoutes = graph.Modify(graphRoutes);
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {graphRoutes, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite}};
        GeometryReads(graph, uses);
        visible->AddVisibleIdsRead(graph, uses);
        graph.AddPass((name + ".Classify").c_str(), uses, [owner](const auto& x) {
            owner->Bind(*x.encoder, 1);
            DispatchGroups(*x.encoder, owner->meshlets);
        });
    }
    void AddRasterUses(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
    {
        if (graphOwner != &graph || graphEpoch != graph.ResourceEpoch())
            throw std::runtime_error("METEOR raster consumer requires explicit classification.");
        uses.push_back({graphRoutes, RHIResourceState::ShaderResource, RGAccessMode::Read});
        uses.push_back({graphKeys, RHIResourceState::UnorderedAccess, RGAccessMode::AtomicMin64});
    }
    void DeclareSoftware(EnhancedRenderGraph& graph, std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        AddRasterUses(graph, uses);
        GeometryReads(graph, uses);
        graph.AddPass((name + ".SW").c_str(), uses, [owner](const auto& x) {
            owner->Bind(*x.encoder, 2);
            DispatchGroups(*x.encoder, owner->meshlets);
        });
    }
    // Occluder stage: the merged atomic keys become the HZB source depth.
    RGHandle DeclareDepthResolve(EnhancedRenderGraph& graph, RGHandle depth,
                                 std::shared_ptr<const SceneHybridVisibility> owner) const
    {
        if (!depthOnly)
            throw std::runtime_error("METEOR visibility stage resolves its visibility target too.");
        depth = graph.Write(depth);
        graphDepth = depth;
        graph.AddPass((name + ".Resolve").c_str(),
                      {{graphKeys, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                       {depth, RHIResourceState::DepthWrite, RGAccessMode::Write}},
                      [owner, depth](const auto& x) {
                          auto& e = *x.encoder;
                          const auto target = RHIDepthTargetDesc::Depth(x.ResolveHandle(depth), RHIFormat::D32Float);
                          const auto targets =
                              owner->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &target);
                          if (!targets.IsValid())
                              throw std::runtime_error("METEOR occluder resolve target binding failed.");
                          owner->DrawResolve(e, targets);
                      });
        return depth;
    }
    RGHandle ResolvedDepth(const EnhancedRenderGraph& graph) const
    {
        if (graphOwner != &graph || graphEpoch != graph.ResourceEpoch() || !graphDepth.IsValid())
            throw std::runtime_error("METEOR depth requires its explicit resolve producer.");
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
        graph.AddPass((name + ".Resolve").c_str(),
                      {{graphKeys, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                       {outputs.visibility, RHIResourceState::RenderTarget, RGAccessMode::Write},
                       {outputs.depth, RHIResourceState::DepthWrite, RGAccessMode::Write}},
                      [owner, outputs](const auto& x) {
                          auto& e = *x.encoder;
                          const auto depth =
                              RHIDepthTargetDesc::Depth(x.ResolveHandle(outputs.depth), RHIFormat::D32Float);
                          const std::array colors{x.ResolveHandle(outputs.visibility)};
                          const auto targets = owner->device->CreateRenderTargets(colors, &depth);
                          if (!targets.IsValid())
                              throw std::runtime_error("METEOR visibility resolve target binding failed.");
                          owner->DrawResolve(e, targets);
                      });
        return outputs;
    }
};
} // namespace material_graph
