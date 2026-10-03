#include "MaterialGraphSceneVolume.h"

#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace material_graph
{
namespace
{
constexpr std::uint32_t DispatchPixels = 4096;

struct VolumeConstants
{
    math::matrix4x4 inverseViewProjection;
    math::vector4 eye;
    std::uint32_t width{}, height{}, first{}, dispatchCount{};
    std::uint32_t triangles{}, objects{}, lights{}, environment{};
    std::uint32_t hasShadow{};
    float shadowBlend{};
    std::uint32_t padding[2]{};
    math::matrix4x4 shadowProjection[3];
    math::vector4 shadowSplits, shadowBias, cameraForward;
    EnhancedLight light[64];
};
static_assert(sizeof(VolumeConstants) == SceneVolumeConstantsBytes);

bool Fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

bool AppendVolume(const MeshSurfaceInput& source, std::uint32_t object, std::vector<SceneVolumeTriangle>& triangles,
                  std::string& error)
{
    const auto& geometry = source.Geometry();
    if (!source.Bones().empty() || assets::Has(geometry.vertexAttributeMask, assets::VertexAttribute::BoneIndices))
    {
        return Fail(error, "Scene Volume currently requires a static closed boundary; skinned media are unsupported.");
    }
    if (!math::try_inverse(source.World()))
    {
        return Fail(error, "Scene Volume boundary has a singular world transform.");
    }
    std::map<std::array<float, 3>, std::uint32_t> welded;
    std::vector<std::uint32_t> vertices;
    std::vector<math::vector4> positions;
    for (std::uint32_t i = 0; i < source.Count(); ++i)
    {
        const auto* bytes = static_cast<const std::byte*>(geometry.vertexData) + std::size_t(i) * geometry.vertexStride;
        std::array<float, 3> position;
        std::memcpy(position.data(),
                    bytes + assets::OffsetOf(geometry.vertexAttributeMask, assets::VertexAttribute::Position),
                    sizeof(position));
        const auto [entry, inserted] = welded.try_emplace(position, static_cast<std::uint32_t>(welded.size()));
        vertices.push_back(entry->second);
        const auto& world = source.World();
        std::array<float, 3> transformed;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            transformed[axis] = position[0] * world.m[0][axis] + position[1] * world.m[1][axis] +
                                position[2] * world.m[2][axis] + world.m[3][axis];
        }
        if (!std::ranges::all_of(transformed,
                                 [](float value) { return std::isfinite(value) && std::abs(value) <= 1e6f; }))
        {
            return Fail(error, "Scene Volume world boundary exceeds the finite spatial range.");
        }
        positions.emplace_back(transformed[0], transformed[1], transformed[2], static_cast<float>(object));
    }
    struct Edge
    {
        unsigned count{};
        int orientation{};
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, Edge> edges;
    for (std::uint32_t i = 0; i < geometry.indexCount; i += 3)
    {
        const std::uint32_t indices[]{geometry.indexData[i], geometry.indexData[i + 1], geometry.indexData[i + 2]};
        for (unsigned side = 0; side < 3; ++side)
        {
            const auto a = vertices[indices[side]], b = vertices[indices[(side + 1) % 3]];
            if (a == b)
            {
                return Fail(error, "Scene Volume boundary contains a degenerate edge.");
            }
            auto& edge = edges[{(std::min)(a, b), (std::max)(a, b)}];
            ++edge.count;
            edge.orientation += a < b ? 1 : -1;
        }
        const auto& a = positions[indices[0]];
        const auto& b = positions[indices[1]];
        const auto& c = positions[indices[2]];
        const double ab[]{double(b.x) - a.x, double(b.y) - a.y, double(b.z) - a.z};
        const double ac[]{double(c.x) - a.x, double(c.y) - a.y, double(c.z) - a.z};
        if (std::hypot(ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0]) <=
            1e-12)
        {
            return Fail(error, "Scene Volume boundary contains a degenerate world triangle.");
        }
        triangles.push_back({a, b, c});
    }
    if (!std::ranges::all_of(
            edges, [](const auto& entry) { return entry.second.count == 2 && entry.second.orientation == 0; }))
    {
        return Fail(error, "Scene Volume needs a closed, consistently oriented manifold boundary.");
    }
    return true;
}
} // namespace

