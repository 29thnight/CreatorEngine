#include "MaterialGraphSceneRefraction.h"

#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace material_graph
{
namespace
{
constexpr std::uint32_t DispatchPixels = 4096;
// Includes the graph-owned HDR and depth snapshots.
constexpr std::uint64_t BytesPerPixel = 2 * 16 + sizeof(SceneRefractionSample) + 8 + 4;

struct RefractionConstants
{
    math::matrix4x4 viewProjection, inverseViewProjection;
    std::uint32_t width, height, first, dispatchCount;
    std::uint32_t environment, steps, rays, reserved;
};
static_assert(sizeof(RefractionConstants) == 160);

bool Fail(std::string& error, const char* message)
{
    error = message;
    return false;
}
} // namespace

SceneRefractionFrame::~SceneRefractionFrame()
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
    if (samples_.IsValid())
    {
        device_->ReleaseBuffer(samples_);
    }
}

bool SceneRefractionResources::Initialize(const EnhancedFrameContext& context, bool volume, std::string& error)
{
    if (device_)
    {
        if (device_ != context.resources)
        {
            return Fail(error, "Scene refraction belongs to another device.");
        }
        if ((volume ? volumeBake_ : bake_).IsValid())
        {
            return true;
        }
    }
    const auto backend = RHIShaderCompiler::GetOutput();
    const auto other = backend == RHIShaderBinary::Dxil ? RHIShaderBinary::SpirV : RHIShaderBinary::Dxil;
    const auto file = RHIShaderSource::Resolve("MaterialGraphSceneRefractionBake.slang").string();
    RHIShaderCompileOptions options;
    options.strictMath = true;
    RHIShaderCompiler::VerifiedShader bake, verification;
    RHIShaderPermutation permutation;
    if (volume && !permutation.Enable("LX_SCENE_REFRACTION_VOLUME", error))
    {
        return false;
    }
    if (!RHIShaderCompiler::VerifyFile(file, "LXSceneRefractionBake", "cs_6_0", backend, permutation, bake, error,
                                       options) ||
        !RHIShaderCompiler::VerifyFile(file, "LXSceneRefractionBake", "cs_6_0", other, permutation, verification, error,
                                       options))
    {
        return false;
    }
    const RHIPipelineLayoutParam parameters[]{
        RHILayout::Cbv(0),  RHILayout::SrvTable(17, 0), RHILayout::UavBufferTable(1, 0), RHILayout::Cbv(1),
        RHILayout::Srv(17), RHILayout::Srv(18),         RHILayout::SrvTable(2, 19)};
    const RHIStaticSamplerDesc samplers[]{
        {RHISampler::Point(RHIAddressMode::Clamp), 0},
        {RHISampler::Linear(RHIAddressMode::Clamp), 2},
        {RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border, RHIBorderColor::OpaqueWhite), 3}};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, samplers}, error);
    if (!layout.IsValid())
    {
        return false;
    }
    RHIComputePipelineDesc desc;
    desc.layout = layout;
    desc.csBytecode = bake.bytecode.Data();
    desc.csSize = bake.bytecode.Size();
    const auto pipeline = context.psoManager->GetOrCreateCompute(desc, error);
    if (!pipeline.IsValid())
    {
        return false;
    }
    device_ = context.resources;
    (volume ? volumeBake_ : bake_) = pipeline;
    return true;
}

