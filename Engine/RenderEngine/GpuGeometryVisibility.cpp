#include "GpuGeometryVisibility.h"

#include "RHI/RHIShaderSource.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    constexpr std::uint32_t kVisibilityThreads = 64;
    constexpr std::uint32_t kVisibilityDispatchWidth = 65535;

    struct GeometryVisibilityConstants
    {
        math::vector4 planes[6]{};
        std::uint32_t candidateCount{}, binCount{}, planeMask{}, mode{};
        // Explicit columns avoid depending on Slang's matrix packing convention.
        math::vector4 projectionColumns[4]{};
        std::uint32_t width{}, height{}, occlusionLevels{}, projectionValid{};
        math::vector4 shadowReceiverSphere{}, shadowLightDirection{};
    };
    static_assert(sizeof(GeometryVisibilityConstants) == 224);
    static_assert(offsetof(GeometryVisibilityConstants, mode) == 108);
    static_assert(offsetof(GeometryVisibilityConstants, projectionColumns) == 112);
    static_assert(offsetof(GeometryVisibilityConstants, width) == 176);
    static_assert(offsetof(GeometryVisibilityConstants, shadowReceiverSphere) == 192);
    static_assert(offsetof(GeometryVisibilityConstants, shadowLightDirection) == 208);

    bool VisibilityFail(std::string& error, std::string message)
    {
        error = std::move(message);
        return false;
    }

    GeometryVisibilityConstants VisibilityConstants(const math::matrix4x4& matrix,
                                                     std::uint32_t candidates, std::uint32_t bins)
    {
        GeometryVisibilityConstants constants;
        constants.candidateCount = candidates;
        constants.binCount = bins;
        for (unsigned row = 0; row < 4; ++row)
        {
            for (unsigned column = 0; column < 4; ++column)
            {
                if (!std::isfinite(matrix.m[row][column]))
                {
                    // An unknown camera must not make otherwise valid geometry disappear.
                    return constants;
                }
            }
        }

        for (unsigned column = 0; column < 4; ++column)
        {
            constants.projectionColumns[column] = {matrix.m[0][column], matrix.m[1][column],
                                                    matrix.m[2][column], matrix.m[3][column]};
        }
        constants.projectionValid = 1;

        // p * ViewProjection: clip inequalities are col3 +/- col0/1,
        // col2 >= 0 and col3 - col2 >= 0, for both renderer backends.
        for (unsigned plane = 0; plane < 6; ++plane)
        {
            std::array<double, 4> coefficients{};
            for (unsigned row = 0; row < 4; ++row)
            {
                if (plane == 4)
                {
                    coefficients[row] = matrix.m[row][2];
                }
                else
                {
                    const unsigned column = plane < 4 ? plane / 2 : 2;
                    const double sign = plane < 4 && plane % 2 == 0 ? 1.0 : -1.0;
                    coefficients[row] = double(matrix.m[row][3]) + sign * matrix.m[row][column];
                }
            }
            const double length = std::hypot(coefficients[0], coefficients[1], coefficients[2]);
            if (!(length > 0.0) || !std::isfinite(length))
            {
                // An infinite far plane (or another degenerate plane) cannot reject anything.
                continue;
            }
            const math::vector4 normalized{
                static_cast<float>(coefficients[0] / length), static_cast<float>(coefficients[1] / length),
                static_cast<float>(coefficients[2] / length), static_cast<float>(coefficients[3] / length)};
            if (std::isfinite(normalized.x) && std::isfinite(normalized.y) &&
                std::isfinite(normalized.z) && std::isfinite(normalized.w))
            {
                constants.planes[plane] = normalized;
                constants.planeMask |= 1u << plane;
            }
        }
        return constants;
    }
} // namespace

GpuGeometryVisibility::~GpuGeometryVisibility()
{
    // Owners destroy render passes only after the device/submission drain, just
    // as for their other persistent render resources.
    ShutdownAfterIdle();
}

GpuGeometryVisibility::Frame::~Frame()
{
    if (m_device)
    {
        if (m_visibleIds.IsValid())
        {
            m_device->ReleaseBuffer(m_visibleIds);
        }
        if (m_arguments.IsValid())
        {
            m_device->ReleaseBuffer(m_arguments);
        }
    }
}

