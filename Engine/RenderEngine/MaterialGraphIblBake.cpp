#include "MaterialGraphIblBake.h"
#include "MaterialGraphSurfaceBatch.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace material_graph
{
bool IsValidIblBakePoint(const IblBakePoint& point)
{
    const auto values = std::bit_cast<std::array<float, 44>>(point);
    if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value) && std::abs(value) <= 1e6f; }))
    {
        return false;
    }
    const auto positiveDirection = [](const IblVector& value) {
        const double length = double(value[0]) * value[0] + double(value[1]) * value[1] + double(value[2]) * value[2];
        return length > 1e-20 && length < 1e20;
    };
    if (!positiveDirection(point.viewTier) || (point.viewTier[3] != 0 && point.viewTier[3] != 1))
    {
        return false;
    }
    const auto& normal = point.normalRoughness;
    const auto& view = point.viewTier;
    const double normalLength = std::hypot(normal[0], normal[1], normal[2]);
    const double viewLength = std::hypot(view[0], view[1], view[2]);
    const double cosine =
        normalLength <= 1e-10
            ? view[2] / viewLength
            : (double(normal[0]) * view[0] + double(normal[1]) * view[1] + double(normal[2]) * view[2]) /
                  (normalLength * viewLength);
    if (cosine < 1e-4)
    {
        return false;
    }
    const bool layered = point.viewTier[3] == 1;
    return layered || (point.tintAnisotropy[3] == 0 && point.coatWeightRoughIorFilmThickness[0] == 0 &&
                       point.coatWeightRoughIorFilmThickness[3] == 0 && point.coatNormalSheenWeight[3] == 0);
}

IblBakeResult::~IblBakeResult()
{
    if (device_ && buffer_.IsValid())
    {
        device_->ReleaseBuffer(buffer_);
    }
}

bool IblBakeResult::Matches(const IRenderDeviceServices& device, const IblEnvironment& environment,
                            std::span<const IblBakePoint> points) const
{
    const auto& a = environment_.cube;
    const auto& b = environment.cube;
    return device_ == &device && environment_.generation == environment.generation &&
           environment_.owner == environment.owner && a.handle == b.handle && a.format == b.format &&
           a.width == b.width && a.height == b.height && a.mipLevels == b.mipLevels && a.arraySize == b.arraySize &&
           a.isCube == b.isCube && !gpuPoints_ && points_.size() == points.size() &&
           std::memcmp(points_.data(), points.data(), points.size_bytes()) == 0;
}

bool IblBakeResult::MatchesGpu(const IRenderDeviceServices& device, const IblEnvironment& environment,
                               const SurfaceBatch& points) const
{
    const auto& a = environment_.cube;
    const auto& b = environment.cube;
    return device_ == &device && gpuPoints_.get() == &points && environment_.generation == environment.generation &&
           environment_.owner == environment.owner && a.handle == b.handle && a.format == b.format &&
           a.width == b.width && a.height == b.height && a.mipLevels == b.mipLevels && a.arraySize == b.arraySize &&
           a.isCube == b.isCube;
}

bool IblBaker::Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                          IRenderPipelineCache& pipelines, const RHIShaderBlob& shader, std::string& error)
{
    if (!shader.IsValid() || (device_ && device_ != &device))
    {
        error = "IBL bake needs compiled CS bytecode and the owning device.";
        return false;
    }
    const RHIPipelineLayoutParam parameters[] = {RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::SrvTable(1, 1),
                                                 RHILayout::UavBufferTable(1, 0)};
    const RHIStaticSamplerDesc sampler{RHISampler::Point(RHIAddressMode::Clamp), 0};
    const auto layout = roots.GetOrCreate({parameters, {&sampler, 1}}, error);
    if (!layout.IsValid())
    {
        return false;
    }
    RHIComputePipelineDesc description;
    description.layout = layout;
    description.csBytecode = shader.Data();
    description.csSize = shader.Size();
    const auto pipeline = pipelines.GetOrCreateCompute(description, error);
    if (!pipeline.IsValid())
    {
        return false;
    }
    device_ = &device;
    pipeline_ = pipeline;
    error.clear();
    return true;
}

bool IblBaker::Record(IRenderDeviceServices& device, const IblEnvironment& environment,
                      std::span<const IblBakePoint> points, std::shared_ptr<const IblBakeResult>& result,
                      std::string& error)
{
    return RecordInputs(device, environment, points, {}, result, error);
}

