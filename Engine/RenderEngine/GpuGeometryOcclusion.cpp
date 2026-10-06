#include "GpuGeometryOcclusion.h"

#include "RHI/RHIShaderSource.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <utility>

namespace
{
    struct SharedGeometryOcclusionConstants
    {
        std::uint32_t sourceWidth{}, sourceHeight{}, targetWidth{}, targetHeight{};
    };
    static_assert(sizeof(SharedGeometryOcclusionConstants) == 16);

    bool SameOcclusionHandle(RGHandle left, RGHandle right)
    {
        return left.index == right.index && left.version == right.version &&
            left.kind == right.kind && left.epoch == right.epoch;
    }
}

GpuGeometryOcclusion::ViewKey GpuGeometryOcclusion::CaptureView(
    const EnhancedFrameContext& context, const math::matrix4x4& viewProjection)
{
    ViewKey key;
    key.device = context.resources;
    key.recording = context.resources ? context.resources->GetCurrentUploadRecordingId() : 0u;
    key.descriptors = context.resources ? context.resources->GetDescriptorVersionToken() : 0u;
    key.frameId = context.frameId;
    key.sceneEpoch = context.sceneEpoch;
    key.width = context.width;
    key.height = context.height;
    for (unsigned row = 0; row < 4; ++row)
    {
        for (unsigned column = 0; column < 4; ++column)
        {
            key.projectionBits[row * 4u + column] = std::bit_cast<std::uint32_t>(viewProjection.m[row][column]);
        }
    }
    return key;
}

std::uint32_t GpuGeometryOcclusion::LevelCount(std::uint32_t width, std::uint32_t height)
{
    if (!width || !height || width > (1u << kMaximumLevels) || height > (1u << kMaximumLevels))
    {
        return 0;
    }
    std::uint32_t levels = 0;
    do
    {
        width = (width + 1u) / 2u;
        height = (height + 1u) / 2u;
        ++levels;
    } while (width > 1u || height > 1u);
    return levels;
}

bool GpuGeometryOcclusion::Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
    std::shared_ptr<const Pyramid>& result, std::string& error)
{
    result.reset();
    const auto levels = LevelCount(context.width, context.height);
    if (!context.resources || !context.rootSignatures || !context.psoManager || !levels ||
        !context.resources->GetCurrentUploadRecordingId() || (m_device && m_device != context.resources))
    {
        error = "Current-depth occlusion requires one live device/recording and a supported nonempty view.";
        return false;
    }
    if (!m_build.GetGeneration())
    {
        const auto source = RHIShaderSource::Resolve("GeometryOcclusionBuild.slang").string();
        RHIShaderCompileOptions options;
        options.strictMath = true;
        LX::Runtime::CompiledCompute compiled;
        if (!LX::Runtime::CompileCompute(source, "GeometryOcclusionBuildCS", {}, options, compiled, error))
        {
            return false;
        }
        const RHIPipelineLayoutParam parameters[]{
            RHILayout::Cbv(0), RHILayout::SrvTable(1, 0), RHILayout::UavTable(1, 0)};
        const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIComputePipelineDesc description;
        description.layout = layout;
        description.csBytecode = compiled.stage.bytecode.Data();
        description.csSize = compiled.stage.bytecode.Size();
        if (!m_build.Create(*context.psoManager, description, std::move(compiled.description), error))
        {
            return false;
        }
        m_device = context.resources;
    }
    auto pyramid = std::make_shared<Pyramid>();
    pyramid->m_view = CaptureView(context, viewProjection);
    pyramid->m_levelCount = levels;
    pyramid->m_build = m_build.GetGeneration();
    pyramid->m_self = pyramid;
    result = std::move(pyramid);
    error.clear();
    return true;
}

void GpuGeometryOcclusion::Pyramid::CheckCurrent(const EnhancedRenderGraph* graph) const
{
    if (!m_view.device || m_view.device->GetCurrentUploadRecordingId() != m_view.recording ||
        m_view.device->GetDescriptorVersionToken() != m_view.descriptors ||
        (graph && (m_graph != graph || m_graphEpoch != graph->ResourceEpoch() ||
            &graph->DeviceServices() != m_view.device)))
    {
        throw std::runtime_error("Current-depth pyramid escaped its device, recording, descriptors or graph epoch.");
    }
}

void GpuGeometryOcclusion::Pyramid::RequireView(const ViewKey& view) const
{
    CheckCurrent();
    if (view != m_view)
    {
        throw std::runtime_error("Current-depth pyramid and culler do not describe the same sealed view.");
    }
}