bool GpuGeometryVisibility::Initialize(const EnhancedFrameContext& context, std::string& error)
{
    if (m_device)
    {
        return m_device == context.resources || VisibilityFail(error, "GPU visibility belongs to another device.");
    }
    const auto source = RHIShaderSource::Resolve("GeometryVisibility.slang").string();
    LX::Runtime::CompiledCompute reset, cull;
    RHIShaderCompileOptions options;
    options.strictMath = true;
    if (!LX::Runtime::CompileCompute(source, "GeometryVisibilityResetCS", {}, options, reset, error) ||
        !LX::Runtime::CompileCompute(source, "GeometryVisibilityCullCS", {}, options, cull, error))
    {
        return false;
    }
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::Srv(0),
                                              RHILayout::Srv(1), RHILayout::UavBufferTable(2, 0)};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
    if (!layout.IsValid())
    {
        return false;
    }
    LX::Runtime::ComputePipeline resetPipeline, cullPipeline;
    RHIComputePipelineDesc description;
    description.layout = layout;
    description.csBytecode = reset.stage.bytecode.Data();
    description.csSize = reset.stage.bytecode.Size();
    if (!resetPipeline.Create(*context.psoManager, description, std::move(reset.description), error))
    {
        return false;
    }
    description.csBytecode = cull.stage.bytecode.Data();
    description.csSize = cull.stage.bytecode.Size();
    if (!cullPipeline.Create(*context.psoManager, description, std::move(cull.description), error))
    {
        return false;
    }
    m_reset = std::move(resetPipeline);
    m_cull = std::move(cullPipeline);
    m_device = context.resources;
    m_device->RegisterUploadTransactionListener(this);
    return true;
}

bool GpuGeometryVisibility::InitializeOcclusion(const EnhancedFrameContext& context, std::string& error)
{
    if (m_occlusionCull.GetGeneration())
    {
        return true;
    }
    const auto source = RHIShaderSource::Resolve("GeometryVisibility.slang").string();
    RHIShaderCompileOptions options;
    options.strictMath = true;
    RHIShaderPermutation cullPermutation;
    LX::Runtime::CompiledCompute cull;
    if (!cullPermutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error) ||
        !LX::Runtime::CompileCompute(source, "GeometryVisibilityCullCS", cullPermutation, options, cull, error))
    {
        return false;
    }
    const RHIPipelineLayoutParam cullParameters[]{
        RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1), RHILayout::UavBufferTable(2, 0),
        RHILayout::SrvTable(kOcclusionLevels, 2)};
    const auto cullLayout = context.rootSignatures->GetOrCreate({cullParameters, {}}, error);
    if (!cullLayout.IsValid())
    {
        return false;
    }
    LX::Runtime::ComputePipeline cullPipeline;
    RHIComputePipelineDesc description;
    description.layout = cullLayout;
    description.csBytecode = cull.stage.bytecode.Data();
    description.csSize = cull.stage.bytecode.Size();
    if (!cullPipeline.Create(*context.psoManager, description, std::move(cull.description), error))
    {
        return false;
    }
    m_occlusionCull = std::move(cullPipeline);
    return true;
}

bool GpuGeometryVisibility::PrepareOcclusionPipelines(const EnhancedFrameContext& context, std::string& error)
{
    if (!context.resources || !context.rootSignatures || !context.psoManager ||
        !context.resources->GetCurrentUploadRecordingId() || !GpuGeometryOcclusion::LevelCount(context.width, context.height))
    {
        return VisibilityFail(error, "GPU occlusion preflight requires an active supported view.");
    }
    return Initialize(context, error) && InitializeOcclusion(context, error);
}

bool GpuGeometryVisibility::Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                                    std::span<const Candidate> candidates, std::span<const Bin> bins,
                                    std::shared_ptr<const Frame>& result, std::string& error,
                                    bool currentFrameOcclusion)
{
    return PrepareInternal(context, viewProjection, candidates, bins, result, error, currentFrameOcclusion, nullptr);
}