bool SceneRefractionResources::Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                                       RHITextureHandle environment, std::uint64_t memoryBudget,
                                       std::shared_ptr<const SceneRefractionFrame>& result, std::string& error,
                                       std::shared_ptr<const SceneVolumeFrame> volume)
{
    const auto count = std::uint64_t(context.width) * context.height;
    if (!context.resources || !context.rootSignatures || !context.psoManager || !count || count > UINT32_MAX ||
        count * BytesPerPixel > memoryBudget || !context.resources->GetCurrentUploadRecordingId())
    {
        return Fail(error, "Scene refraction needs current device services and sufficient GPU memory budget.");
    }
    if (!Initialize(context, bool(volume), error))
    {
        return false;
    }
    auto candidate = std::make_shared<SceneRefractionFrame>();
    candidate->device_ = device_;
    candidate->width_ = context.width;
    candidate->height_ = context.height;
    candidate->recording_ = device_->GetCurrentUploadRecordingId();
    candidate->descriptors_ = device_->GetDescriptorVersionToken();
    candidate->environment_ = environment;
    candidate->bake_ = volume ? volumeBake_ : bake_;
    candidate->volume_ = std::move(volume);
    if (!candidate->volume_)
    {
        const std::array<std::byte, SceneVolumeConstantsBytes> empty{};
        candidate->emptyVolumeConstants_ = device_->UploadConstants(empty.data(), empty.size());
        candidate->emptyVolumeBuffer_ = device_->AllocateUpload({48, RHIUploadUsage::Raw, 16});
        if (!candidate->emptyVolumeConstants_.IsValid() || !candidate->emptyVolumeBuffer_.IsWritable())
        {
            return Fail(error, "Scene refraction empty medium allocation failed.");
        }
        std::memset(candidate->emptyVolumeBuffer_.cpuAddress, 0, 48);
    }
    RHITextureDesc texture;
    texture.width = context.width;
    texture.height = context.height;
    texture.format = RHIFormat::RGBA32Float;
    texture.allowRenderTarget = true;
    texture.debugName = L"LX.Scene.RefractionInput";
    for (auto& input : candidate->inputs_)
    {
        if (!device_->CreateTexture(texture, input, error))
        {
            return false;
        }
    }
    RHIBufferDesc buffer;
    buffer.bytes = count * sizeof(SceneRefractionSample);
    buffer.allowUnorderedAccess = true;
    buffer.debugName = L"LX.Scene.RefractionSamples";
    if (!device_->CreateBuffer(buffer, candidate->samples_, error))
    {
        return false;
    }
    const auto output = RHIBindingDesc::UavBuffer(candidate->samples_, static_cast<std::uint32_t>(count),
                                                  sizeof(SceneRefractionSample));
    candidate->outputs_ = device_->CreateBindings({&output, 1});
    if (!candidate->outputs_.IsValid())
    {
        return Fail(error, "Scene refraction output binding failed.");
    }
    for (std::uint32_t first = 0; first < count; first += DispatchPixels)
    {
        const RefractionConstants constants{
            math::transpose(viewProjection),
            math::transpose(math::inverse(viewProjection)),
            context.width,
            context.height,
            first,
            static_cast<std::uint32_t>((std::min)(count - first, std::uint64_t{DispatchPixels})),
            environment.IsValid(),
            64,
            32,
            0};
        const auto uploaded = device_->UploadConstants(&constants, sizeof(constants));
        if (!uploaded.IsValid())
        {
            return Fail(error, "Scene refraction constants allocation failed.");
        }
        candidate->constants_.push_back(uploaded);
    }
    if (candidate->recording_ != device_->GetCurrentUploadRecordingId() ||
        candidate->descriptors_ != device_->GetDescriptorVersionToken())
    {
        return Fail(error, "Scene refraction preparation changed upload/descriptor ownership.");
    }
    candidate->self_ = candidate;
    result = std::move(candidate);
    error.clear();
    return true;
}

void SceneRefractionFrame::CheckCurrent(const EnhancedRenderGraph& graph) const
{
    if (graph_ != &graph || graphEpoch_ != graph.ResourceEpoch() ||
        recording_ != device_->GetCurrentUploadRecordingId() || descriptors_ != device_->GetDescriptorVersionToken())
    {
        throw std::runtime_error("Scene refraction has stale graph/upload/descriptor ownership.");
    }
}

const std::array<RGHandle, 2>& SceneRefractionFrame::DeclareInputs(EnhancedRenderGraph& graph) const
{
    if (graph_)
    {
        throw std::runtime_error("Scene refraction inputs require one declaration.");
    }
    graph_ = &graph;
    graphEpoch_ = graph.ResourceEpoch();
    CheckCurrent(graph);
    for (unsigned i = 0; i < inputs_.size(); ++i)
    {
        graphInputs_[i] = graph.ImportTexture(inputs_[i], RHIResourceState::Common, "LX.Scene.RefractionInput");
    }
    graphSamples_ = graph.ImportBuffer(samples_, RHIResourceState::Common, "LX.Scene.RefractionSamples");
    return graphInputs_;
}

void SceneRefractionFrame::DeclareBackground(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth) const
{
    CheckCurrent(graph);
    if (backgroundColor_.IsValid())
    {
        throw std::runtime_error("Scene refraction background requires one snapshot.");
    }
    RGTextureDesc desc;
    desc.width = width_;
    desc.height = height_;
    desc.format = RHIFormat::RGBA16Float;
    desc.name = "LX.Scene.OpaqueColor";
    backgroundColor_ = graph.CreateTexture(desc);
    desc.format = RHIFormat::D32Float;
    desc.allowDepthStencil = true;
    desc.name = "LX.Scene.OpaqueDepth";
    backgroundDepth_ = graph.CreateTexture(desc);
    const auto owner = self_.lock();
    graph.AddPass("LX.Scene.RefractionBackground",
                  {{lighting, RHIResourceState::CopySource},
                   {depth, RHIResourceState::CopySource},
                   {backgroundColor_, RHIResourceState::CopyDest},
                   {backgroundDepth_, RHIResourceState::CopyDest}},
                  [owner, lighting, depth](const auto& execution) {
                      owner->CheckCurrent(*execution.graph);
                      execution.encoder->CopyResource(execution.ResolveHandle(owner->backgroundColor_),
                                                      execution.ResolveHandle(lighting));
                      execution.encoder->CopyResource(execution.ResolveHandle(owner->backgroundDepth_),
                                                      execution.ResolveHandle(depth));
                  });
}

