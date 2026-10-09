#include "MaterialGraphSurfaceBatch.h"
#include "MaterialGraphMeshSurface.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace material_graph
{
    namespace
    {
        bool Fail(std::string& error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        bool Finite(std::span<const float> values)
        {
            return std::ranges::all_of(values, [](float value) { return std::isfinite(value) && std::abs(value) <= 1e6f; });
        }

        bool ValidInput(const SurfacePoint& point, const SurfaceView& view)
        {
            const auto values = std::bit_cast<std::array<float, 20>>(point);
            if (!Finite(values) || point.uvLod[3] < 0 || point.uvLod[3] > 32 || point.position[3] < 0)
            {
                return false;
            }
            const auto direction = [](const IblVector& value) { return std::hypot(value[0], value[1], value[2]) > 1e-10; };
            const double distance = std::hypot(double(view.eye[0]) - point.position[0], double(view.eye[1]) - point.position[1],
                                               double(view.eye[2]) - point.position[2]);
            return direction(point.normal) && ValidSurfaceTangentPair(point) && distance > 1e-10;
        }
    } // namespace

    bool ValidSurfaceTangentPair(const SurfacePoint& point)
    {
        const bool tangent = std::hypot(point.tangent[0], point.tangent[1], point.tangent[2]) > 1e-10;
        const bool bitangent = std::hypot(point.bitangent[0], point.bitangent[1], point.bitangent[2]) > 1e-10;
        return tangent == bitangent;
    }

    std::string BuildSurfaceSource(const LX::LXMaterialProgram& program)
    {
        return BuildBoundSource(program) + "\n#include \"MaterialGraphSurfaceHost.slang\"\n";
    }

    bool VerifySurfaceProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& file,
                              std::span<const CompileTarget> targets, RHIShaderCompileOptions options, const Budget& budget,
                              VerifiedProduct& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics)
    {
        std::ifstream input(file, std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (!input || source != BuildSurfaceSource(program) || targets.size() != 6 ||
            !std::ranges::all_of(targets, [](const CompileTarget& target) {
                return (target.entry == "LXEvaluateSurface" && target.profile == "cs_6_0") ||
                       (target.entry == "LXSurfaceVS" && target.profile == "vs_6_0") ||
                       (target.entry == "LXSurfacePS" && target.profile == "ps_6_0");
            }))
        {
            diagnostics.push_back(
                {"MAT_SURFACE_HOST", "Surface product needs the exact spatial evaluation/reference host and targets."});
            return false;
        }
        return VerifyProduct(program, file, targets, {}, std::move(options), {.coreForward = true, .layeredLookup = true},
                             budget, result, diagnostics);
    }

    SurfaceBatch::~SurfaceBatch()
    {
        if (device_ && buffer_.IsValid())
        {
            device_->ReleaseBuffer(buffer_);
        }
    }

    bool SurfaceBatch::Matches(const Instance& instance, const SurfaceView& view,
                               std::span<const SurfacePoint> points) const
    {
        return !mesh_ && bindings_->instance->representationId == instance.representationId && view_.eye == view.eye &&
               view_.sceneEpoch == view.sceneEpoch && view_.viewRevision == view.viewRevision &&
               view_.geometryRevision == view.geometryRevision && points_.size() == points.size() &&
               std::memcmp(points_.data(), points.data(), points.size_bytes()) == 0;
    }

    bool SurfaceBatch::Matches(const Instance& instance, const MeshSurfaceBatch& mesh) const
    {
        return Matches(instance, static_cast<const SurfaceGeometrySource&>(mesh));
    }

    bool SurfaceBatch::Matches(const Instance& instance, const SurfaceGeometrySource& mesh) const
    {
        return mesh_ && bindings_->instance->representationId == instance.representationId && mesh_.get() == &mesh;
    }

    bool SurfaceBatch::ValidateReadback(std::span<const IblBakePoint> points, std::string& error) const
    {
        if (!IsReadyForBake() || points.size() != Count() || (mesh_ && !mesh_->IsValidated()))
        {
            return Fail(error, "Surface validation needs the exact point count and an accepted mesh frame source.");
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            if (mesh_ && !mesh_->IsCovered(static_cast<std::uint32_t>(i)))
            {
                auto background = points[i];
                if (background.viewTier[3] != -1)
                {
                    return Fail(error, "Uncovered surface point must carry the explicit rejection marker.");
                }
                background.viewTier[3] = 0;
                const auto values = std::bit_cast<std::array<float, sizeof(IblBakePoint) / sizeof(float)>>(background);
                if (!std::ranges::all_of(values, [](float value) { return value == 0; }))
                {
                    return Fail(error, "Uncovered surface point contains material payload.");
                }
                continue;
            }
            if (!IsValidIblBakePoint(points[i]))
            {
                return Fail(error, "Surface evaluation rejected point " + std::to_string(i) +
                                       ": nonfinite, unsupported or back-facing material input.");
            }
        }
        validated_ = true;
        error.clear();
        return true;
    }

    bool SurfaceEvaluator::Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                                      IRenderPipelineCache& pipelines, const VerifiedProduct& product,
                                      RHIShaderBinary backend, const Budget& budget, std::string& error)
    {
        std::vector<std::uint8_t> cooked;
        if ((device_ && device_ != &device) || !WriteCookedProgram(product, budget, cooked, error) ||
            !product.program.surface || product.program.volume || product.selection.tier == Tier::Special ||
            product.selection.route != Route::Forward)
        {
            return Fail(error, "Surface evaluator needs a verified Core/Layered Forward program on its owning device.");
        }
        const LX::LXMaterialShaderArtifact* shader{};
        for (std::size_t i = 0; i < product.targets.size(); ++i)
        {
            const auto& target = product.targets[i];
            if (target.binary == backend && target.entry == "LXEvaluateSurface" && target.profile == "cs_6_0")
            {
                if (shader)
                {
                    return Fail(error, "Surface evaluator has duplicate backend CS artifacts.");
                }
                const auto name = backend == RHIShaderBinary::Dxil ? "dxil" : "spirv";
                const auto artifact = std::ranges::find_if(product.shaders, [&](const auto& value) {
                    return value.backend == name && value.entryPoint == target.entry;
                });
                if (artifact == product.shaders.end())
                {
                    return Fail(error, "Surface evaluator has no matching CS bytes.");
                }
                shader = &*artifact;
            }
        }
        if (!shader)
        {
            return Fail(error, "Surface evaluator has no cooked backend CS artifact.");
        }
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::UavBufferTable(1, 0)};
        PassLayout layout;
        if (!CreatePassLayout(roots, product.layout, parameters, {}, false, layout, error))
        {
            return false;
        }
        RHIComputePipelineDesc description;
        description.layout = layout.handle;
        description.csBytecode = shader->bytecode.data();
        description.csSize = shader->bytecode.size();
        LX::Runtime::ComputePipeline pipeline;
        LX::Runtime::ComputeShaderDescription identity;
        if (!DescribeComputeShader(product, backend, "LXEvaluateSurface", identity, error) ||
            !pipeline.Create(pipelines, description, std::move(identity), error))
        {
            return false;
        }
        device_ = &device;
        layout_ = std::move(layout);
        pipeline_ = pipeline.GetGeneration();
        semanticKey_ = product.program.semanticKey;
        principledGgx_ = (product.program.features & 0x17C0u) != 0;
        error.clear();
        return true;
    }

    bool SurfaceEvaluator::Record(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                                  const SurfaceView& view, std::span<const SurfacePoint> points,
                                  std::shared_ptr<const SurfaceBatch>& result, std::string& error)
    {
        return RecordInputs(device, std::move(bindings), view, points, {}, result, error);
    }

    bool SurfaceEvaluator::RecordGpu(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                                     std::shared_ptr<const SurfaceGeometrySource> mesh,
                                     std::shared_ptr<const SurfaceBatch>& result, std::string& error)
    {
        if (!mesh || mesh->Device() != &device || !mesh->Buffer().IsValid() || !mesh->Count() ||
            mesh->Count() > IblBaker::MaxPoints || !mesh->IsReadyForEvaluation() ||
            (mesh->RecordingId() != device.GetCurrentUploadRecordingId() && !mesh->IsValidated()))
        {
            return Fail(error,
                        "GPU surface evaluation needs a live mesh source from this recording or completed validation.");
        }
        const auto view = mesh->View();
        return RecordInputs(device, std::move(bindings), view, {}, std::move(mesh), result, error);
    }

    bool SurfaceEvaluator::RecordInputs(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                                        const SurfaceView& view, std::span<const SurfacePoint> points,
                                        std::shared_ptr<const SurfaceGeometrySource> mesh,
                                        std::shared_ptr<const SurfaceBatch>& result, std::string& error)
    {
        std::shared_ptr<const SurfaceBatch> candidate;
        if (!PrepareInputs(device, std::move(bindings), view, points, std::move(mesh), candidate, error))
        {
            return false;
        }
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{candidate->Buffer(), RHIResourceState::Common, RHIResourceState::UnorderedAccess};
        encoder.ResourceBarriers({{}, {&before, 1}});
        if (!candidate->RecordCommands(encoder, error))
        {
            return false;
        }
        const RHIBufferTransition after{candidate->Buffer(), RHIResourceState::UnorderedAccess,
                                        RHIResourceState::ShaderResource};
        encoder.ResourceBarriers({{}, {&after, 1}});
        candidate->recordedStages_.fetch_or(2);
        result = std::move(candidate);
        return true;
    }

    bool SurfaceEvaluator::PrepareGpu(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                                      std::shared_ptr<const SurfaceGeometrySource> source,
                                      std::shared_ptr<const SurfaceBatch>& result, std::string& error)
    {
        if (!source || source->Device() != &device || !source->Buffer().IsValid() || source->Count() == 0 ||
            source->Count() > IblBaker::MaxPoints || (!source->IsReadyForEvaluation() && !source->IsPreparedForGraph()) ||
            (source->RecordingId() != device.GetCurrentUploadRecordingId() && !source->IsValidated()))
        {
            return Fail(error, "Surface preparation needs a current graph source or an accepted completed geometry owner.");
        }
        const auto view = source->View();
        return PrepareInputs(device, std::move(bindings), view, {}, std::move(source), result, error);
    }

    bool SurfaceEvaluator::PrepareInputs(IRenderDeviceServices& device, own::shared_owner<const RenderBindings> bindings,
                                         const SurfaceView& view, std::span<const SurfacePoint> points,
                                         std::shared_ptr<const SurfaceGeometrySource> mesh,
                                         std::shared_ptr<const SurfaceBatch>& result, std::string& error)
    {
        const auto recordingId = device.GetCurrentUploadRecordingId();
        const auto descriptorVersion = device.GetDescriptorVersionToken();
        if (device_ != &device || !(pipeline_ && pipeline_->IsValid()) || recordingId == 0 || !bindings || !bindings->instance ||
            !bindings->instance->generation || bindings->layout.handle != layout_.handle ||
            bindings->layout.material != layout_.material ||
            bindings->instance->generation->cooked.product.program.semanticKey != semanticKey_ || view.sceneEpoch == 0 ||
            view.viewRevision == 0 || view.geometryRevision == 0 || !Finite(view.eye) ||
            (!mesh && (points.empty() || points.size() > IblBaker::MaxPoints ||
                       !std::ranges::all_of(points, [&](const SurfacePoint& point) { return ValidInput(point, view); }))))
        {
            return Fail(error, "Surface evaluation needs matching bindings and finite spatial/view inputs in a recording.");
        }
        if (!RenderBindingCache::Validate(device, *bindings, error))
        {
            return false;
        }
        auto candidate = std::shared_ptr<SurfaceBatch>(new SurfaceBatch);
        candidate->device_ = &device;
        candidate->view_ = view;
        candidate->points_.assign(points.begin(), points.end());
        candidate->mesh_ = std::move(mesh);
        candidate->count_ = candidate->mesh_ ? candidate->mesh_->Count() : static_cast<std::uint32_t>(points.size());
        candidate->bindings_ = std::move(bindings);
        candidate->recordingId_ = recordingId;
        candidate->descriptorVersion_ = descriptorVersion;
        candidate->pipeline_ = pipeline_;
        RHIBufferDesc description;
        description.bytes = candidate->Count() * sizeof(IblBakePoint);
        description.allowUnorderedAccess = true;
        description.debugName = L"LX.Material.SurfacePoints";
        if (!device.CreateBuffer(description, candidate->buffer_, error))
        {
            return false;
        }
        struct Constants
        {
            std::array<std::uint32_t, 4> countTier;
            IblVector eye;
        };
        const Constants constants{{candidate->Count(), principledGgx_ ? 1u : 0u, 0, 0}, view.eye};
        candidate->inputs_ = candidate->mesh_ ? RHIBufferSlice::Whole(candidate->mesh_->Buffer())
                                              : device.AllocateUpload({points.size_bytes(), RHIUploadUsage::Raw, 16});
        candidate->uniform_ = device.UploadConstants(&constants, sizeof(constants));
        const auto target = RHIBindingDesc::UavBuffer(candidate->buffer_, candidate->Count(), sizeof(IblBakePoint));
        candidate->output_ = device.CreateBindings({&target, 1});
        if (!candidate->inputs_.IsValid() || (!candidate->mesh_ && !candidate->inputs_.IsWritable()) ||
            !candidate->uniform_.IsValid() || !candidate->output_.IsValid() || !candidate->IsPreparedForGraph())
        {
            return Fail(error, "Surface allocation failed or changed recording/descriptors; accepted batch is retained.");
        }
        if (!candidate->mesh_)
        {
            std::memcpy(candidate->inputs_.cpuAddress, points.data(), points.size_bytes());
        }
        candidate->self_ = candidate;
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool SurfaceBatch::IsPreparedForGraph() const
    {
        return recordingId_ != 0 && device_->GetCurrentUploadRecordingId() == recordingId_ &&
               device_->GetDescriptorVersionToken() == descriptorVersion_;
    }

    RGHandle SurfaceBatch::GraphOutput(const EnhancedRenderGraph& graph) const
    {
        return graph_ == &graph && graphEpoch_ == graph.ResourceEpoch() &&
                       graph.ResolveBufferHandle(graphOutput_) == buffer_
                   ? graphOutput_
                   : RGHandle{};
    }

    bool SurfaceBatch::RecordCommands(RHIEncoder& encoder, std::string& error) const
    {
        if (!IsPreparedForGraph() || !RenderBindingCache::Validate(*device_, *bindings_, error) ||
            (recordedStages_.fetch_or(1) & 1) != 0)
        {
            recordedStages_.fetch_or(4);
            return Fail(error, "Surface recording has stale bindings or has already been recorded.");
        }
        encoder.SetPipeline(RHIBindPoint::Compute, pipeline_->GetHandle());
        if (!RenderBindingCache::Bind(*device_, encoder, RHIBindPoint::Compute, *bindings_, error))
        {
            recordedStages_.fetch_or(4);
            return false;
        }
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, inputs_);
        encoder.SetBindings(RHIBindPoint::Compute, 2, output_);
        encoder.Dispatch((Count() + 31) / 32, 1, 1);
        error.clear();
        return true;
    }

    bool SurfaceBatch::Declare(EnhancedRenderGraph& graph, std::string& error) const
    {
        const auto owner = self_.lock();
        if (!owner || &graph.DeviceServices() != device_ || !IsPreparedForGraph() || !mesh_ ||
            recordedStages_.load() != 0 || declared_.load())
        {
            return Fail(error, "Surface declaration needs an unrecorded GPU source and its current prepared owner.");
        }
        auto input = mesh_->GraphOutput(graph);
        if (!input.IsValid() && (mesh_->IsPreparedForGraph() || !mesh_->IsReadyForEvaluation()))
        {
            return Fail(error, "Surface producer must be declared in this graph before its consumer.");
        }
        if (!input.IsValid())
        {
            input = graph.FindImportedBuffer(mesh_->Buffer());
            if (!input.IsValid())
            {
                input = graph.ImportBuffer(mesh_->Buffer(), RHIResourceState::ShaderResource, "LX.Surface.Geometry");
            }
        }
        if (declared_.exchange(true))
        {
            return Fail(error, "Surface packet already belongs to a graph.");
        }
        graph_ = &graph;
        graphEpoch_ = graph.ResourceEpoch();
        graphOutput_ = graph.ImportBuffer(buffer_, RHIResourceState::Common, "LX.Surface.Evaluated");
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto write = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            graphOutput_ = graph.Write(graphOutput_);
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> usages{{input, RHIResourceState::ShaderResource, read},
                                                             {graphOutput_, RHIResourceState::UnorderedAccess, write}};
        for (const auto& binding : bindings_->resources.textures)
        {
            auto texture = graph.FindImportedTexture(binding.resource);
            if (!texture.IsValid())
            {
                texture = graph.ImportTexture(binding.resource, RHIResourceState::ShaderResource, "LX.Surface.Texture");
            }
            if (std::ranges::none_of(usages, [&](const auto& usage) { return usage.handle.index == texture.index; }))
            {
                usages.push_back({texture, RHIResourceState::ShaderResource, read});
            }
        }
        graph.AddPass("LX.EvaluateMaterialSurface", usages, [owner](const auto& context) {
            std::string error;
            if (!context.graph || !context.encoder || !owner->GraphOutput(*context.graph).IsValid() ||
                !owner->RecordCommands(*context.encoder, error))
            {
                owner->recordedStages_.fetch_or(4);
                throw std::runtime_error(error.empty() ? "Invalid surface graph recording owner." : error);
            }
        });
        graph.AddPass(
            "LX.EvaluatedSurfaceReady", {{graphOutput_, RHIResourceState::ShaderResource, read}},
            [owner](const auto&) {
                if (!owner->IsPreparedForGraph())
                {
                    owner->recordedStages_.fetch_or(4);
                    throw std::runtime_error("Stale surface readiness recording.");
                }
                owner->recordedStages_.fetch_or(2);
            },
            true);
        error.clear();
        return true;
    }
} // namespace material_graph
