#include "GpuMeshletVisibility.h"

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
    constexpr std::uint32_t kMeshletVisibilityThreads = 64;
    constexpr std::uint32_t kMeshletVisibilityProfile = 1;
    constexpr std::uint32_t kMeshletVisibilityDescriptorBytes = 80;
    static_assert(GpuMeshletVisibility::kMaximumLods == 8);
    static_assert(GpuGeometryOcclusion::kMaximumLevels == 16); // Recheck SRVs begin at t18.

    struct MeshletVisibilityWorld
    {
        // Explicit columns avoid a C++/Slang matrix-major packing assumption.
        math::vector4 columns[4]{};
    };
    static_assert(sizeof(MeshletVisibilityWorld) == 64);

    struct MeshletVisibilityConstants
    {
        math::vector4 planes[6]{};
        std::uint32_t meshletCount{}, instanceCount{}, candidateCount{}, planeMask{};
        math::vector4 projectionColumns[4]{};
        math::vector4 localSphere{};
        float lodErrors[8]{}; // Slang float4[2], not a scalar cbuffer array.
        std::uint32_t width{}, height{}, lodCount{1}, lodIndex{};
        float maxPixelError{1.0f};
        std::uint32_t projectionValid{}, occlusionLevels{}, padding{};
    };
    static_assert(sizeof(MeshletVisibilityConstants) == 256);
    static_assert(offsetof(MeshletVisibilityConstants, projectionColumns) == 112);
    static_assert(offsetof(MeshletVisibilityConstants, lodErrors) == 192);
    static_assert(offsetof(MeshletVisibilityConstants, width) == 224);
    static_assert(offsetof(MeshletVisibilityConstants, maxPixelError) == 240);
    static_assert(offsetof(MeshletVisibilityConstants, occlusionLevels) == 248);

    bool MeshletVisibilityFail(std::string& error, std::string message)
    {
        error = std::move(message);
        return false;
    }

    MeshletVisibilityConstants MakeMeshletVisibilityConstants(const math::matrix4x4& matrix,
        std::uint32_t meshlets, std::uint32_t instances, std::uint32_t candidates)
    {
        MeshletVisibilityConstants constants;
        constants.meshletCount = meshlets;
        constants.instanceCount = instances;
        constants.candidateCount = candidates;
        for (const auto& row : matrix.m)
        {
            for (float value : row)
            {
                if (!std::isfinite(value))
                {
                    return constants;
                }
            }
        }
        for (unsigned column = 0; column < 4; ++column)
        {
            constants.projectionColumns[column] = {matrix.m[0][column], matrix.m[1][column],
                matrix.m[2][column], matrix.m[3][column]};
        }
        constants.projectionValid = 1u;
        // Native row-vector view projection, zero-to-one clip depth.
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
}

GpuMeshletVisibility::~GpuMeshletVisibility()
{
    ShutdownAfterIdle();
}

GpuMeshletVisibility::Frame::~Frame()
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