bool GpuGeometryVisibility::PreparePipelines(const EnhancedFrameContext& context, std::string& error)
{
    if (!context.resources || (m_device && m_device != context.resources))
    {
        return VisibilityFail(error, "GPU visibility preflight requires its current device services.");
    }
    const auto capabilities = context.resources->GetIndirectDrawCapabilities();
    if (!capabilities.indexedDraw && !capabilities.nonIndexedDraw)
    {
        error.clear();
        return true;
    }
    if (!context.rootSignatures || !context.psoManager)
    {
        return VisibilityFail(error, "GPU visibility preflight requires pipeline services.");
    }
    return Initialize(context, error);
}

bool GpuGeometryVisibility::PrepareShadow(const EnhancedFrameContext& context, const math::vector4& receiverSphere,
                                         const math::vector4& lightDirection, std::span<const Candidate> candidates,
                                         std::span<const Bin> bins, std::shared_ptr<const Frame>& result,
                                         std::string& error)
{
    const ShadowCullVolume shadow{receiverSphere, lightDirection};
    return PrepareInternal(context, math::matrix4x4::identity(), candidates, bins, result, error, false, &shadow);
}

bool GpuGeometryVisibility::PrepareInternal(const EnhancedFrameContext& context,
                                          const math::matrix4x4& viewProjection,
                                          std::span<const Candidate> candidates, std::span<const Bin> bins,
                                          std::shared_ptr<const Frame>& result, std::string& error,
                                          bool currentFrameOcclusion, const ShadowCullVolume* shadow)
{
    result.reset();
    if (!context.resources || (m_device && m_device != context.resources))
    {
        return VisibilityFail(error, "GPU visibility requires its current device services.");
    }
    if (candidates.empty() && bins.empty())
    {
        error.clear();
        return true;
    }
    const auto capabilities = context.resources->GetIndirectDrawCapabilities();
    if (!capabilities.indexedDraw && !capabilities.nonIndexedDraw)
    {
        error.clear();
        return true;
    }
    if (!context.rootSignatures || !context.psoManager || !context.resources->GetCurrentUploadRecordingId())
    {
        return VisibilityFail(error, "GPU visibility requires pipeline services and an active upload recording.");
    }
    constexpr auto maximum = (std::numeric_limits<std::uint32_t>::max)();
    if (bins.empty() || bins.size() > maximum || candidates.size() > maximum)
    {
        return VisibilityFail(error, "GPU visibility requires nonempty bins and 32-bit candidate/bin counts.");
    }
    struct OutputRange
    {
        std::uint32_t offset{}, count{};
    };
    std::vector<OutputRange> ranges(bins.size());
    for (const auto& candidate : candidates)
    {
        if (candidate.bin >= bins.size() || candidate.outputOffset % kOutputAlignment != 0)
        {
            return VisibilityFail(error, "GPU visibility candidate has an invalid bin or unaligned output offset.");
        }
        auto& range = ranges[candidate.bin];
        if (range.count && range.offset != candidate.outputOffset)
        {
            return VisibilityFail(error, "GPU visibility candidates in one bin must share an output offset.");
        }
        range.offset = candidate.outputOffset;
        ++range.count;
    }
    std::vector<std::vector<std::uint8_t>> preservedSources(bins.size());
    for (std::size_t bin = 0; bin < bins.size(); ++bin)
    {
        if (bins[bin].preservedInstanceCount != 0u)
        {
            if (ranges[bin].count != bins[bin].preservedInstanceCount)
            {
                return VisibilityFail(error, "Preserved GPU bins require one candidate per original instance.");
            }
            preservedSources[bin].resize(ranges[bin].count, 0u);
        }
    }
    for (const auto& candidate : candidates)
    {
        auto& seen = preservedSources[candidate.bin];
        if (!seen.empty())
        {
            if (candidate.sourceIndex >= seen.size() || seen[candidate.sourceIndex] != 0u)
            {
                return VisibilityFail(error, "Preserved GPU bins require unique original instance IDs 0..N-1.");
            }
            seen[candidate.sourceIndex] = 1u;
        }
    }
    std::erase_if(ranges, [](const auto& range) { return range.count == 0; });
    std::ranges::sort(ranges, {}, &OutputRange::offset);
    std::uint64_t outputEnd = 0;
    for (const auto& range : ranges)
    {
        const auto end = std::uint64_t(range.offset) + range.count;
        if (range.offset < outputEnd || end > maximum)
        {
            return VisibilityFail(error, "GPU visibility output ranges overlap or exceed 32-bit addressing.");
        }
        outputEnd = end;
    }
    if (!Initialize(context, error))
    {
        return false;
    }
    if (currentFrameOcclusion &&
        (!context.width || !context.height || context.width > (1u << kOcclusionLevels) ||
         context.height > (1u << kOcclusionLevels)))
    {
        return VisibilityFail(error, "GPU occlusion requires a nonempty view within the pyramid extent limit.");
    }
    if (currentFrameOcclusion && !InitializeOcclusion(context, error))
    {
        return false;
    }

    auto frame = std::shared_ptr<Frame>(new Frame);
    frame->m_device = m_device;
    frame->m_recording = m_device->GetCurrentUploadRecordingId();
    frame->m_preparedStats.candidateCount = static_cast<std::uint32_t>(candidates.size());
    for (const auto& bin : bins)
    {
        if (bin.preservedInstanceCount != 0u)
        {
            ++frame->m_preparedStats.preservedBins;
        }
        else
        {
            ++frame->m_preparedStats.compactedBins;
        }
    }
    for (const auto& candidate : candidates)
    {
        if ((candidate.flags & kConservative) != 0u || !(candidate.sphere.w > 0.f)
            || !std::isfinite(candidate.sphere.x) || !std::isfinite(candidate.sphere.y)
            || !std::isfinite(candidate.sphere.z) || !std::isfinite(candidate.sphere.w))
        {
            ++frame->m_preparedStats.conservativeCandidates;
        }
    }
    frame->m_candidateCount = static_cast<std::uint32_t>(candidates.size());
    frame->m_binCount = static_cast<std::uint32_t>(bins.size());
    frame->m_outputCount = static_cast<std::uint32_t>((std::max)(std::uint64_t{1}, outputEnd));
    frame->m_reset = m_reset.GetGeneration();
    frame->m_cull = m_cull.GetGeneration();
    if (currentFrameOcclusion)
    {
        frame->m_occlusionCull = m_occlusionCull.GetGeneration();
        frame->m_width = context.width;
        frame->m_height = context.height;
        frame->m_occlusionLevelCount = GpuGeometryOcclusion::LevelCount(context.width, context.height);
    }

    RHIBufferDesc description;
    description.bytes = std::uint64_t(frame->m_outputCount) * sizeof(std::uint32_t);
    description.allowUnorderedAccess = true;
    description.debugName = L"Geometry.Visibility.IDs";
    if (!m_device->CreateBuffer(description, frame->m_visibleIds, error))
    {
        return false;
    }
    description.bytes = bins.size() * sizeof(RHIDrawIndexedIndirectArguments);
    description.allowIndirectArguments = true;
    description.debugName = L"Geometry.Visibility.Arguments";
    if (!m_device->CreateBuffer(description, frame->m_arguments, error))
    {
        return false;
    }

    auto constants = VisibilityConstants(viewProjection, frame->m_candidateCount, frame->m_binCount);
    constants.width = frame->m_width;
    constants.height = frame->m_height;
    constants.occlusionLevels = frame->m_occlusionLevelCount;
    if (shadow)
    {
        constants.mode = 1u;
        constants.planeMask = 0u;
        constants.projectionValid = 0u;
        constants.shadowReceiverSphere = shadow->receiverSphere;
        constants.shadowLightDirection = shadow->lightDirection;
    }
    const RHIUploadRequest requests[]{
        {(std::max)(std::size_t{1}, candidates.size()) * sizeof(Candidate), RHIUploadUsage::Raw, 256},
        {bins.size_bytes(), RHIUploadUsage::Raw, 256},
        {sizeof(constants), RHIUploadUsage::ConstantBuffer, 256}};
    std::array<RHIBufferSlice, 3> uploads;
    if (!m_device->ReserveUploadBatch(requests, uploads, error))
    {
        return false;
    }
    for (const auto& upload : uploads)
    {
        if (!upload.IsWritable() || !upload.IsValid())
        {
            return VisibilityFail(error, "GPU visibility upload reservation is not writable.");
        }
    }
    frame->m_candidates = uploads[0];
    frame->m_bins = uploads[1];
    frame->m_constants = uploads[2];
    if (!candidates.empty())
    {
        std::memcpy(frame->m_candidates.cpuAddress, candidates.data(), candidates.size_bytes());
    }
    else
    {
        std::memset(frame->m_candidates.cpuAddress, 0, sizeof(Candidate));
    }
    std::memcpy(frame->m_bins.cpuAddress, bins.data(), bins.size_bytes());
    std::memcpy(frame->m_constants.cpuAddress, &constants, sizeof(constants));
    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(frame->m_visibleIds, frame->m_outputCount, sizeof(std::uint32_t)),
        RHIBindingDesc::UavBuffer(frame->m_arguments, frame->m_binCount, sizeof(RHIDrawIndexedIndirectArguments))};
    frame->m_outputs = m_device->CreateBindings(outputs);
    if (!frame->m_outputs.IsValid() || frame->m_recording != m_device->GetCurrentUploadRecordingId())
    {
        return VisibilityFail(error, "GPU visibility requires complete bindings in the same upload recording.");
    }
    frame->m_descriptors = m_device->GetDescriptorVersionToken();
    frame->m_occlusionView = GpuGeometryOcclusion::CaptureView(context, viewProjection);
    if (currentFrameOcclusion && !m_depthPyramid.Prepare(context, viewProjection, frame->m_preparedPyramid, error))
    {
        return false;
    }
    frame->m_self = frame;
    {
        std::lock_guard lock(m_recordingMutex);
        m_recordings[frame->m_recording].owners.push_back(frame);
    }
    result = std::move(frame);
    error.clear();
    return true;
}

