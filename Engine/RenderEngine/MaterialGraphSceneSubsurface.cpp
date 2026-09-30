#include "MaterialGraphSceneSubsurface.h"

#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <stdexcept>

namespace material_graph
{
namespace
{
constexpr std::uint32_t DispatchPixels = 4096;
constexpr std::uint64_t BytesPerPixel = 7 * 16 + sizeof(SceneSubsurfaceReflection) + 16;

struct SubsurfaceConstants
{
    std::uint32_t width, height, first, dispatchCount;
    std::uint32_t environment, radius, reserved0, reserved1;
};

bool Fail(std::string& error, const char* message)
{
    error = message;
    return false;
}
} // namespace

SceneSubsurfaceFrame::~SceneSubsurfaceFrame()
{
    if (!device_)
    {
        return;
    }
    for (const auto input : inputs_)
    {
        if (input.IsValid())
        {
            device_->ReleaseTexture(input);
        }
    }
    if (reflection_.IsValid())
    {
        device_->ReleaseBuffer(reflection_);
    }
    if (irradiance_.IsValid())
    {
        device_->ReleaseBuffer(irradiance_);
    }
}

std::span<const RHITextureHandle> SceneSubsurfaceFrame::Inputs() const
{
    return inputs_;
}

RHIBufferHandle SceneSubsurfaceFrame::Reflection() const
{
    return reflection_;
}

RHIBufferHandle SceneSubsurfaceFrame::Irradiance() const
{
    return irradiance_;
}

bool SceneSubsurfaceResources::Initialize(const EnhancedFrameContext& context, std::string& error)
{
    if (device_)
    {
        return device_ == context.resources || Fail(error, "Scene SSS resources belong to another device.");
    }
    const auto backend = RHIShaderCompiler::GetOutput();
    const auto other = backend == RHIShaderBinary::Dxil ? RHIShaderBinary::SpirV : RHIShaderBinary::Dxil;
    const auto bakeFile = RHIShaderSource::Resolve("MaterialGraphSceneSubsurfaceBake.slang").string();
    const auto filterFile = RHIShaderSource::Resolve("MaterialGraphSceneSubsurfaceFilter.slang").string();
    RHIShaderCompileOptions options;
    options.strictMath = true;
    RHIShaderCompiler::VerifiedShader bake, filter, verification;
    if (!RHIShaderCompiler::VerifyFile(bakeFile, "LXSceneSubsurfaceBake", "cs_6_0", backend, {}, bake, error,
                                       options) ||
        !RHIShaderCompiler::VerifyFile(filterFile, "LXSceneSubsurfaceFilter", "cs_6_0", backend, {}, filter, error,
                                       options) ||
        !RHIShaderCompiler::VerifyFile(bakeFile, "LXSceneSubsurfaceBake", "cs_6_0", other, {}, verification, error,
                                       options) ||
        !RHIShaderCompiler::VerifyFile(filterFile, "LXSceneSubsurfaceFilter", "cs_6_0", other, {}, verification, error,
                                       options))
    {
        return false;
    }
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::SrvTable(13, 0),
                                              RHILayout::UavBufferTable(2, 0)};
    const RHIStaticSamplerDesc sampler{RHISampler::Point(RHIAddressMode::Clamp), 0};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, {&sampler, 1}}, error);
    if (!layout.IsValid())
    {
        return false;
    }
    RHIComputePipelineDesc desc;
    desc.layout = layout;
    desc.csBytecode = bake.bytecode.Data();
    desc.csSize = bake.bytecode.Size();
    const auto bakePipeline = context.psoManager->GetOrCreateCompute(desc, error);
    desc.csBytecode = filter.bytecode.Data();
    desc.csSize = filter.bytecode.Size();
    const auto filterPipeline = context.psoManager->GetOrCreateCompute(desc, error);
    if (!bakePipeline.IsValid() || !filterPipeline.IsValid())
    {
        return false;
    }
    device_ = context.resources;
    bake_ = bakePipeline;
    filter_ = filterPipeline;
    return true;
}