bool GpuMeshletVisibility::Initialize(const EnhancedFrameContext& context, std::string& error)
{
    if (m_device)
    {
        return m_device == context.resources ||
            MeshletVisibilityFail(error, "GPU meshlet visibility belongs to another device.");
    }
    const auto source = RHIShaderSource::Resolve("GeometryMeshletVisibility.slang").string();
    LX::Runtime::CompiledCompute reset, cull;
    RHIShaderCompileOptions options;
    options.strictMath = true;
    if (!LX::Runtime::CompileCompute(source, "GeometryMeshletResetCS", {}, options, reset, error) ||
        !LX::Runtime::CompileCompute(source, "GeometryMeshletCullCS", {}, options, cull, error))
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

bool GpuMeshletVisibility::InitializeOcclusion(const EnhancedFrameContext& context, std::string& error)
{
    if (m_occlusionCull.GetGeneration() && m_occlusionRecheck.GetGeneration())
    {
        return true;
    }
    const auto build = [&](bool recheck, LX::Runtime::ComputePipeline& pipeline) {
        RHIShaderCompileOptions options;
        options.strictMath = true;
        RHIShaderPermutation permutation;
        LX::Runtime::CompiledCompute cull;
        const auto source = RHIShaderSource::Resolve("GeometryMeshletVisibility.slang").string();
        if (!permutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error) ||
            (recheck && !permutation.Enable("GEOMETRY_MESHLET_RECHECK", error)) ||
            !LX::Runtime::CompileCompute(source, "GeometryMeshletCullCS", permutation, options, cull, error))
        {
            return false;
        }
        std::vector<RHIPipelineLayoutParam> parameters{RHILayout::Cbv(0), RHILayout::Srv(0),
            RHILayout::Srv(1), RHILayout::UavBufferTable(2, 0),
            RHILayout::SrvTable(GpuGeometryOcclusion::kMaximumLevels, 2)};
        if (recheck)
        {
            parameters.push_back(RHILayout::Srv(18));
            parameters.push_back(RHILayout::Srv(19));
        }
        const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIComputePipelineDesc description;
        description.layout = layout;
        description.csBytecode = cull.stage.bytecode.Data();
        description.csSize = cull.stage.bytecode.Size();
        return pipeline.Create(*context.psoManager, description, std::move(cull.description), error);
    };
    LX::Runtime::ComputePipeline cull, recheck;
    if (!build(false, cull) || !build(true, recheck))
    {
        return false;
    }
    m_occlusionCull = std::move(cull);
    m_occlusionRecheck = std::move(recheck);
    return true;
}

bool GpuMeshletVisibility::PrepareOcclusionPipelines(const EnhancedFrameContext& context, std::string& error)
{
    if (!context.resources || !context.rootSignatures || !context.psoManager ||
        !context.resources->GetCurrentUploadRecordingId() || !GpuGeometryOcclusion::LevelCount(context.width, context.height))
    {
        return MeshletVisibilityFail(error, "GPU meshlet occlusion preflight requires an active supported view.");
    }
    return Initialize(context, error) && InitializeOcclusion(context, error);
}