bool IblBaker::RecordGpu(IRenderDeviceServices& device, const IblEnvironment& environment,
                         std::shared_ptr<const SurfaceBatch> points, std::shared_ptr<const IblBakeResult>& result,
                         std::string& error)
{
    if (!points)
    {
        error = "GPU IBL bake needs an owning evaluated surface batch.";
        return false;
    }
    return RecordInputs(device, environment, {}, std::move(points), result, error);
}

bool IblBaker::RecordInputs(IRenderDeviceServices& device, const IblEnvironment& environment,
                            std::span<const IblBakePoint> cpuPoints, std::shared_ptr<const SurfaceBatch> gpuPoints,
                            std::shared_ptr<const IblBakeResult>& result, std::string& error)
{
    if (gpuPoints && !gpuPoints->IsReadyForBake())
    {
        error = "GPU IBL recording needs a recorded surface producer.";
        return false;
    }
    std::shared_ptr<const IblBakeResult> candidate;
    if (!PrepareInputs(device, environment, cpuPoints, std::move(gpuPoints), candidate, error))
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

bool IblBaker::PrepareGpu(IRenderDeviceServices& device, const IblEnvironment& environment,
                          std::shared_ptr<const SurfaceBatch> points, std::shared_ptr<const IblBakeResult>& result,
                          std::string& error)
{
    if (!points || (!points->IsPreparedForGraph() && !points->IsValidated()))
    {
        error = "GPU IBL preparation needs a current prepared surface or a completed accepted surface.";
        return false;
    }
    return PrepareInputs(device, environment, {}, std::move(points), result, error);
}

bool IblBaker::PrepareInputs(IRenderDeviceServices& device, const IblEnvironment& environment,
                             std::span<const IblBakePoint> cpuPoints, std::shared_ptr<const SurfaceBatch> gpuPoints,
                             std::shared_ptr<const IblBakeResult>& result, std::string& error)
{
    const auto recordingId = device.GetCurrentUploadRecordingId();
    const auto descriptorVersion = device.GetDescriptorVersionToken();
    const auto& cube = environment.cube;
    const auto count = gpuPoints ? gpuPoints->Count() : cpuPoints.size();
    const bool validPoints = gpuPoints ? gpuPoints->Device() == &device && gpuPoints->Buffer().IsValid() &&
                                             (gpuPoints->RecordingId() == recordingId || gpuPoints->IsValidated())
                                       : std::ranges::all_of(cpuPoints, IsValidIblBakePoint);
    if (device_ != &device || !pipeline_.IsValid() || recordingId == 0 || count == 0 || count > MaxPoints ||
        !validPoints || !environment.owner || environment.generation == 0 || !cube.IsValid() || !cube.isCube ||
        cube.arraySize != 6 || cube.width == 0 || cube.width != cube.height || cube.mipLevels == 0 ||
        (cube.format != RHIFormat::RGBA16Float && cube.format != RHIFormat::RGBA32Float))
    {
        error = "IBL bake needs finite Core/Layered points and an owning linear HDR cube generation in a recording.";
        return false;
    }
    auto candidate = std::shared_ptr<IblBakeResult>(new IblBakeResult);
    candidate->device_ = &device;
    candidate->environment_ = environment;
    candidate->points_.assign(cpuPoints.begin(), cpuPoints.end());
    candidate->gpuPoints_ = std::move(gpuPoints);
    candidate->count_ = static_cast<std::uint32_t>(count);
    candidate->recordingId_ = recordingId;
    candidate->descriptorVersion_ = descriptorVersion;
    candidate->pipeline_ = pipeline_;
    RHIBufferDesc description;
    description.bytes = count * sizeof(IblBakeSample);
    description.allowUnorderedAccess = true;
    description.debugName = L"LX.Material.IblBake";
    if (!device.CreateBuffer(description, candidate->buffer_, error))
    {
        return false;
    }
    candidate->inputs_ = candidate->gpuPoints_
                             ? RHIBufferSlice::Whole(candidate->gpuPoints_->Buffer())
                             : device.AllocateUpload({cpuPoints.size_bytes(), RHIUploadUsage::Raw, 16});
    const std::array<std::uint32_t, 4> constants{candidate->Count(), 0, 0, 0};
    candidate->uniform_ = device.UploadConstants(constants.data(), sizeof(constants));
    const auto source = RHIBindingDesc::SrvCube(cube.handle, cube.format, 1);
    const auto target = RHIBindingDesc::UavBuffer(candidate->buffer_, candidate->Count(), sizeof(IblBakeSample));
    candidate->sourceTable_ = device.CreateBindings({&source, 1});
    candidate->targetTable_ = device.CreateBindings({&target, 1});
    if (!candidate->inputs_.IsValid() || (!candidate->gpuPoints_ && !candidate->inputs_.IsWritable()) ||
        !candidate->uniform_.IsValid() || !candidate->sourceTable_.IsValid() || !candidate->targetTable_.IsValid() ||
        !candidate->IsCurrent())
    {
        error = "IBL bake allocation failed or changed recording/descriptors; retry with the accepted result retained.";
        return false;
    }
    if (!candidate->gpuPoints_)
    {
        std::memcpy(candidate->inputs_.cpuAddress, cpuPoints.data(), cpuPoints.size_bytes());
    }
    candidate->self_ = candidate;
    result = std::move(candidate);
    error.clear();
    return true;
}

bool IblBakeResult::IsCurrent() const
{
    return recordingId_ != 0 && device_->GetCurrentUploadRecordingId() == recordingId_ &&
           device_->GetDescriptorVersionToken() == descriptorVersion_;
}

bool IblBakeResult::IsReady() const
{
    return recordedStages_.load() == 3 && (!gpuPoints_ || gpuPoints_->IsReadyForBake());
}

RGHandle IblBakeResult::GraphOutput(const EnhancedRenderGraph& graph) const
{
    return graph_ == &graph && graphEpoch_ == graph.ResourceEpoch() &&
                   graph.ResolveBufferHandle(graphOutput_) == buffer_
               ? graphOutput_
               : RGHandle{};
}

bool IblBakeResult::RecordCommands(RHIEncoder& encoder, std::string& error) const
{
    if (!IsCurrent() || (recordedStages_.fetch_or(1) & 1) != 0)
    {
        recordedStages_.fetch_or(4);
        error = "IBL recording is stale or has already been recorded.";
        return false;
    }
    encoder.SetPipeline(RHIBindPoint::Compute, pipeline_);
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform_);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, inputs_);
    encoder.SetBindings(RHIBindPoint::Compute, 2, sourceTable_);
    encoder.SetBindings(RHIBindPoint::Compute, 3, targetTable_);
    encoder.Dispatch((Count() + 31) / 32, 1, 1);
    error.clear();
    return true;
}