SceneVolumeFrame::~SceneVolumeFrame()
{
    if (device_ && coefficients_.IsValid())
    {
        device_->ReleaseBuffer(coefficients_);
    }
}

bool SceneVolumeResources::Initialize(const EnhancedFrameContext& context, std::string& error)
{
    if (device_)
    {
        return device_ == context.resources || Fail(error, "Scene Volume belongs to another device.");
    }
    const auto backend = RHIShaderCompiler::GetOutput();
    const auto other = backend == RHIShaderBinary::Dxil ? RHIShaderBinary::SpirV : RHIShaderBinary::Dxil;
    const auto file = RHIShaderSource::Resolve("MaterialGraphSceneVolumeBake.slang").string();
    RHIShaderCompileOptions options;
    options.strictMath = true;
    LX::Runtime::CompiledCompute shader;
    RHIShaderCompiler::VerifiedShader verification;
    if (!LX::Runtime::CompileCompute(file, "LXSceneVolumeCompositeCS", {}, options, shader, error) ||
        !RHIShaderCompiler::VerifyFile(file, "LXSceneVolumeCompositeCS", "cs_6_0", other, {}, verification, error,
                                       options))
    {
        return false;
    }
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(1),          RHILayout::SrvTable(2, 0),
                                              RHILayout::Srv(17),         RHILayout::Srv(18),
                                              RHILayout::SrvTable(2, 19), RHILayout::UavTable(1, 0)};
    const RHIStaticSamplerDesc samplers[]{
        {RHISampler::Linear(RHIAddressMode::Clamp), 2},
        {RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border, RHIBorderColor::OpaqueWhite), 3}};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, samplers}, error);
    if (!layout.IsValid())
    {
        return false;
    }
    RHIComputePipelineDesc desc;
    desc.layout = layout;
    desc.csBytecode = shader.stage.bytecode.Data();
    desc.csSize = shader.stage.bytecode.Size();
    LX::Runtime::ComputePipeline pipeline;
    if (!pipeline.Create(*context.psoManager, desc, std::move(shader.description), error))
    {
        return false;
    }
    device_ = context.resources;
    composite_ = std::move(pipeline);
    return true;
}