bool GpuMeshletVisibility::PrepareOcclusionRecheck(const EnhancedFrameContext& context,
    std::shared_ptr<const Frame> prior, std::shared_ptr<const Frame>& result, std::string& error)
{
    result.reset();
    if (!prior || !context.resources || prior->m_device != context.resources || prior->m_source ||
        prior->m_recording != context.resources->GetCurrentUploadRecordingId() ||
        prior->m_descriptors != context.resources->GetDescriptorVersionToken() ||
        prior->m_occlusionView.frameId != context.frameId || prior->m_occlusionView.sceneEpoch != context.sceneEpoch ||
        prior->m_occlusionView.width != context.width || prior->m_occlusionView.height != context.height ||
        !prior->m_constants.cpuAddress)
    {
        return MeshletVisibilityFail(error, "Meshlet occlusion recheck requires an original frame from this sealed view.");
    }
    if (!PrepareOcclusionPipelines(context, error))
    {
        return true;
    }
    auto frame = std::shared_ptr<Frame>(new Frame);
    frame->m_device = m_device;
    frame->m_recording = prior->m_recording;
    frame->m_descriptors = prior->m_descriptors;
    frame->m_occlusionView = prior->m_occlusionView;
    frame->m_candidateCount = prior->m_candidateCount;
    frame->m_worlds = prior->m_worlds;
    frame->m_meshlets = prior->m_meshlets;
    frame->m_reset = prior->m_reset;
    frame->m_cull = prior->m_cull;
    frame->m_occlusionCull = m_occlusionRecheck.GetGeneration();
    frame->m_source = std::move(prior);

    RHIBufferDesc description;
    description.bytes = std::uint64_t(frame->m_candidateCount) * sizeof(std::uint32_t) * 2u;
    description.allowUnorderedAccess = true;
    description.debugName = L"Geometry.MeshletRecheck.Pairs";
    if (!m_device->CreateBuffer(description, frame->m_visibleIds, error))
    {
        return false;
    }
    description.bytes = sizeof(RHIDispatchMeshIndirectArguments);
    description.allowIndirectArguments = true;
    description.debugName = L"Geometry.MeshletRecheck.Arguments";
    if (!m_device->CreateBuffer(description, frame->m_arguments, error))
    {
        return false;
    }
    MeshletVisibilityConstants constants;
    std::memcpy(&constants, frame->m_source->m_constants.cpuAddress, sizeof(constants));
    constants.occlusionLevels = GpuGeometryOcclusion::LevelCount(context.width, context.height);
    frame->m_constants = m_device->UploadConstants(&constants, sizeof(constants));
    if (!frame->m_constants.IsValid())
    {
        return MeshletVisibilityFail(error, "Meshlet occlusion recheck constant upload failed.");
    }
    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(frame->m_visibleIds, frame->m_candidateCount, sizeof(std::uint32_t) * 2u),
        RHIBindingDesc::UavBuffer(frame->m_arguments, 1, sizeof(RHIDispatchMeshIndirectArguments))};
    frame->m_outputs = m_device->CreateBindings(outputs);
    if (!frame->m_outputs.IsValid() || frame->m_recording != m_device->GetCurrentUploadRecordingId() ||
        frame->m_descriptors != m_device->GetDescriptorVersionToken())
    {
        return MeshletVisibilityFail(error, "Meshlet occlusion recheck escaped its upload recording/descriptors.");
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

bool GpuMeshletVisibility::Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
    const RHIMeshletBinding& meshlets, std::span<const math::matrix4x4> worlds,
    std::shared_ptr<const Frame>& result, std::string& error, bool currentFrameOcclusion)
{
    return PrepareLod(context, viewProjection, meshlets, worlds, {}, currentFrameOcclusion, result, error);
}

bool GpuMeshletVisibility::PrepareLods(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
    std::span<const RHIMeshletBinding> lodBindings, std::span<const float> geometricErrors,
    const math::vector4& localSphere, std::span<const math::matrix4x4> worlds,
    std::vector<std::shared_ptr<const Frame>>& result, std::string& error, float maxPixelError,
    bool currentFrameOcclusion)
{
    result.clear();
    if (!context.resources || (m_device && m_device != context.resources))
    {
        return MeshletVisibilityFail(error, "GPU meshlet LODs require their current device services.");
    }
    if (lodBindings.empty() || lodBindings.size() > kMaximumLods || lodBindings.size() != geometricErrors.size() ||
        !std::isfinite(maxPixelError) || maxPixelError <= 0.0f)
    {
        error = "GPU meshlet LOD table is unavailable; retain LOD0.";
        return true;
    }
    LodSelection selection;
    selection.localSphere = localSphere;
    selection.count = static_cast<std::uint32_t>(lodBindings.size());
    selection.maxPixelError = maxPixelError;
    const auto caps = context.resources->GetMeshShaderCapabilities();
    float previousError = 0.0f;
    for (std::size_t level = 0; level < lodBindings.size(); ++level)
    {
        const auto& binding = lodBindings[level];
        const auto geometricError = geometricErrors[level];
        const auto capacity = std::uint64_t(worlds.size()) * binding.meshletCount;
        if (!std::isfinite(geometricError) || geometricError < previousError ||
            (level == 0u && geometricError != 0.0f) || !binding.IsValid() ||
            binding.profileVersion != kMeshletVisibilityProfile || worlds.size() > kMaximumCandidates ||
            binding.meshletCount > kMaximumCandidates || capacity > kMaximumCandidates ||
            !caps.meshShader || !caps.meshIndirect ||
            !caps.SupportsDispatch(static_cast<std::uint32_t>(capacity), 1u, 1u))
        {
            error = "GPU meshlet LOD table exceeds supported geometry/error/capacity bounds; retain LOD0.";
            return true;
        }
        selection.errors[level] = geometricError;
        previousError = geometricError;
    }
    std::vector<std::shared_ptr<const Frame>> prepared;
    prepared.reserve(lodBindings.size());
    for (std::size_t level = 0; level < lodBindings.size(); ++level)
    {
        selection.index = static_cast<std::uint32_t>(level);
        std::shared_ptr<const Frame> frame;
        if (!PrepareLod(context, viewProjection, lodBindings[level], worlds, selection, currentFrameOcclusion, frame, error))
        {
            return false;
        }
        if (!frame)
        {
            // No partial result escapes. Earlier prepared owners are retained by
            // the normal recording lifecycle but no GPU work is declared for them.
            return true;
        }
        prepared.push_back(std::move(frame));
    }
    result = std::move(prepared);
    error.clear();
    return true;
}