void GpuGeometryOcclusion::Pyramid::Declare(EnhancedRenderGraph& graph, RGHandle depth) const
{
    CheckCurrent();
    if (m_graph)
    {
        CheckCurrent(&graph);
        if (!SameOcclusionHandle(depth, m_depth))
        {
            throw std::runtime_error("Current-depth pyramid was redeclared with a different depth version.");
        }
        return;
    }
    if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned ||
        &graph.DeviceServices() != m_view.device || !depth.IsValid() ||
        depth.kind != RGResourceKind::Texture || depth.epoch != graph.ResourceEpoch())
    {
        throw std::runtime_error("Current-depth pyramid requires an earlier same-view RG2 depth version.");
    }
    const auto owner = m_self.lock();
    if (!owner)
    {
        throw std::runtime_error("Current-depth pyramid requires its retained prepared owner.");
    }
    m_graph = &graph;
    m_graphEpoch = graph.ResourceEpoch();
    m_depth = depth;
    auto source = depth;
    auto width = m_view.width, height = m_view.height;
    for (std::uint32_t level = 0; level < m_levelCount; ++level)
    {
        const SharedGeometryOcclusionConstants constants{width, height, (width + 1u) / 2u, (height + 1u) / 2u};
        RGTextureDesc description;
        description.width = constants.targetWidth;
        description.height = constants.targetHeight;
        description.format = RHIFormat::R32Float;
        description.allowUnorderedAccess = true;
        description.name = "Geometry.Occlusion." + std::to_string(level);
        const auto target = graph.Write(graph.CreateTexture(description));
        m_levels.push_back(target);
        graph.AddPass(description.name,
            {{source, RHIResourceState::ShaderResource, RGAccessMode::Read},
             {target, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [owner, source, target, constants, level](const auto& execution) {
                owner->CheckCurrent(execution.graph);
                auto* device = owner->m_view.device;
                const auto sourceTexture = execution.ResolveHandle(source);
                const auto info = device->DescribeTexture(sourceTexture);
                if (info.width != constants.sourceWidth || info.height != constants.sourceHeight ||
                    info.depthOrArraySize != 1u ||
                    info.format != (level == 0u ? RHIFormat::D32Float : RHIFormat::R32Float))
                {
                    throw std::runtime_error("Current-depth pyramid source does not match its prepared view.");
                }
                const RHIBindingDesc input[]{level == 0u ? RHIBindingDesc::SrvDepth(sourceTexture)
                    : RHIBindingDesc::Srv2D(sourceTexture, RHIFormat::R32Float)};
                const RHIBindingDesc output[]{RHIBindingDesc::Uav2D(execution.ResolveHandle(target), RHIFormat::R32Float)};
                const auto inputs = device->CreateBindings(input);
                const auto outputs = device->CreateBindings(output);
                const auto uploaded = device->UploadConstants(&constants, sizeof(constants));
                if (!inputs.IsValid() || !outputs.IsValid() || !uploaded.IsValid())
                {
                    throw std::runtime_error("Current-depth pyramid binding or upload failed.");
                }
                auto& encoder = *execution.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, owner->m_build->GetHandle());
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uploaded);
                encoder.SetBindings(RHIBindPoint::Compute, 1, inputs);
                encoder.SetBindings(RHIBindPoint::Compute, 2, outputs);
                encoder.Dispatch((constants.targetWidth + 7u) / 8u, (constants.targetHeight + 7u) / 8u, 1u);
            });
        source = target;
        width = constants.targetWidth;
        height = constants.targetHeight;
    }
}

void GpuGeometryOcclusion::Pyramid::AddReadUsages(EnhancedRenderGraph& graph,
    std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    CheckCurrent(&graph);
    if (m_levels.size() != m_levelCount)
    {
        throw std::runtime_error("Current-depth pyramid is not completely declared.");
    }
    for (const auto level : m_levels)
    {
        const auto duplicate = std::find_if(usages.begin(), usages.end(), [&](const auto& usage) {
            return SameOcclusionHandle(level, usage.handle);
        });
        if (duplicate == usages.end())
        {
            usages.push_back({level, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        else if (duplicate->state != RHIResourceState::ShaderResource || duplicate->access != RGAccessMode::Read)
        {
            throw std::runtime_error("Current-depth pyramid consumer has conflicting resource usage.");
        }
    }
}

RHIBindingTable GpuGeometryOcclusion::Pyramid::Bindings(const EnhancedRenderGraph::ExecuteContext& execution) const
{
    CheckCurrent(execution.graph);
    if (m_levels.size() != m_levelCount || m_levels.empty())
    {
        throw std::runtime_error("Current-depth pyramid binding requires a complete declaration.");
    }
    std::array<RHIBindingDesc, kMaximumLevels> inputs;
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        const auto levelIndex = (std::min)(index, m_levels.size() - 1u);
        inputs[index] = RHIBindingDesc::Srv2D(execution.ResolveHandle(m_levels[levelIndex]), RHIFormat::R32Float);
    }
    const auto bindings = m_view.device->CreateBindings(inputs);
    if (!bindings.IsValid())
    {
        throw std::runtime_error("Current-depth pyramid consumer bindings failed.");
    }
    return bindings;
}

void GpuGeometryOcclusion::ShutdownAfterIdle()
{
    m_build = {};
    m_device = nullptr;
}