bool SceneVolumeResources::Prepare(const EnhancedFrameContext& context, const SceneViewInput& input,
                                   RHITextureHandle environment, const EnhancedShadowData& shadow,
                                   std::uint64_t memoryBudget, std::shared_ptr<const SceneVolumeFrame>& result,
                                   std::string& error)
{
    const auto pixels = std::uint64_t(context.width) * context.height;
    if (!context.resources || !context.rootSignatures || !context.psoManager || !pixels || pixels > UINT32_MAX ||
        !context.resources->GetCurrentUploadRecordingId())
    {
        return Fail(error, "Scene Volume requires current device services, viewport and upload recording.");
    }
    std::vector<SceneVolumeTriangle> triangles;
    std::uint32_t objects = 0;
    for (const auto& draw : input.Draws())
    {
        if (!draw.material->generation->cooked.product.program.volume)
        {
            continue;
        }
        if (objects >= SceneVolumeMaxObjects ||
            draw.geometry->Cost().triangles > SceneVolumeMaxTriangles - triangles.size())
        {
            return Fail(error, "Scene Volume exceeds its 16-object/128-triangle exact intersection budget.");
        }
        if (!AppendVolume(*draw.geometry->Source(), objects++, triangles, error))
        {
            return false;
        }
    }
    const auto inverse = math::try_inverse(input.ViewProjection());
    if (!inverse)
    {
        return Fail(error, "Scene Volume requires an invertible camera projection.");
    }
    for (float x : {-1.f, 1.f})
    {
        for (float y : {-1.f, 1.f})
        {
            const auto& matrix = *inverse;
            const float w = x * matrix.m[0][3] + y * matrix.m[1][3] + matrix.m[2][3] + matrix.m[3][3];
            if (!std::isfinite(w) || std::abs(w) <= 1e-6f)
            {
                return Fail(error, "Scene Volume currently requires a finite camera far plane.");
            }
        }
    }
    const auto constantsBytes =
        ((pixels + DispatchPixels - 1) / DispatchPixels) * ((sizeof(VolumeConstants) + 255) / 256 * 256);
    if (!objects || pixels * 8 + triangles.size() * sizeof(SceneVolumeTriangle) +
                            objects * (sizeof(SceneVolumeCoefficient) + 256) + constantsBytes >
                        memoryBudget)
    {
        return Fail(error, "Scene Volume needs media and sufficient graph/upload memory budget.");
    }
    if (!Initialize(context, error))
    {
        return false;
    }
    auto candidate = std::make_shared<SceneVolumeFrame>();
    candidate->device_ = device_;
    candidate->width_ = context.width;
    candidate->height_ = context.height;
    candidate->triangleCount_ = static_cast<std::uint32_t>(triangles.size());
    candidate->objectCount_ = objects;
    candidate->recording_ = device_->GetCurrentUploadRecordingId();
    candidate->descriptors_ = device_->GetDescriptorVersionToken();
    candidate->environment_ = environment;
    candidate->composite_ = composite_.GetGeneration();
    candidate->triangles_ =
        device_->AllocateUpload({triangles.size() * sizeof(SceneVolumeTriangle), RHIUploadUsage::Raw, 16});
    if (!candidate->triangles_.IsWritable())
    {
        return Fail(error, "Scene Volume triangle upload failed.");
    }
    std::memcpy(candidate->triangles_.cpuAddress, triangles.data(), triangles.size() * sizeof(SceneVolumeTriangle));
    RHIBufferDesc buffer;
    buffer.bytes = objects * sizeof(SceneVolumeCoefficient);
    buffer.allowUnorderedAccess = true;
    buffer.debugName = L"LX.Scene.VolumeCoefficients";
    if (!device_->CreateBuffer(buffer, candidate->coefficients_, error))
    {
        return false;
    }
    const auto description =
        RHIBindingDesc::UavBuffer(candidate->coefficients_, objects, sizeof(SceneVolumeCoefficient));
    candidate->coefficientOutput_ = device_->CreateBindings({&description, 1});
    if (!candidate->coefficientOutput_.IsValid())
    {
        return Fail(error, "Scene Volume coefficient binding failed.");
    }
    VolumeConstants constants;
    constants.inverseViewProjection = math::transpose(math::inverse(input.ViewProjection()));
    const auto eye = input.Surface().eye;
    constants.eye = {eye[0], eye[1], eye[2], 1};
    constants.width = context.width;
    constants.height = context.height;
    constants.triangles = candidate->triangleCount_;
    constants.objects = objects;
    constants.environment = environment.IsValid();
    constants.hasShadow = shadow.enabled;
    constants.shadowBlend = shadow.cascadeBlendBand;
    constants.shadowSplits = shadow.splitDepths;
    constants.shadowBias = shadow.bias;
    constants.cameraForward = shadow.cameraForward;
    for (unsigned i = 0; i < 3; ++i)
    {
        constants.shadowProjection[i] = math::transpose(shadow.lightViewProjection[i]);
    }
    if (context.lights)
    {
        constants.lights = static_cast<std::uint32_t>((std::min)(context.lights->size(), std::size_t{64}));
        std::copy_n(context.lights->begin(), constants.lights, constants.light);
    }
    for (std::uint32_t first = 0; first < pixels; first += DispatchPixels)
    {
        constants.first = first;
        constants.dispatchCount = static_cast<std::uint32_t>((std::min)(pixels - first, std::uint64_t{DispatchPixels}));
        const auto uploaded = device_->UploadConstants(&constants, sizeof(constants));
        if (!uploaded.IsValid())
        {
            return Fail(error, "Scene Volume constants allocation failed.");
        }
        candidate->constants_.push_back(uploaded);
    }
    if (candidate->recording_ != device_->GetCurrentUploadRecordingId() ||
        candidate->descriptors_ != device_->GetDescriptorVersionToken())
    {
        return Fail(error, "Scene Volume preparation changed upload/descriptor ownership.");
    }
    candidate->self_ = candidate;
    result = std::move(candidate);
    error.clear();
    return true;
}