bool GpuMeshletVisibility::PrepareLod(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
    const RHIMeshletBinding& meshlets, std::span<const math::matrix4x4> worlds,
    const LodSelection& lod, bool currentFrameOcclusion, std::shared_ptr<const Frame>& result, std::string& error)
{
    result.reset();
    if (!context.resources || (m_device && m_device != context.resources))
    {
        return MeshletVisibilityFail(error, "GPU meshlet visibility requires its current device services.");
    }
    const auto caps = context.resources->GetMeshShaderCapabilities();
    if (worlds.empty() || !meshlets.IsValid() || meshlets.profileVersion != kMeshletVisibilityProfile ||
        !caps.meshShader || !caps.meshIndirect || worlds.size() > kMaximumCandidates ||
        meshlets.meshletCount > kMaximumCandidates)
    {
        error.clear();
        return true;
    }
    const auto capacity = std::uint64_t(worlds.size()) * meshlets.meshletCount;
    if (capacity > kMaximumCandidates || !caps.SupportsDispatch(static_cast<std::uint32_t>(capacity), 1, 1))
    {
        error.clear();
        return true;
    }
    const auto minimumBytes = sizeof(RHIMeshletBufferHeader) +
        std::uint64_t(meshlets.meshletCount) * kMeshletVisibilityDescriptorBytes;
    if (meshlets.data.offset % 256 != 0 || meshlets.data.size < minimumBytes)
    {
        return MeshletVisibilityFail(error, "GPU meshlet data has an unaligned or incomplete packed slice.");
    }
    if (!context.rootSignatures || !context.psoManager || !context.resources->GetCurrentUploadRecordingId())
    {
        return MeshletVisibilityFail(error, "GPU meshlet visibility requires pipeline services and an active recording.");
    }
    for (const auto& world : worlds)
    {
        bool affine = world.m[0][3] == 0.f && world.m[1][3] == 0.f &&
            world.m[2][3] == 0.f && world.m[3][3] == 1.f;
        for (const auto& row : world.m)
        {
            for (float value : row)
            {
                affine = affine && std::isfinite(value);
            }
        }
        if (!affine)
        {
            error.clear();
            return true;
        }
    }
    if (!Initialize(context, error))
    {
        // Shader/compiler/PSO prerequisites may be unavailable even on a device
        // advertising mesh support. Keep the indexed route before allocating or
        // declaring mesh work, retaining the diagnostic for the caller.
        return true;
    }
    if (currentFrameOcclusion && (!GpuGeometryOcclusion::LevelCount(context.width, context.height) ||
        !InitializeOcclusion(context, error)))
    {
        return true;
    }

    auto frame = std::shared_ptr<Frame>(new Frame);
    frame->m_device = m_device;
    frame->m_recording = m_device->GetCurrentUploadRecordingId();
    frame->m_candidateCount = static_cast<std::uint32_t>(capacity);
    frame->m_meshlets = meshlets;
    frame->m_reset = m_reset.GetGeneration();
    frame->m_cull = m_cull.GetGeneration();
    if (currentFrameOcclusion)
    {
        frame->m_occlusionCull = m_occlusionCull.GetGeneration();
    }

    RHIBufferDesc description;
    description.bytes = capacity * sizeof(std::uint32_t) * 2;
    description.allowUnorderedAccess = true;
    description.debugName = L"Geometry.MeshletVisibility.Pairs";
    if (!m_device->CreateBuffer(description, frame->m_visibleIds, error))
    {
        return false;
    }
    description.bytes = sizeof(RHIDispatchMeshIndirectArguments);
    description.allowIndirectArguments = true;
    description.debugName = L"Geometry.MeshletVisibility.Arguments";
    if (!m_device->CreateBuffer(description, frame->m_arguments, error))
    {
        return false;
    }
    auto constants = MakeMeshletVisibilityConstants(viewProjection, meshlets.meshletCount,
        static_cast<std::uint32_t>(worlds.size()), frame->m_candidateCount);
    constants.localSphere = lod.localSphere;
    std::copy(lod.errors.begin(), lod.errors.end(), constants.lodErrors);
    constants.width = context.width;
    constants.height = context.height;
    constants.lodCount = lod.count;
    constants.lodIndex = lod.index;
    constants.maxPixelError = lod.maxPixelError;
    constants.occlusionLevels = currentFrameOcclusion ? GpuGeometryOcclusion::LevelCount(context.width, context.height) : 0u;
    const RHIUploadRequest requests[]{
        {worlds.size() * sizeof(MeshletVisibilityWorld), RHIUploadUsage::Raw, 256},
        {sizeof(constants), RHIUploadUsage::ConstantBuffer, 256}};
    std::array<RHIBufferSlice, 2> uploads;
    if (!m_device->ReserveUploadBatch(requests, uploads, error))
    {
        return false;
    }
    for (const auto& upload : uploads)
    {
        if (!upload.IsValid() || !upload.IsWritable())
        {
            return MeshletVisibilityFail(error, "GPU meshlet visibility upload reservation is not writable.");
        }
    }
    frame->m_worlds = uploads[0];
    frame->m_constants = uploads[1];
    for (std::size_t index = 0; index < worlds.size(); ++index)
    {
        MeshletVisibilityWorld packed;
        for (unsigned column = 0; column < 4; ++column)
        {
            packed.columns[column] = {worlds[index].m[0][column], worlds[index].m[1][column],
                worlds[index].m[2][column], worlds[index].m[3][column]};
        }
        std::memcpy(static_cast<std::byte*>(frame->m_worlds.cpuAddress) + index * sizeof(packed),
            &packed, sizeof(packed));
    }
    std::memcpy(frame->m_constants.cpuAddress, &constants, sizeof(constants));
    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(frame->m_visibleIds, frame->m_candidateCount, sizeof(std::uint32_t) * 2),
        RHIBindingDesc::UavBuffer(frame->m_arguments, 1, sizeof(RHIDispatchMeshIndirectArguments))};
    frame->m_outputs = m_device->CreateBindings(outputs);
    if (!frame->m_outputs.IsValid() || frame->m_recording != m_device->GetCurrentUploadRecordingId())
    {
        return MeshletVisibilityFail(error, "GPU meshlet visibility escaped its upload recording.");
    }
    frame->m_descriptors = m_device->GetDescriptorVersionToken();
    frame->m_occlusionView = GpuGeometryOcclusion::CaptureView(context, viewProjection);
    frame->m_self = frame;
    {
        std::lock_guard lock(m_recordingMutex);
        m_recordings[frame->m_recording].owners.push_back(frame);
    }
    result = std::move(frame);
    error.clear();
    return true;
}

