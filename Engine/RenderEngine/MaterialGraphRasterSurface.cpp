#include "MaterialGraphRasterSurface.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
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

bool SameView(const SurfaceView& a, const SurfaceView& b)
{
    return a.eye == b.eye && a.sceneEpoch == b.sceneEpoch && a.viewRevision == b.viewRevision &&
           a.geometryRevision == b.geometryRevision;
}

bool ValidCamera(const math::matrix4x4& camera)
{
    std::array<std::array<double, 4>, 4> matrix;
    for (unsigned r = 0; r < 4; ++r)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            const auto value = camera.m[r][c];
            if (!std::isfinite(value) || std::abs(value) > 1e6f)
            {
                return false;
            }
            matrix[r][c] = value;
        }
    }
    for (unsigned c = 0; c < 4; ++c)
    {
        unsigned pivot = c;
        for (unsigned r = c + 1; r < 4; ++r)
        {
            if (std::abs(matrix[r][c]) > std::abs(matrix[pivot][c]))
            {
                pivot = r;
            }
        }
        if (std::abs(matrix[pivot][c]) <= 1e-12)
        {
            return false;
        }
        std::swap(matrix[c], matrix[pivot]);
        for (unsigned r = c + 1; r < 4; ++r)
        {
            const double factor = matrix[r][c] / matrix[c][c];
            for (unsigned k = c + 1; k < 4; ++k)
            {
                matrix[r][k] -= factor * matrix[c][k];
            }
        }
    }
    return true;
}
} // namespace

RasterSurfaceBatch::~RasterSurfaceBatch()
{
    if (device_)
    {
        for (auto target : targets_)
        {
            if (target.IsValid())
            {
                device_->ReleaseTexture(target);
            }
        }
        if (!depthSource_ && depth_.IsValid())
        {
            device_->ReleaseTexture(depth_);
        }
        if (buffer_.IsValid())
        {
            device_->ReleaseBuffer(buffer_);
        }
    }
}

bool RasterSurfaceBatch::IsCurrent() const
{
    return (recordedStages_.load() & 8) == 0 && recordingId_ != 0 &&
           device_->GetCurrentUploadRecordingId() == recordingId_ &&
           device_->GetDescriptorVersionToken() == descriptorVersion_;
}

bool RasterSurfaceBatch::IsCovered(std::uint32_t index) const
{
    return validated_ && index < coverage_.size() && coverage_[index] != 0;
}

RGHandle RasterSurfaceBatch::GraphOutput(const EnhancedRenderGraph& graph) const
{
    return graph_ == &graph && graphEpoch_ == graph.ResourceEpoch() &&
                   graph.ResolveBufferHandle(graphOutput_) == buffer_
               ? graphOutput_
               : RGHandle{};
}

bool RasterSurfaceBatch::ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const
{
    if (!IsReadyForEvaluation() || points.size() != Count() || (depthSource_ && !depthSource_->IsValidated()) ||
        !std::ranges::all_of(meshes_, [](const auto& mesh) { return mesh->IsValidated(); }))
    {
        return Fail(error, "Raster validation needs completed recording, exact pixels and accepted mesh sources.");
    }
    std::vector<std::uint8_t> coverage(points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        auto point = points[i];
        const bool covered = point.position[3] == 0;
        if (!covered)
        {
            if (point.position[3] != -1)
            {
                return Fail(error, "Invalid raster coverage marker at pixel " + std::to_string(i));
            }
            point.position[3] = 0;
        }
        const auto values = std::bit_cast<std::array<float, 20>>(point);
        if (!std::ranges::all_of(values, [&](float value) {
                return covered ? (std::isfinite(value) && std::abs(value) <= 1e6f) : value == 0;
            }))
        {
            return Fail(error, "Raster pixel contains nonfinite or unexpected background payload.");
        }
        if (covered)
        {
            const auto direction = [](const IblVector& value) {
                return std::hypot(value[0], value[1], value[2]) > 1e-10;
            };
            const auto& eye = View().eye;
            if (point.uvLod[3] < 0 || point.uvLod[3] > request_.texture.mipLevels - 1 || !direction(point.normal) ||
                !ValidSurfaceTangentPair(point) ||
                std::hypot(double(eye[0]) - point.position[0], double(eye[1]) - point.position[1],
                           double(eye[2]) - point.position[2]) <= 1e-10)
            {
                return Fail(error, "Raster pixel has an invalid interpolated frame or texture LOD.");
            }
            coverage[i] = 1;
        }
    }
    coverage_ = std::move(coverage);
    validated_ = true;
    error.clear();
    return true;
}