RHIBufferSlice GpuGeometryVisibility::UploadIdentity(const EnhancedFrameContext& context, std::uint32_t count,
                                                    std::string& error)
{
    if (!context.resources || !context.resources->GetCurrentUploadRecordingId() || !count)
    {
        VisibilityFail(error, "Visibility identity IDs require a nonempty active upload recording.");
        return {};
    }
    const auto recording = context.resources->GetCurrentUploadRecordingId();
    auto identity = context.resources->AllocateUpload({std::uint64_t(count) * sizeof(std::uint32_t),
                                                       RHIUploadUsage::Raw, 256});
    if (!identity.IsWritable() || !identity.IsValid() ||
        recording != context.resources->GetCurrentUploadRecordingId())
    {
        VisibilityFail(error, "Visibility identity ID upload failed.");
        return {};
    }
    auto* ids = static_cast<std::uint32_t*>(identity.cpuAddress);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        ids[index] = index;
    }
    error.clear();
    return identity;
}

void GpuGeometryVisibility::Frame::CheckCurrent(const EnhancedRenderGraph* graph) const
{
    if (!m_device || m_device->GetCurrentUploadRecordingId() != m_recording ||
        m_device->GetDescriptorVersionToken() != m_descriptors ||
        (graph && (m_graph != graph || m_graphEpoch != graph->ResourceEpoch() ||
                   &graph->DeviceServices() != m_device)))
    {
        throw std::runtime_error("GPU visibility frame escaped its recording, descriptors or graph epoch.");
    }
}