bool SceneSubsurfaceResources::Prepare(const EnhancedFrameContext& context, RHITextureHandle environment,
                                       std::uint64_t memoryBudget, std::shared_ptr<const SceneSubsurfaceFrame>& result,
                                       std::string& error)
{
    const auto count = std::uint64_t(context.width) * context.height;
    if (!context.resources || !context.rootSignatures || !context.psoManager || !count || count > UINT32_MAX ||
        count * BytesPerPixel > memoryBudget || !context.resources->GetCurrentUploadRecordingId())
    {
        return Fail(error, "Scene SSS needs current device services and sufficient per-frame GPU memory budget.");
    }
    if (!Initialize(context, error))
    {
        return false;
    }
    auto candidate = std::make_shared<SceneSubsurfaceFrame>();
    candidate->device_ = device_;
    candidate->width_ = context.width;
    candidate->height_ = context.height;
    candidate->recording_ = device_->GetCurrentUploadRecordingId();
    candidate->descriptors_ = device_->GetDescriptorVersionToken();
    candidate->environment_ = environment;
    candidate->bake_ = bake_;
    candidate->filter_ = filter_;
    RHITextureDesc texture;
    texture.width = context.width;
    texture.height = context.height;
    texture.format = RHIFormat::RGBA32Float;
    texture.allowRenderTarget = true;
    texture.debugName = L"LX.Scene.SubsurfaceInput";
    for (auto& input : candidate->inputs_)
    {
        if (!device_->CreateTexture(texture, input, error))
        {
            return false;
        }
    }
    RHIBufferDesc buffer;
    buffer.bytes = count * sizeof(SceneSubsurfaceReflection);
    buffer.allowUnorderedAccess = true;
    buffer.debugName = L"LX.Scene.SubsurfaceReflection";
    if (!device_->CreateBuffer(buffer, candidate->reflection_, error))
    {
        return false;
    }
    buffer.bytes = count * 16;
    buffer.debugName = L"LX.Scene.SubsurfaceIrradiance";
    if (!device_->CreateBuffer(buffer, candidate->irradiance_, error))
    {
        return false;
    }
    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(candidate->reflection_, static_cast<std::uint32_t>(count),
                                  sizeof(SceneSubsurfaceReflection)),
        RHIBindingDesc::UavBuffer(candidate->irradiance_, static_cast<std::uint32_t>(count), 16)};
    candidate->outputs_ = device_->CreateBindings(outputs);
    if (!candidate->outputs_.IsValid())
    {
        return Fail(error, "Scene SSS output binding failed.");
    }
    for (std::uint32_t first = 0; first < count; first += DispatchPixels)
    {
        const SubsurfaceConstants constants{
            context.width,
            context.height,
            first,
            static_cast<std::uint32_t>((std::min)(count - first, std::uint64_t{DispatchPixels})),
            environment.IsValid(),
            4,
            0,
            0};
        const auto uploaded = device_->UploadConstants(&constants, sizeof(constants));
        if (!uploaded.IsValid())
        {
            return Fail(error, "Scene SSS constants allocation failed.");
        }
        candidate->constants_.push_back(uploaded);
    }
    if (candidate->recording_ != device_->GetCurrentUploadRecordingId() ||
        candidate->descriptors_ != device_->GetDescriptorVersionToken())
    {
        return Fail(error, "Scene SSS preparation changed upload/descriptor ownership.");
    }
    candidate->self_ = candidate;
    result = std::move(candidate);
    error.clear();
    return true;
}

void SceneSubsurfaceFrame::CheckCurrent(const EnhancedRenderGraph& graph) const
{
    if (graph_ != &graph || graphEpoch_ != graph.ResourceEpoch() ||
        recording_ != device_->GetCurrentUploadRecordingId() || descriptors_ != device_->GetDescriptorVersionToken())
    {
        throw std::runtime_error("Scene SSS frame has stale graph/upload/descriptor ownership.");
    }
}

const std::array<RGHandle, 7>& SceneSubsurfaceFrame::DeclareInputs(EnhancedRenderGraph& graph) const
{
    if (graph_)
    {
        throw std::runtime_error("Scene SSS inputs require one declaration.");
    }
    graph_ = &graph;
    graphEpoch_ = graph.ResourceEpoch();
    CheckCurrent(graph);
    for (unsigned i = 0; i < inputs_.size(); ++i)
    {
        graphInputs_[i] = graph.ImportTexture(inputs_[i], RHIResourceState::Common, "LX.Scene.SubsurfaceInput");
    }
    graphReflection_ = graph.ImportBuffer(reflection_, RHIResourceState::Common, "LX.Scene.SubsurfaceReflection");
    graphIrradiance_ = graph.ImportBuffer(irradiance_, RHIResourceState::Common, "LX.Scene.SubsurfaceIrradiance");
    return graphInputs_;
}