void SceneRefractionFrame::DeclareBake(EnhancedRenderGraph& graph, const SceneLookupFrame& lookup, RGHandle owners,
                                       RGHandle shadow) const
{
    CheckCurrent(graph);
    const auto owner = self_.lock();
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{owners, RHIResourceState::ShaderResource},
                                                       {backgroundColor_, RHIResourceState::ShaderResource},
                                                       {backgroundDepth_, RHIResourceState::ShaderResource},
                                                       {graphSamples_, RHIResourceState::UnorderedAccess}};
    std::array<RHIBindingDesc, 17> descriptions;
    for (unsigned i = 0; i < 11; ++i)
    {
        uses.push_back({graph.FindImportedTexture(lookup.Inputs()[i]), RHIResourceState::ShaderResource});
        descriptions[i] = RHIBindingDesc::Srv2D(lookup.Inputs()[i], RHIFormat::RGBA32Float);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        uses.push_back({graphInputs_[i], RHIResourceState::ShaderResource});
        descriptions[11 + i] = RHIBindingDesc::Srv2D(inputs_[i], RHIFormat::RGBA32Float);
    }
    descriptions[16] = RHIBindingDesc::SrvCube(environment_,
                                               environment_.IsValid() ? device_->DescribeTexture(environment_).format
                                                                      : RHIFormat::RGBA16Float,
                                               1)
                           .OrNull();
    if (environment_.IsValid())
    {
        uses.push_back({graph.FindImportedTexture(environment_), RHIResourceState::ShaderResource});
    }
    if (volume_)
    {
        uses.push_back({volume_->GraphCoefficients(graph), RHIResourceState::ShaderResource});
        if (shadow.IsValid())
        {
            uses.push_back({shadow, RHIResourceState::ShaderResource});
        }
    }
    graph.AddPass(
        "LX.Scene.RefractionBake", uses, [owner, descriptions, owners, shadow](const auto& execution) mutable {
            owner->CheckCurrent(*execution.graph);
            descriptions[13] =
                RHIBindingDesc::Srv2D(execution.ResolveHandle(owner->backgroundColor_), RHIFormat::RGBA16Float);
            descriptions[14] =
                RHIBindingDesc::Srv2D(execution.ResolveHandle(owner->backgroundDepth_), RHIFormat::R32Float);
            descriptions[15] = RHIBindingDesc::Srv2D(execution.ResolveHandle(owners), RHIFormat::R32Uint);
            const auto table = owner->device_->CreateBindings(descriptions);
            if (!table.IsValid())
            {
                throw std::runtime_error("Scene refraction input binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.SetPipeline(RHIBindPoint::Compute, owner->bake_);
            encoder.SetBindings(RHIBindPoint::Compute, 1, table);
            encoder.SetBindings(RHIBindPoint::Compute, 2, owner->outputs_);
            const std::array<RHIBindingDesc, 2> emptyLights{
                RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull(),
                RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull()};
            const auto volumeLights = owner->device_->CreateBindings(
                owner->volume_ ? owner->volume_->LightingBindings(shadow.IsValid() ? execution.ResolveHandle(shadow)
                                                                                   : RHITextureHandle{})
                               : emptyLights);
            if (!volumeLights.IsValid())
            {
                throw std::runtime_error("Scene refraction medium lighting binding failed.");
            }
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 3,
                                      owner->volume_ ? owner->volume_->TransportConstants()
                                                     : owner->emptyVolumeConstants_);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 4,
                                  owner->volume_ ? owner->volume_->Triangles() : owner->emptyVolumeBuffer_);
            encoder.SetRootBuffer(RHIBindPoint::Compute, 5,
                                  owner->volume_ ? RHIBufferSlice::Whole(owner->volume_->Coefficients())
                                                 : owner->emptyVolumeBuffer_);
            encoder.SetBindings(RHIBindPoint::Compute, 6, volumeLights);
            for (unsigned i = 0; i < owner->constants_.size(); ++i)
            {
                const auto count = (std::min)(std::uint64_t(owner->width_) * owner->height_ - i * DispatchPixels,
                                              std::uint64_t{DispatchPixels});
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_[i]);
                encoder.Dispatch(static_cast<std::uint32_t>((count + 31) / 32), 1, 1);
            }
        });
}

RGHandle SceneRefractionFrame::GraphSamples(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return graphSamples_;
}

RGHandle SceneRefractionFrame::GraphBackgroundColor(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return backgroundColor_;
}

RGHandle SceneRefractionFrame::GraphBackgroundDepth(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return backgroundDepth_;
}

void SceneRefractionResources::ShutdownAfterIdle()
{
    device_ = nullptr;
    bake_ = {};
    volumeBake_ = {};
}
} // namespace material_graph