void GpuMeshletVisibility::Frame::CheckCurrent(const EnhancedRenderGraph* graph) const
{
    if (!m_device || m_device->GetCurrentUploadRecordingId() != m_recording ||
        m_device->GetDescriptorVersionToken() != m_descriptors ||
        (graph && (m_graph != graph || m_graphEpoch != graph->ResourceEpoch() ||
            &graph->DeviceServices() != m_device)))
    {
        throw std::runtime_error("GPU meshlet visibility escaped its recording, descriptors or graph epoch.");
    }
}

void GpuMeshletVisibility::Frame::Dispatch(RHIEncoder& encoder, bool reset, RHIBindingTable depth) const
{
    const auto& pipeline = reset ? m_reset : (depth.IsValid() ? m_occlusionCull : m_cull);
    encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, m_worlds);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, m_meshlets.data);
    encoder.SetBindings(RHIBindPoint::Compute, 3, m_outputs);
    if (!reset && depth.IsValid())
    {
        encoder.SetBindings(RHIBindPoint::Compute, 4, depth);
    }
    if (!reset && m_source)
    {
        encoder.SetRootBuffer(RHIBindPoint::Compute, 5, m_source->VisibleIds());
        RHIBufferSlice count;
        count.buffer = m_source->m_arguments;
        count.size = sizeof(RHIDispatchMeshIndirectArguments);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 6, count);
    }
    encoder.Dispatch(reset ? 1u : (m_candidateCount + kMeshletVisibilityThreads - 1u) / kMeshletVisibilityThreads, 1, 1);
}