bool RasterSurfaceBatch::Declare(EnhancedRenderGraph& graph, std::string& error) const
{
    const auto owner = self_.lock();
    if (!owner || &graph.DeviceServices() != device_ || !IsCurrent() ||
        (depthSource_ && !depthSource_->GraphOutput(graph).IsValid()))
    {
        return Fail(error, "Raster declaration needs its current owner and shared depth producer in the same graph.");
    }
    // A prepared current-frame mesh must be produced in this graph. Importing
    // its uninitialized UAV as ShaderResource would silently skip skin/world work.
    for (const auto& mesh : meshes_)
    {
        if (!mesh->IsValidated() && (mesh->HasGraphDeclaration() || !mesh->IsReadyForEvaluation()) &&
            !mesh->GraphOutput(graph).IsValid())
        {
            return Fail(error, "Raster declaration is missing its current mesh transform producer.");
        }
    }
    if (declared_.exchange(true))
    {
        return Fail(error, "Raster declaration needs its prepared owner, current descriptors and a fresh graph.");
    }
    std::vector<EnhancedRenderGraph::RGPassUsage> captureUsage, resolveUsage;
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto write = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
    for (std::size_t i = 0; i < targets_.size(); ++i)
    {
        auto texture = graph.ImportTexture(targets_[i], RHIResourceState::Common, "LX.Raster.Frame");
        if (versioned)
        {
            texture = graph.Write(texture);
        }
        graphTargets_[i] = texture;
        captureUsage.push_back({texture, RHIResourceState::RenderTarget, write});
        resolveUsage.push_back({texture, RHIResourceState::ShaderResource, read});
    }
    auto depth = graph.FindImportedTexture(depth_);
    if (!depth.IsValid())
    {
        depth = graph.ImportTexture(depth_, RHIResourceState::Common, "LX.Raster.Depth");
    }
    if (depthSource_)
    {
        depth = depthSource_->graphDepth_;
    }
    else if (versioned)
    {
        depth = graph.Write(depth);
    }
    graphDepth_ = depth;
    captureUsage.push_back({depth, depthSource_ ? RHIResourceState::DepthRead : RHIResourceState::DepthWrite,
                           depthSource_ ? read : write});
    if (depthSource_)
    {
        captureUsage.push_back(
            {depthSource_->graphTargets_[1], RHIResourceState::ShaderResource, read});
    }
    for (const auto& mesh : meshes_)
    {
        auto vertex = mesh->GraphOutput(graph);
        if (!vertex.IsValid())
        {
            vertex = graph.FindImportedBuffer(mesh->Buffer());
        }
        if (!vertex.IsValid())
        {
            vertex = graph.ImportBuffer(mesh->Buffer(), RHIResourceState::ShaderResource, "LX.Raster.Vertices");
        }
        captureUsage.push_back({vertex, RHIResourceState::ShaderResource, read});
    }
    auto output = graph.ImportBuffer(buffer_, RHIResourceState::Common, "LX.Raster.Pixels");
    if (versioned)
    {
        output = graph.Write(output);
    }
    graph_ = &graph;
    graphEpoch_ = graph.ResourceEpoch();
    graphOutput_ = output;
    resolveUsage.push_back({output, RHIResourceState::UnorderedAccess, write});
    graph.AddPass("LX.OpaqueRasterCapture", captureUsage, [owner](const auto& context) {
        if (!owner->IsCurrent() || !context.graph || !context.encoder ||
            !owner->GraphOutput(*context.graph).IsValid() || (owner->recordedStages_.fetch_or(1) & 1) != 0)
        {
            owner->recordedStages_.fetch_or(8);
            throw std::runtime_error("Stale or repeated raster capture recording.");
        }
        auto& encoder = *context.encoder;
        const float clear[4]{};
        encoder.ClearRenderTargets(owner->renderTargets_, clear);
        if (!owner->depthSource_)
        {
            encoder.ClearDepthTarget(owner->renderTargets_, 1);
        }
        encoder.BindRenderTargets(owner->renderTargets_);
        encoder.SetViewportAndScissor(owner->request_.width, owner->request_.height);
        encoder.SetPipeline(RHIBindPoint::Graphics, owner->capturePipeline_);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, owner->constants_);
        if (owner->depthSource_)
        {
            encoder.SetBindings(RHIBindPoint::Graphics, 2, owner->opaqueOwners_);
        }
        for (std::size_t i = 0; i < owner->meshes_.size(); ++i)
        {
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, owner->drawConstants_[i]);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(owner->meshes_[i]->Buffer()));
            encoder.SetIndexBuffer(owner->indices_[i], RHIFormat::R32Uint);
            encoder.DrawIndexed(owner->meshes_[i]->Input()->Geometry().indexCount, 1);
        }
    });
    graph.AddPass("LX.ResolveVisibleSurface", resolveUsage, [owner](const auto& context) {
        if (!owner->IsCurrent() || !context.graph || !context.encoder ||
            !owner->GraphOutput(*context.graph).IsValid() || (owner->recordedStages_.fetch_or(2) & 2) != 0)
        {
            owner->recordedStages_.fetch_or(8);
            throw std::runtime_error("Stale or repeated raster resolve recording.");
        }
        auto& encoder = *context.encoder;
        encoder.SetPipeline(RHIBindPoint::Compute, owner->resolvePipeline_->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_);
        encoder.SetBindings(RHIBindPoint::Compute, 1, owner->inputs_);
        encoder.SetBindings(RHIBindPoint::Compute, 2, owner->output_);
        encoder.Dispatch((owner->Count() + 31) / 32, 1, 1);
    });
    // Preserve the capture/resolve chain and transition the external consumer input.
    graph.AddPass(
        "LX.VisibleSurfaceReady", {{output, RHIResourceState::ShaderResource, read}},
        [owner](const auto&) {
            if (!owner->IsCurrent())
            {
                owner->recordedStages_.fetch_or(8);
                throw std::runtime_error("Stale raster readiness recording.");
            }
            owner->recordedStages_.fetch_or(4);
        },
        true);
    error.clear();
    return true;
}