void GpuGeometryVisibility::Frame::Dispatch(RHIEncoder& encoder, bool reset, RHIBindingTable depth) const
{
    const auto count = reset ? m_binCount : m_candidateCount;
    if (!count)
    {
        return;
    }
    const auto groups = static_cast<std::uint32_t>((std::uint64_t(count) + kVisibilityThreads - 1) /
                                                   kVisibilityThreads);
    const auto& pipeline = reset ? m_reset : (depth.IsValid() ? m_occlusionCull : m_cull);
    encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, m_candidates);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, m_bins);
    encoder.SetBindings(RHIBindPoint::Compute, 3, m_outputs);
    if (!reset && depth.IsValid())
    {
        encoder.SetBindings(RHIBindPoint::Compute, 4, depth);
    }
    encoder.Dispatch((std::min)(groups, kVisibilityDispatchWidth),
                     (groups + kVisibilityDispatchWidth - 1) / kVisibilityDispatchWidth, 1);
}

void GpuGeometryVisibility::Frame::DeclareWithOcclusion(EnhancedRenderGraph& graph, RGHandle occluderDepth) const
{
    if (!m_preparedPyramid)
    {
        throw std::runtime_error("GPU visibility was not prepared for current-depth occlusion.");
    }
    m_preparedPyramid->Declare(graph, occluderDepth);
    DeclareWithOcclusion(graph, m_preparedPyramid);
}