void GpuMeshletVisibility::Frame::DeclareWithOcclusion(EnhancedRenderGraph& graph,
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> pyramid) const
{
    CheckCurrent();
    if (m_graph || !m_occlusionCull || !pyramid || graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
    {
        throw std::runtime_error("GPU meshlet occlusion requires its undeclared prepared frame and a current-depth pyramid.");
    }
    pyramid->RequireView(m_occlusionView);
    m_pyramid = std::move(pyramid);
    Declare(graph);
}

void GpuMeshletVisibility::Frame::Declare(EnhancedRenderGraph& graph) const
{
    CheckCurrent();
    if (m_source && !m_pyramid)
    {
        throw std::runtime_error("Meshlet occlusion recheck requires the shared current-depth pyramid.");
    }
    if (m_graph)
    {
        CheckCurrent(&graph);
        return;
    }
    if (&graph.DeviceServices() != m_device)
    {
        throw std::runtime_error("GPU meshlet visibility graph belongs to another device.");
    }
    if (m_source)
    {
        m_source->Declare(graph);
    }
    const auto owner = m_self.lock();
    if (!owner)
    {
        throw std::runtime_error("GPU meshlet declaration requires a retained frame owner.");
    }
    m_graph = &graph;
    m_graphEpoch = graph.ResourceEpoch();
    m_graphMeshlets = graph.FindImportedBuffer(m_meshlets.data.buffer);
    if (!m_graphMeshlets.IsValid())
    {
        m_graphMeshlets = graph.ImportBuffer(m_meshlets.data.buffer, RHIResourceState::ShaderResource,
            "Geometry.MeshletData");
    }
    m_graphVisibleIds = graph.ImportBuffer(m_visibleIds, RHIResourceState::Common, "Geometry.MeshletVisibility.Pairs");
    m_graphArguments = graph.ImportBuffer(m_arguments, RHIResourceState::Common, "Geometry.MeshletVisibility.Arguments");
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitSingleWriter)
    {
        graph.AddPass("Geometry.MeshletVisibility",
            {{m_graphMeshlets, RHIResourceState::ShaderResource, RGAccessMode::Read},
             {m_graphArguments, RHIResourceState::UnorderedAccess, RGAccessMode::Write},
             {m_graphVisibleIds, RHIResourceState::UnorderedAccess, RGAccessMode::Write}},
            [owner](const auto& execution) {
                owner->CheckCurrent(execution.graph);
                owner->Dispatch(*execution.encoder, true);
                const RHIBufferHandle barriers[]{owner->m_arguments};
                execution.encoder->ResourceBarriers({{}, {}, {}, barriers});
                owner->Dispatch(*execution.encoder, false);
            });
        return;
    }
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    if (versioned)
    {
        m_graphArguments = graph.Write(m_graphArguments);
    }
    graph.AddPass("Geometry.MeshletVisibility.Reset",
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
        {m_graphMeshlets, RHIResourceState::ShaderResource,
            versioned ? RGAccessMode::Read : RGAccessMode::LegacyState},
         {m_graphArguments, RHIResourceState::UnorderedAccess,
            versioned ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState},
         {m_graphVisibleIds, RHIResourceState::UnorderedAccess,
            versioned ? RGAccessMode::Write : RGAccessMode::LegacyState}};
    if (m_pyramid)
    {
        m_pyramid->AddReadUsages(graph, usages);
    }
    if (m_source)
    {
        // Pyramid depends on depth rasterization, which consumed the original
        // arguments as IndirectArgument. These explicit reads order its later
        // transition to ShaderResource; source pairs/count are never modified.
        usages.push_back({m_source->m_graphVisibleIds, RHIResourceState::ShaderResource, RGAccessMode::Read});
        usages.push_back({m_source->m_graphArguments, RHIResourceState::ShaderResource, RGAccessMode::Read});
    }
    graph.AddPass(m_pyramid ? "Geometry.MeshletVisibility.CullOcclusion" : "Geometry.MeshletVisibility.Cull", usages,
        [owner](const auto& execution) {
            owner->CheckCurrent(execution.graph);
            const auto depth = owner->m_pyramid ? owner->m_pyramid->Bindings(execution) : RHIBindingTable{};
            owner->Dispatch(*execution.encoder, false, depth);
        });
}