bool RasterSurfaceCollector::Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                                        IRenderPipelineCache& pipelines, const RHIShaderBlob& vertex,
                                        const RHIShaderBlob& pixel, const RHIShaderBlob& resolve,
                                        const RHIShaderBlob& sharedPixel, bool doubleSided, std::string& error)
{
    if ((device_ && device_ != &device) || !vertex.IsValid() || !pixel.IsValid() || !resolve.IsValid() ||
        !sharedPixel.IsValid())
    {
        return Fail(error, "Raster collector requires four compiled host artifacts on its owning device.");
    }
    const RHIPipelineLayoutParam graphicsHost[]{RHILayout::Cbv(0), RHILayout::Srv(6, RHIShaderVisibility::Vertex)};
    const RHIPipelineLayoutParam sharedHost[]{RHILayout::Cbv(0), RHILayout::Srv(6, RHIShaderVisibility::Vertex),
                                              RHILayout::SrvTable(1, 7, RHIShaderVisibility::Pixel)};
    const RHIPipelineLayoutParam computeHost[]{RHILayout::Cbv(0), RHILayout::SrvTable(6), RHILayout::UavBufferTable(1)};
    const auto graphicsLayout = roots.GetOrCreate({graphicsHost, {}, true}, error);
    const auto sharedLayout = roots.GetOrCreate({sharedHost, {}, true}, error);
    const auto computeLayout = roots.GetOrCreate({computeHost, {}, false}, error);
    if (!graphicsLayout.IsValid() || !sharedLayout.IsValid() || !computeLayout.IsValid())
    {
        return false;
    }
    RHIGraphicsPipelineDesc description;
    description.layout = graphicsLayout;
    description.vsBytecode = vertex.Data();
    description.vsSize = vertex.Size();
    description.psBytecode = pixel.Data();
    description.psSize = pixel.Size();
    description.numRenderTargets = 6;
    std::fill_n(description.rtvFormats, 6, RHIFormat::RGBA32Float);
    description.dsvFormat = RHIFormat::D32Float;
    description.depthEnable = true;
    description.depthWriteMask = RHIDepthWrite::All;
    description.depthFunc = RHICompareOp::Less;
    description.cullMode = doubleSided ? RHICullMode::None : RHICullMode::Back;
    const auto capture = pipelines.GetOrCreate(description, error);
    description.depthWriteMask = RHIDepthWrite::Zero;
    description.depthFunc = RHICompareOp::Equal;
    description.layout = sharedLayout;
    description.psBytecode = sharedPixel.Data();
    description.psSize = sharedPixel.Size();
    const auto sharedDepth = pipelines.GetOrCreate(description, error);
    RHIComputePipelineDesc compute;
    compute.layout = computeLayout;
    compute.csBytecode = resolve.Data();
    compute.csSize = resolve.Size();
    LX::Runtime::ComputePipeline resolved;
    if (!capture.IsValid() || !sharedDepth.IsValid() || !resolved.Create(pipelines, compute, error))
    {
        return false;
    }
    device_ = &device;
    capturePipeline_ = capture;
    sharedDepthPipeline_ = sharedDepth;
    resolvePipeline_ = resolved.GetGeneration();
    doubleSided_ = doubleSided;
    error.clear();
    return true;
}