void SceneVolumeFrame::CheckCurrent(const EnhancedRenderGraph& graph) const
{
    if (graph_ != &graph || graphEpoch_ != graph.ResourceEpoch() ||
        recording_ != device_->GetCurrentUploadRecordingId() || descriptors_ != device_->GetDescriptorVersionToken())
    {
        throw std::runtime_error("Scene Volume has stale graph/upload/descriptor ownership.");
    }
}

void SceneVolumeFrame::DeclareCoefficients(EnhancedRenderGraph& graph,
                                           std::span<const SceneVolumeBinding> bindings) const
{
    if (graph_ || bindings.size() != objectCount_)
    {
        throw std::runtime_error("Scene Volume coefficients require one complete declaration.");
    }
    for (const auto& draw : bindings)
        if (!draw.material || !draw.pipeline || !draw.pipeline->IsValid() || !draw.constants.IsValid())
            throw std::runtime_error("Scene Volume coefficients require retained LX compute owners and bindings.");
    graph_ = &graph;
    graphEpoch_ = graph.ResourceEpoch();
    CheckCurrent(graph);
    graphCoefficients_ = graph.ImportBuffer(coefficients_, RHIResourceState::Common, "LX.Scene.VolumeCoefficients");
    const auto owner = self_.lock();
    const std::vector<SceneVolumeBinding> draws(bindings.begin(), bindings.end());
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{graphCoefficients_, RHIResourceState::UnorderedAccess}};
    for (const auto& draw : draws)
    {
        for (const auto& texture : draw.material->resources.textures)
        {
            uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::ShaderResource});
        }
    }
    graph.AddPass("LX.Scene.VolumeCoefficients", uses, [owner, draws](const auto& execution) {
        owner->CheckCurrent(*execution.graph);
        auto& encoder = *execution.encoder;
        for (const auto& draw : draws)
        {
            encoder.SetPipeline(RHIBindPoint::Compute, draw.pipeline->GetHandle());
            std::string error;
            if (!RenderBindingCache::Bind(*owner->device_, encoder, RHIBindPoint::Compute, *draw.material, error))
            {
                throw std::runtime_error(error);
            }
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, draw.constants);
            encoder.SetBindings(RHIBindPoint::Compute, 1, owner->coefficientOutput_);
            encoder.Dispatch(1, 1, 1);
        }
    });
}