void GpuMeshletVisibility::Frame::AddReadUsages(EnhancedRenderGraph& graph,
    std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    Declare(graph);
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
        ? RGAccessMode::LegacyState : RGAccessMode::Read;
    for (const auto& usage : {
        EnhancedRenderGraph::RGPassUsage{m_graphArguments, RHIResourceState::IndirectArgument, access},
        EnhancedRenderGraph::RGPassUsage{m_graphVisibleIds, RHIResourceState::ShaderResource, access},
        EnhancedRenderGraph::RGPassUsage{m_graphMeshlets, RHIResourceState::ShaderResource, access}})
    {
        const auto duplicate = std::ranges::find_if(usages, [&](const auto& prior) {
            return prior.handle.index == usage.handle.index && prior.handle.version == usage.handle.version &&
                prior.handle.kind == usage.handle.kind && prior.handle.epoch == usage.handle.epoch;
        });
        if (duplicate == usages.end())
        {
            usages.push_back(usage);
        }
        else if (duplicate->state != usage.state || duplicate->access != usage.access)
        {
            throw std::runtime_error("GPU meshlet consumer has conflicting resource usages.");
        }
    }
}

RHIBufferSlice GpuMeshletVisibility::Frame::VisibleIds() const
{
    CheckCurrent();
    RHIBufferSlice result;
    result.buffer = m_visibleIds;
    result.size = std::uint64_t(m_candidateCount) * sizeof(std::uint32_t) * 2;
    return result;
}

void GpuMeshletVisibility::ShutdownAfterIdle()
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
    m_occlusionRecheck = {};
    m_device = nullptr;
}

void GpuMeshletVisibility::OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion)
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

void GpuMeshletVisibility::OnUploadCompleted(std::uint64_t completed)
{
    std::lock_guard lock(m_recordingMutex);
    m_completed = (std::max)(m_completed, completed);
    std::erase_if(m_recordings, [&](const auto& recording) {
        const auto& entry = recording.second;
        return entry.submitted && !entry.quarantined && entry.completion && entry.completion <= m_completed;
    });
}

void GpuMeshletVisibility::OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end() &&
        (!found->second.submitted || found->second.completion == completion.value))
    {
        m_recordings.erase(found);
    }
}

void GpuMeshletVisibility::OnUploadAborted(std::uint64_t recording)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end() && !found->second.submitted)
    {
        m_recordings.erase(found);
    }
}