void SceneSubsurfaceFrame::DeclareReflection(EnhancedRenderGraph& graph, const SceneLookupFrame& lookup,
                                             RGHandle owners) const
{
    CheckCurrent(graph);
    const auto owner = self_.lock();
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{owners, RHIResourceState::ShaderResource},
                                                       {graphReflection_, RHIResourceState::UnorderedAccess}};
    std::array<RHIBindingDesc, 13> descriptions;
    for (unsigned i = 0; i < 11; ++i)
    {
        const auto input = lookup.Inputs()[i];
        uses.push_back({graph.FindImportedTexture(input), RHIResourceState::ShaderResource});
        descriptions[i] = RHIBindingDesc::Srv2D(input, RHIFormat::RGBA32Float);
    }
    descriptions[11] = RHIBindingDesc::SrvCube(environment_,
                                               environment_.IsValid() ? device_->DescribeTexture(environment_).format
                                                                      : RHIFormat::RGBA16Float,
                                               1)
                           .OrNull();
    if (environment_.IsValid())
    {
        uses.push_back({graph.FindImportedTexture(environment_), RHIResourceState::ShaderResource});
    }
    graph.AddPass("LX.Scene.SubsurfaceReflection", uses, [owner, descriptions, owners](const auto& execution) mutable {
        owner->CheckCurrent(*execution.graph);
        descriptions[12] = RHIBindingDesc::Srv2D(execution.ResolveHandle(owners), RHIFormat::R32Uint);
        const auto table = owner->device_->CreateBindings(descriptions);
        if (!table.IsValid())
        {
            throw std::runtime_error("Scene SSS reflection input binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.SetPipeline(RHIBindPoint::Compute, owner->bake_);
        encoder.SetBindings(RHIBindPoint::Compute, 1, table);
        encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
        for (unsigned i = 0; i < owner->constants_.size(); ++i)
        {
            const auto count = (std::min)(std::uint64_t(owner->width_) * owner->height_ - i * DispatchPixels,
                                          std::uint64_t{DispatchPixels});
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_[i]);
            encoder.Dispatch(static_cast<std::uint32_t>((count + 31) / 32), 1, 1);
        }
    });
}

void SceneSubsurfaceFrame::DeclareFilter(EnhancedRenderGraph& graph, RGHandle owners) const
{
    CheckCurrent(graph);
    const auto owner = self_.lock();
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{owners, RHIResourceState::ShaderResource},
                                                       {graphIrradiance_, RHIResourceState::UnorderedAccess}};
    for (const auto input : graphInputs_)
    {
        uses.push_back({input, RHIResourceState::ShaderResource});
    }
    graph.AddPass("LX.Scene.SubsurfaceFilter", uses, [owner, owners](const auto& execution) {
        owner->CheckCurrent(*execution.graph);
        std::array<RHIBindingDesc, 13> descriptions;
        for (unsigned i = 0; i < 7; ++i)
        {
            descriptions[i] = RHIBindingDesc::Srv2D(owner->inputs_[i], RHIFormat::RGBA32Float);
        }
        descriptions[7] = RHIBindingDesc::Srv2D(execution.ResolveHandle(owners), RHIFormat::R32Uint);
        for (unsigned i = 8; i < descriptions.size(); ++i)
        {
            descriptions[i] = RHIBindingDesc::Srv2D({}, RHIFormat::RGBA32Float).OrNull();
        }
        const auto table = owner->device_->CreateBindings(descriptions);
        if (!table.IsValid())
        {
            throw std::runtime_error("Scene SSS filter input binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.SetPipeline(RHIBindPoint::Compute, owner->filter_);
        encoder.SetBindings(RHIBindPoint::Compute, 1, table);
        encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
        for (unsigned i = 0; i < owner->constants_.size(); ++i)
        {
            const auto count = (std::min)(std::uint64_t(owner->width_) * owner->height_ - i * DispatchPixels,
                                          std::uint64_t{DispatchPixels});
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_[i]);
            encoder.Dispatch(static_cast<std::uint32_t>((count + 31) / 32), 1, 1);
        }
    });
}

RGHandle SceneSubsurfaceFrame::GraphReflection(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return graphReflection_;
}

RGHandle SceneSubsurfaceFrame::GraphIrradiance(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return graphIrradiance_;
}

void SceneSubsurfaceResources::ShutdownAfterIdle()
{
    device_ = nullptr;
    bake_ = {};
    filter_ = {};
}
} // namespace material_graph