bool RasterSurfaceCollector::Prepare(IRenderDeviceServices& device, const RasterSurfaceRequest& request,
                                     std::span<const std::shared_ptr<const MeshSurfaceBatch>> meshes,
                                     std::shared_ptr<const RasterSurfaceBatch>& result, std::string& error)
{
    return PrepareImpl(device, request, meshes, {}, result, error);
}

bool RasterSurfaceCollector::PrepareSharedDepth(IRenderDeviceServices& device, const RasterSurfaceRequest& request,
                                                std::span<const std::shared_ptr<const MeshSurfaceBatch>> meshes,
                                                std::shared_ptr<const RasterSurfaceBatch> depthSource,
                                                std::shared_ptr<const RasterSurfaceBatch>& result, std::string& error)
{
    if (!depthSource)
    {
        return Fail(error, "Shared raster capture requires an owning complete opaque depth source.");
    }
    return PrepareImpl(device, request, meshes, std::move(depthSource), result, error);
}

bool RasterSurfaceCollector::PrepareImpl(IRenderDeviceServices& device, const RasterSurfaceRequest& request,
                                         std::span<const std::shared_ptr<const MeshSurfaceBatch>> meshes,
                                         std::shared_ptr<const RasterSurfaceBatch> depthSource,
                                         std::shared_ptr<const RasterSurfaceBatch>& result, std::string& error)
{
    float ignored;
    auto footprint = request.texture;
    footprint.uvDx = footprint.uvDy = {};
    const auto recording = device.GetCurrentUploadRecordingId();
    const auto descriptors = device.GetDescriptorVersionToken();
    if (device_ != &device || !recording || !request.width || !request.height ||
        std::uint64_t(request.width) * request.height > MaxPixels || meshes.empty() || meshes.size() > MaxDraws ||
        !ResolveSurfaceLod(footprint, ignored, error) || !ValidCamera(request.viewProjection))
    {
        return Fail(error, "Raster preparation needs a bounded viewport, valid camera, footprint and mesh list.");
    }
    for (std::size_t i = 0; i < meshes.size(); ++i)
    {
        const auto& mesh = meshes[i];
        if (!mesh || mesh->Device() != &device || mesh->IsSampled() || !mesh->Buffer().IsValid() ||
            (!mesh->IsReadyForEvaluation() && !mesh->IsPreparedForGraph()) ||
            (mesh->RecordingId() != recording && !mesh->IsValidated()) || !SameView(mesh->View(), meshes[0]->View()))
        {
            return Fail(error, "Raster inputs require unsampled world frames sharing the exact current view.");
        }
        for (std::size_t j = 0; j < i; ++j)
        {
            if (meshes[j]->Buffer() == mesh->Buffer())
            {
                return Fail(error, "Duplicate raster vertex resource.");
            }
        }
    }
    if (depthSource &&
        (depthSource->Device() != &device || !depthSource->IsCurrent() || depthSource->depthSource_ ||
         depthSource->doubleSided_ != doubleSided_ || request.width != depthSource->Request().width ||
         request.height != depthSource->Request().height ||
         std::memcmp(&request.viewProjection, &depthSource->Request().viewProjection, sizeof(request.viewProjection)) !=
             0 ||
         !SameView(meshes[0]->View(), depthSource->View()) || !std::ranges::all_of(meshes, [&](const auto& mesh) {
             return std::ranges::find(depthSource->meshes_, mesh) != depthSource->meshes_.end();
         })))
    {
        return Fail(error,
                    "Shared depth requires the same camera/view, current recording and exact opaque mesh owners.");
    }
    auto candidate = std::shared_ptr<RasterSurfaceBatch>(new RasterSurfaceBatch);
    candidate->device_ = &device;
    candidate->request_ = request;
    candidate->meshes_.assign(meshes.begin(), meshes.end());
    candidate->depthSource_ = std::move(depthSource);
    candidate->recordingId_ = recording;
    candidate->descriptorVersion_ = descriptors;
    candidate->capturePipeline_ = candidate->depthSource_ ? sharedDepthPipeline_ : capturePipeline_;
    candidate->resolvePipeline_ = resolvePipeline_;
    candidate->doubleSided_ = doubleSided_;
    RHITextureDesc texture;
    texture.width = request.width;
    texture.height = request.height;
    texture.format = RHIFormat::RGBA32Float;
    texture.allowRenderTarget = true;
    texture.debugName = L"LX.Raster.WorldFrame";
    for (auto& target : candidate->targets_)
    {
        if (!device.CreateTexture(texture, target, error))
        {
            return false;
        }
    }
    texture.format = RHIFormat::D32Float;
    texture.allowRenderTarget = false;
    texture.allowDepthStencil = true;
    texture.debugName = L"LX.Raster.Depth";
    RHIBufferDesc buffer;
    buffer.bytes = candidate->Count() * sizeof(SurfacePoint);
    buffer.allowUnorderedAccess = true;
    buffer.debugName = L"LX.Raster.VisiblePixels";
    if (candidate->depthSource_)
    {
        candidate->depth_ = candidate->depthSource_->Depth();
    }
    else if (!device.CreateTexture(texture, candidate->depth_, error))
    {
        return false;
    }
    if (!device.CreateBuffer(buffer, candidate->buffer_, error))
    {
        return false;
    }
    const auto depth = candidate->depthSource_
                           ? RHIDepthTargetDesc::DepthReadOnly(candidate->depth_, RHIFormat::D32Float)
                           : RHIDepthTargetDesc::Depth(candidate->depth_, RHIFormat::D32Float);
    candidate->renderTargets_ = device.CreateRenderTargets(candidate->targets_, &depth);
    std::array<RHIBindingDesc, 6> inputs;
    for (std::size_t i = 0; i < inputs.size(); ++i)
    {
        inputs[i] = RHIBindingDesc::Srv2D(candidate->targets_[i], RHIFormat::RGBA32Float);
    }
    candidate->inputs_ = device.CreateBindings(inputs);
    const auto output = RHIBindingDesc::UavBuffer(candidate->buffer_, candidate->Count(), sizeof(SurfacePoint));
    candidate->output_ = device.CreateBindings({&output, 1});
    struct Constants
    {
        math::matrix4x4 viewProjection;
        std::array<std::uint32_t, 4> viewportTexture;
        IblVector lod;
    };
    const Constants constants{request.viewProjection,
                              {request.width, request.height, footprint.width, footprint.height},
                              {float(footprint.mipLevels - 1), footprint.bias, 0, 0}};
    candidate->constants_ = device.UploadConstants(&constants, sizeof(constants));
    if (candidate->depthSource_)
    {
        const auto owners = RHIBindingDesc::Srv2D(candidate->depthSource_->targets_[1], RHIFormat::RGBA32Float);
        candidate->opaqueOwners_ = device.CreateBindings({&owners, 1});
    }
    for (const auto& mesh : meshes)
    {
        auto drawConstants = constants;
        const auto index = candidate->depthSource_ ? std::ranges::find(candidate->depthSource_->meshes_, mesh) -
                                                         candidate->depthSource_->meshes_.begin()
                                                   : candidate->drawConstants_.size();
        // Exact draw identity resolves coplanar ties according to prepass order.
        // Equal depth alone would let two different materials claim one pixel.
        drawConstants.lod[2] = static_cast<float>(index + 1);
        candidate->drawConstants_.push_back(device.UploadConstants(&drawConstants, sizeof(drawConstants)));
        const auto& geometry = mesh->Input()->Geometry();
        const auto bytes = std::size_t(geometry.indexCount) * sizeof(std::uint32_t);
        auto indices = device.AllocateUpload({bytes, RHIUploadUsage::IndexData, 4});
        if (!indices.IsWritable())
        {
            return Fail(error, "Raster index allocation failed.");
        }
        std::memcpy(indices.cpuAddress, geometry.indexData, bytes);
        candidate->indices_.push_back(indices);
    }
    if (!candidate->renderTargets_.IsValid() || !candidate->inputs_.IsValid() || !candidate->output_.IsValid() ||
        !candidate->constants_.IsValid() || !candidate->IsCurrent() ||
        (candidate->depthSource_ && !candidate->opaqueOwners_.IsValid()) ||
        !std::ranges::all_of(candidate->drawConstants_, [](const auto& uniform) { return uniform.IsValid(); }))
    {
        return Fail(error,
                    "Raster preparation failed or recording/descriptor prefix changed; previous batch retained.");
    }
    candidate->self_ = candidate;
    result = std::move(candidate);
    error.clear();
    return true;
}
} // namespace material_graph