void GpuGeometryVisibility::Frame::DeclareWithOcclusion(EnhancedRenderGraph& graph,
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> pyramid) const
{
    CheckCurrent();
    if (m_graph || !m_occlusionCull || !pyramid ||
        pyramid->LevelCount() != m_occlusionLevelCount ||
        graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
    {
        throw std::runtime_error("GPU visibility requires an undeclared occlusion-capable frame and matching pyramid.");
    }
    pyramid->RequireView(m_occlusionView);
    m_pyramid = std::move(pyramid);
    Declare(graph);
}

void GpuGeometryVisibility::Frame::Declare(EnhancedRenderGraph& graph) const
{
    CheckCurrent();
    if (m_graph)
    {
        CheckCurrent(&graph);
        return;
    }
    const auto owner = m_self.lock();
    if (!owner)
    {
        throw std::runtime_error("GPU visibility declaration requires a retained frame owner.");
    }
    m_graph = &graph;
    m_graphEpoch = graph.ResourceEpoch();
    m_graphVisibleIds = graph.ImportBuffer(m_visibleIds, RHIResourceState::Common, "Geometry.Visibility.IDs");
    m_graphArguments = graph.ImportBuffer(m_arguments, RHIResourceState::Common, "Geometry.Visibility.Arguments");

    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitSingleWriter)
    {
        // RG1 cannot express two writes to one resource. Keep that compatibility
        // mode as one producer; the product RG2 path below declares both versions.
        graph.AddPass("Geometry.Visibility",
                      {{m_graphArguments, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
                       {m_graphVisibleIds, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
                      [owner](const auto& execution) {
                          owner->CheckCurrent(execution.graph);
                          owner->Dispatch(*execution.encoder, true);
                          const RHIBufferHandle barrier[]{owner->m_arguments};
                          execution.encoder->ResourceBarriers({{}, {}, {}, barrier});
                          owner->Dispatch(*execution.encoder, false);
                      });
        return;
    }

    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    if (versioned)
    {
        m_graphArguments = graph.Write(m_graphArguments);
    }
    const auto resetPass = graph.AddPass("Geometry.Visibility.Reset",
                  {{m_graphArguments, RHIResourceState::UnorderedAccess,
                    versioned ? RGAccessMode::Write : RGAccessMode::LegacyState}},
                  [owner](const auto& execution) {
                      owner->CheckCurrent(execution.graph);
                      owner->Dispatch(*execution.encoder, true);
                  });
    if (versioned)
    {
        m_graphArguments = graph.Modify(m_graphArguments);
        m_graphVisibleIds = graph.Write(m_graphVisibleIds);
    }
    std::vector<EnhancedRenderGraph::RGPassUsage> usages{
        {m_graphArguments, RHIResourceState::UnorderedAccess,
         versioned ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState},
        {m_graphVisibleIds, RHIResourceState::UnorderedAccess,
         versioned ? RGAccessMode::Write : RGAccessMode::LegacyState}};
    if (m_pyramid)
    {
        m_pyramid->AddReadUsages(graph, usages);
    }
    const auto cullPass = graph.AddPass(
        m_pyramid ? "Geometry.Visibility.CullOcclusion" : "Geometry.Visibility.Cull", usages,
                  [owner](const auto& execution) {
                      owner->CheckCurrent(execution.graph);
                      RHIBindingTable depth;
                      if (owner->m_pyramid)
                      {
                          depth = owner->m_pyramid->Bindings(execution);
                      }
                      owner->Dispatch(*execution.encoder, false, depth);
                  });
    // Candidates, bins and constants are sealed uploads. All mutable GPU
    // outputs and optional pyramid reads are represented by the usages above.
    graph.DeclareComputeCompatible(resetPass);
    graph.DeclareComputeCompatible(cullPass);
}

void GpuGeometryVisibility::Frame::AddReadUsages(
    EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    Declare(graph);
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
                            ? RGAccessMode::LegacyState
                            : RGAccessMode::Read;
    for (const auto& usage : {EnhancedRenderGraph::RGPassUsage{m_graphArguments, RHIResourceState::IndirectArgument,
                                                              access},
                             EnhancedRenderGraph::RGPassUsage{m_graphVisibleIds, RHIResourceState::ShaderResource,
                                                              access}})
    {
        const auto duplicate = std::ranges::find_if(usages, [&](const auto& existing) {
            return existing.handle.index == usage.handle.index && existing.handle.version == usage.handle.version &&
                   existing.handle.kind == usage.handle.kind && existing.handle.epoch == usage.handle.epoch;
        });
        if (duplicate == usages.end())
        {
            usages.push_back(usage);
        }
        else if (duplicate->state != usage.state || duplicate->access != usage.access)
        {
            throw std::runtime_error("GPU visibility consumer has conflicting resource usages.");
        }
    }
}

RHIBufferSlice GpuGeometryVisibility::Frame::VisibleIds(std::uint32_t outputOffset, std::uint32_t count) const
{
    if (!count || outputOffset % kOutputAlignment != 0 ||
        std::uint64_t(outputOffset) + count > m_outputCount)
    {
        throw std::out_of_range("GPU visibility ID slice is outside its aligned output range.");
    }
    RHIBufferSlice result;
    result.buffer = m_visibleIds;
    result.offset = std::uint64_t(outputOffset) * sizeof(std::uint32_t);
    result.size = std::uint64_t(count) * sizeof(std::uint32_t);
    return result;
}

std::uint64_t GpuGeometryVisibility::Frame::ArgsOffset(std::uint32_t bin) const
{
    if (bin >= m_binCount)
    {
        throw std::out_of_range("GPU visibility argument bin is outside the prepared frame.");
    }
    return std::uint64_t(bin) * sizeof(RHIDrawIndexedIndirectArguments);
}

void GpuGeometryVisibility::ShutdownAfterIdle()
{
    if (m_device)
    {
        m_device->UnregisterUploadTransactionListener(this);
    }
    {
        std::lock_guard lock(m_recordingMutex);
        m_recordings.clear();
        m_completed = 0;
    }
    m_reset = {};
    m_cull = {};
    m_occlusionCull = {};
    m_depthPyramid.ShutdownAfterIdle();
    m_device = nullptr;
}

void GpuGeometryVisibility::OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end())
    {
        auto& entry = found->second;
        entry.submitted = true;
        entry.completion = (std::max)(entry.completion, completion.value);
        entry.quarantined = entry.quarantined || !completion.IsValid();
        if (!entry.quarantined && entry.completion <= m_completed)
        {
            m_recordings.erase(found);
        }
    }
}

void GpuGeometryVisibility::OnUploadCompleted(std::uint64_t completed)
{
    std::lock_guard lock(m_recordingMutex);
    m_completed = (std::max)(m_completed, completed);
    std::erase_if(m_recordings, [&](const auto& recording) {
        const auto& entry = recording.second;
        return entry.submitted && !entry.quarantined && entry.completion && entry.completion <= m_completed;
    });
}

void GpuGeometryVisibility::OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end() &&
        (!found->second.submitted || found->second.completion == completion.value))
    {
        m_recordings.erase(found);
    }
}

void GpuGeometryVisibility::OnUploadAborted(std::uint64_t recording)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end() && !found->second.submitted)
    {
        m_recordings.erase(found);
    }
}