RGHandle SceneVolumeFrame::DeclareComposite(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth,
                                            RGHandle shadow) const
{
    CheckCurrent(graph);
    if (output_.IsValid())
    {
        throw std::runtime_error("Scene Volume composite requires one declaration.");
    }
    RGTextureDesc desc;
    desc.width = width_;
    desc.height = height_;
    desc.format = RHIFormat::RGBA16Float;
    desc.allowUnorderedAccess = true;
    desc.allowRenderTarget = true;
    desc.name = "LX.Scene.VolumeColor";
    output_ = graph.CreateTexture(desc);
    std::vector<EnhancedRenderGraph::RGPassUsage> uses{{lighting, RHIResourceState::ShaderResource},
                                                       {depth, RHIResourceState::ShaderResource},
                                                       {graphCoefficients_, RHIResourceState::ShaderResource},
                                                       {output_, RHIResourceState::UnorderedAccess}};
    if (shadow.IsValid())
    {
        uses.push_back({shadow, RHIResourceState::ShaderResource});
    }
    if (environment_.IsValid())
    {
        uses.push_back({graph.FindImportedTexture(environment_), RHIResourceState::ShaderResource});
    }
    const auto owner = self_.lock();
    graph.AddPass("LX.Scene.VolumeComposite", uses, [owner, lighting, depth, shadow](const auto& execution) {
        owner->CheckCurrent(*execution.graph);
        const RHIBindingDesc inputs[]{RHIBindingDesc::Srv2D(execution.ResolveHandle(lighting), RHIFormat::RGBA16Float),
                                      RHIBindingDesc::Srv2D(execution.ResolveHandle(depth), RHIFormat::R32Float)};
        const RHIBindingDesc lights[]{
            RHIBindingDesc::SrvArray(shadow.IsValid() ? execution.ResolveHandle(shadow) : RHITextureHandle{},
                                     RHIFormat::R32Float, 3)
                .OrNull(),
            RHIBindingDesc::SrvCube(owner->environment_,
                                    owner->environment_.IsValid()
                                        ? owner->device_->DescribeTexture(owner->environment_).format
                                        : RHIFormat::RGBA16Float,
                                    1)
                .OrNull()};
        const auto output = RHIBindingDesc::Uav2D(execution.ResolveHandle(owner->output_), RHIFormat::RGBA16Float);
        const auto inputTable = owner->device_->CreateBindings(inputs);
        const auto lightTable = owner->device_->CreateBindings(lights);
        const auto outputTable = owner->device_->CreateBindings({&output, 1});
        if (!inputTable.IsValid() || !lightTable.IsValid() || !outputTable.IsValid())
        {
            throw std::runtime_error("Scene Volume composite binding failed.");
        }
        auto& encoder = *execution.encoder;
        encoder.SetPipeline(RHIBindPoint::Compute, owner->composite_->GetHandle());
        encoder.SetBindings(RHIBindPoint::Compute, 1, inputTable);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 2, owner->triangles_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 3, RHIBufferSlice::Whole(owner->coefficients_));
        encoder.SetBindings(RHIBindPoint::Compute, 4, lightTable);
        encoder.SetBindings(RHIBindPoint::Compute, 5, outputTable);
        for (unsigned i = 0; i < owner->constants_.size(); ++i)
        {
            const auto count = (std::min)(std::uint64_t(owner->width_) * owner->height_ - i * DispatchPixels,
                                          std::uint64_t{DispatchPixels});
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, owner->constants_[i]);
            encoder.Dispatch(static_cast<std::uint32_t>((count + 31) / 32), 1, 1);
        }
    });
    if (environment_.IsValid())
    {
        // The external Scene/texture cache admits this cube in pixel-readable
        // state on the next frame. Volume compute must restore that contract.
        graph.AddPass(
            "LX.Scene.VolumeEnvironmentRestore",
            {{graph.FindImportedTexture(environment_), RHIResourceState::PixelShaderResource}},
            [owner](const auto& execution) { owner->CheckCurrent(*execution.graph); }, true);
    }
    return output_;
}

RGHandle SceneVolumeFrame::GraphCoefficients(const EnhancedRenderGraph& graph) const
{
    CheckCurrent(graph);
    return graphCoefficients_;
}

std::array<RHIBindingDesc, 2> SceneVolumeFrame::LightingBindings(RHITextureHandle shadow) const
{
    return {RHIBindingDesc::SrvArray(shadow, RHIFormat::R32Float, 3).OrNull(),
            RHIBindingDesc::SrvCube(
                environment_,
                environment_.IsValid() ? device_->DescribeTexture(environment_).format : RHIFormat::RGBA16Float, 1)
                .OrNull()};
}

void SceneVolumeResources::ShutdownAfterIdle()
{
    device_ = nullptr;
    composite_ = {};
}
} // namespace material_graph