bool IblBakeResult::Declare(EnhancedRenderGraph& graph, std::string& error) const
{
    const auto owner = self_.lock();
    if (!owner || &graph.DeviceServices() != device_ || !IsCurrent() || !gpuPoints_ || recordedStages_.load() != 0 ||
        declared_.load())
    {
        error = "IBL declaration needs an unrecorded GPU source and its current prepared owner.";
        return false;
    }
    auto input = gpuPoints_->GraphOutput(graph);
    if (!input.IsValid() && !gpuPoints_->IsValidated())
    {
        error = "IBL surface producer must be declared in this graph before its consumer.";
        return false;
    }
    if (!input.IsValid())
    {
        input = graph.FindImportedBuffer(gpuPoints_->Buffer());
        if (!input.IsValid())
        {
            input = graph.ImportBuffer(gpuPoints_->Buffer(), RHIResourceState::ShaderResource, "LX.IBL.Surface");
        }
    }
    if (declared_.exchange(true))
    {
        error = "IBL packet already belongs to a graph.";
        return false;
    }
    auto environment = graph.FindImportedTexture(environment_.cube.handle);
    if (!environment.IsValid())
    {
        environment = graph.ImportTexture(environment_.cube.handle, RHIResourceState::ShaderResource, "LX.IBL.Cube");
    }
    graph_ = &graph;
    graphEpoch_ = graph.ResourceEpoch();
    graphOutput_ = graph.ImportBuffer(buffer_, RHIResourceState::Common, "LX.IBL.Result");
    graph.AddPass("LX.BakePhysicalIBL",
                  {{input, RHIResourceState::ShaderResource},
                   {environment, RHIResourceState::ShaderResource},
                   {graphOutput_, RHIResourceState::UnorderedAccess}},
                  [owner](const auto& context) {
                      std::string error;
                      if (!context.graph || !context.encoder || !owner->GraphOutput(*context.graph).IsValid() ||
                          !owner->RecordCommands(*context.encoder, error))
                      {
                          owner->recordedStages_.fetch_or(4);
                          throw std::runtime_error(error.empty() ? "Invalid IBL graph recording owner." : error);
                      }
                  });
    graph.AddPass(
        "LX.PhysicalIBLReady", {{graphOutput_, RHIResourceState::ShaderResource}},
        [owner](const auto&) {
            if (!owner->IsCurrent())
            {
                owner->recordedStages_.fetch_or(4);
                throw std::runtime_error("Stale IBL readiness recording.");
            }
            owner->recordedStages_.fetch_or(2);
        },
        true);
    error.clear();
    return true;
}
} // namespace material_graph
