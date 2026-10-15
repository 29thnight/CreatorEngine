#include "GpuGeometryVisibility.h"
#include "MaterialGraphMeshSurface.h"

#include "RHI/RHIShaderSource.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <set>
#include <utility>

namespace
{
    constexpr std::uint32_t kVisibilityThreads = 64;
    constexpr std::uint32_t kVisibilityDispatchWidth = 65535;
    constexpr std::uint32_t kMeshletBinCapacity = 4096;

    std::uint32_t VisibilityGroups(std::uint32_t elements)
    {
        return static_cast<std::uint32_t>((std::uint64_t(elements) + kVisibilityThreads - 1) / kVisibilityThreads);
    }
    void DispatchGroups(RHIEncoder& encoder, std::uint32_t groups)
    {
        encoder.Dispatch((std::min)(groups, kVisibilityDispatchWidth),
            (groups + kVisibilityDispatchWidth - 1) / kVisibilityDispatchWidth, 1);
    }

    struct GeometryVisibilityConstants
    {
        math::vector4 planes[6]{};
        std::uint32_t candidateCount{}, binCount{}, planeMask{}, padding{};
        // Explicit columns avoid depending on Slang's matrix packing convention.
        math::vector4 projectionColumns[4]{};
        std::uint32_t width{}, height{}, occlusionLevels{}, projectionValid{};
    };
    static_assert(sizeof(GeometryVisibilityConstants) == 192);
    static_assert(offsetof(GeometryVisibilityConstants, padding) == 108);
    static_assert(offsetof(GeometryVisibilityConstants, projectionColumns) == 112);
    static_assert(offsetof(GeometryVisibilityConstants, width) == 176);

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

struct GpuGeometryVisibility::BufferPool
{
    struct Entry { RHIBufferHandle handle; std::uint64_t bytes; bool indirect; };
    IRenderDeviceServices& device;
    std::mutex mutex;
    std::vector<Entry> free;
    std::uint64_t cachedBytes{};
    bool closed{};
    explicit BufferPool(IRenderDeviceServices& value) : device(value) { free.reserve(64); }
    ~BufferPool() { Close(); }
    bool Acquire(std::uint64_t bytes, bool indirect, RHIBufferHandle& handle, std::string& error)
    {
        std::lock_guard lock(mutex);
        if (closed) return VisibilityFail(error, "Geometry visibility buffer pool is closed.");
        const auto found = std::ranges::find_if(free, [&](const auto& entry) {
            return entry.bytes == bytes && entry.indirect == indirect;
        });
        if (found != free.end())
        {
            handle = found->handle; cachedBytes -= found->bytes; free.erase(found);
            return true;
        }
        RHIBufferDesc desc;
        desc.bytes = bytes; desc.allowUnorderedAccess = true; desc.allowIndirectArguments = indirect;
        desc.debugName = indirect ? L"Geometry.Visibility.Arguments" : L"Geometry.Visibility.IDs";
        return device.CreateBuffer(desc, handle, error);
    }
    void Return(RHIBufferHandle handle, std::uint64_t bytes, bool indirect, bool reusable)
    {
        if (!handle.IsValid()) return;
        std::lock_guard lock(mutex);
        constexpr std::uint64_t budget = 32ull << 20;
        if (!closed && reusable && free.size() < 64 && bytes <= budget - cachedBytes)
        { free.push_back({handle, bytes, indirect}); cachedBytes += bytes; }
        else device.ReleaseBuffer(handle);
    }
    void Close()
    {
        std::lock_guard lock(mutex);
        closed = true;
        for (const auto& entry : free) device.ReleaseBuffer(entry.handle);
        free.clear(); cachedBytes = 0;
    }
};

GpuGeometryVisibility::~GpuGeometryVisibility()
{
    // Owners destroy render passes only after the device/submission drain, just
    // as for their other persistent render resources.
    ShutdownAfterIdle();
}

GpuGeometryVisibility::Frame::~Frame()
{
    if (m_bufferPool)
    {
        const bool reusable = m_poolReusable.load(std::memory_order_acquire);
        m_bufferPool->Return(m_visibleIds, m_visibleBytes, false, reusable);
        m_bufferPool->Return(m_arguments, m_argumentBytes, true, reusable);
        return;
    }
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
    m_bufferPool = std::make_shared<BufferPool>(*m_device);
    m_device->RegisterUploadTransactionListener(this);
    return true;
}

bool GpuGeometryVisibility::InitializeMeshlets(const EnhancedFrameContext& context, std::string& error)
{
    if (m_meshletCull.GetGeneration()) return true;
    const auto source = RHIShaderSource::Resolve("GeometryVisibility.slang").string();
    RHIShaderCompileOptions options; options.strictMath = true;
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::Srv(0),
        RHILayout::Srv(1), RHILayout::UavBufferTable(2, 0),
        RHILayout::SrvBufferTable(kMeshletBinCapacity, 18),
        RHILayout::SrvBufferTable(kMeshletBinCapacity, 18 + kMeshletBinCapacity)};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
    if (!layout.IsValid()) return false;
    LX::Runtime::ComputePipeline reset, cull;
    const auto compile = [&](const char* entry, LX::Runtime::ComputePipeline& pipeline) {
        LX::Runtime::CompiledCompute shader;
        if (!LX::Runtime::CompileCompute(source, entry, {}, options, shader, error)) return false;
        RHIComputePipelineDesc desc; desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data(); desc.csSize = shader.stage.bytecode.Size();
        return pipeline.Create(*context.psoManager, desc, std::move(shader.description), error);
    };
    LX::Runtime::ComputePipeline finalize;
    if (!compile("ShadowMeshletResetCS", reset) || !compile("ShadowMeshletCullCS", cull) ||
        !compile("MeshletFinalizeCS", finalize)) return false;
    m_meshletReset = std::move(reset); m_meshletCull = std::move(cull);
    m_meshletFinalize = std::move(finalize);
    return true;
}

bool GpuGeometryVisibility::PrepareMeshlets(const EnhancedFrameContext& context,
    const math::matrix4x4& projection,
    std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries,
    std::shared_ptr<const Frame>& result, std::string& error, bool currentFrameOcclusion,
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> history, std::shared_ptr<const Frame> firstStage, bool ordered)
{
    std::vector<Candidate> candidates;
    std::vector<Bin> bins;
    std::uint64_t offset{}, groups{};
    result.reset();
    if (geometries.size() > kMeshletBinCapacity)
        return VisibilityFail(error, "METEOR meshlet work exceeds 4096 bins; no per-bin dispatch route.");
    if (!context.resources) return VisibilityFail(error, "Meshlet work requires its current device.");
    for (const auto& geometry : geometries) {
        if (!geometry || !geometry->MeshletCount() || geometry->Device() != context.resources)
            return VisibilityFail(error, "Shadow meshlet culling requires its nonempty transformed topology.");
        offset = (offset + kOutputAlignment - 1) / kOutputAlignment * kOutputAlignment;
        const auto count = geometry->MeshletCount();
        const auto dispatchX = (std::min)(count, kVisibilityDispatchWidth);
        const auto dispatchY = static_cast<uint32_t>((uint64_t(count) + kVisibilityDispatchWidth - 1) / kVisibilityDispatchWidth);
        if (!context.resources->GetMeshShaderCapabilities().SupportsDispatch(dispatchX, dispatchY, 1))
            return VisibilityFail(error, "Meshlet range exceeds native 2D dispatch limits; split and recook the asset.");
        // Three cascades have independently bounded allocations, never one
        // committed resource per draw. Oversized views fail instead of truncating.
        constexpr std::uint64_t outputBudget = 32ull << 20;
        if (offset + uint64_t(count) * 2 + 1 > UINT32_MAX || candidates.size() + count + 1 > UINT32_MAX ||
            (offset + uint64_t(count) * 2 + 1) * sizeof(std::uint32_t) + (bins.size() + 1) * sizeof(RHIDrawIndexedIndirectArguments) > outputBudget)
            return VisibilityFail(error, "Shadow meshlet output exceeds its 32 MiB cascade budget.");
        const auto bin = static_cast<std::uint32_t>(bins.size());
        // One leading word contains the GPU survivor count; IDs follow it.
        // CPU seals only one range per material/geometry bin. Compute enumerates
        // the source meshlets; no CPU-sized list of individual work items exists.
        // sourceIndex is the first 64-thread group of this range. Padding each
        // range to a group keeps descriptor selection uniform across a wave.
        candidates.push_back({{}, bin, static_cast<uint32_t>(groups), static_cast<uint32_t>(offset),
            8u | (ordered ? kOrderedMeshlets : 0u)});
        groups += (uint64_t(count) + kVisibilityThreads - 1) / kVisibilityThreads;
        bins.push_back({count, static_cast<uint32_t>(offset), 0, 0}); offset += uint64_t(count) * 2 + 1;
    }
    if (history && firstStage) return VisibilityFail(error, "Meshlet stage cannot both read history and recover.");
    if (ordered && (history || firstStage)) return VisibilityFail(error, "Ordered meshlet lists are single-stage.");
    if (!PrepareInternal(context, projection, candidates, bins, result, error,
        currentFrameOcclusion || bool(history) || bool(firstStage), geometries)) return false;
    if (!result) return true;
    auto frame = std::const_pointer_cast<Frame>(result);
    frame->m_ordered = ordered;
    if (history)
    {
        history->RequireView(frame->m_occlusionView);
        if (!history->HistoryOwner()) return VisibilityFail(error, "First stage requires retained history.");
        math::vector4 columns[4];
        const auto& bits = history->HistoryOwner()->source.projectionBits;
        for (unsigned column = 0; column < 4; ++column)
            columns[column] = {std::bit_cast<float>(bits[column]), std::bit_cast<float>(bits[4 + column]),
                std::bit_cast<float>(bits[8 + column]), std::bit_cast<float>(bits[12 + column])};
        frame->m_historyProjection = m_device->UploadConstants(columns, sizeof(columns));
        if (!frame->m_historyProjection.IsValid()) return VisibilityFail(error, "History projection upload failed.");
        frame->m_historyPyramid = std::move(history);
        frame->m_occlusionCull = m_meshletHistoryCull.GetGeneration();
    }
    if (firstStage)
    {
        if (firstStage->m_occlusionView != frame->m_occlusionView ||
            firstStage->m_meshletGeometries != frame->m_meshletGeometries ||
            firstStage->m_meshletOffsets != frame->m_meshletOffsets)
            return VisibilityFail(error, "Meshlet recovery requires the same sealed first-stage geometry and view.");
        frame->m_firstStage = std::move(firstStage);
        frame->m_occlusionCull = m_meshletRecoveryCull.GetGeneration();
    }
    return true;
}

bool GpuGeometryVisibility::InitializeMeshletOcclusion(const EnhancedFrameContext& context, std::string& error)
{
    if (m_meshletOcclusionCull.GetGeneration() && m_meshletHistoryCull.GetGeneration() && m_meshletRecoveryCull.GetGeneration()) return true;
    const auto compile = [&](LX::Runtime::ComputePipeline& pipeline, const char* stage) {
    std::vector<RHIPipelineLayoutParam> parameters{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
        RHILayout::UavBufferTable(2, 0), RHILayout::SrvBufferTable(kMeshletBinCapacity, 18),
        RHILayout::SrvBufferTable(kMeshletBinCapacity, 18 + kMeshletBinCapacity), RHILayout::SrvTable(kOcclusionLevels, 2)};
    if (stage) parameters.push_back(std::string_view(stage) == "GEOMETRY_VISIBILITY_HISTORY" ? RHILayout::Cbv(1) : RHILayout::Srv(8210));
    const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
    if (!layout.IsValid()) return false;
    RHIShaderCompileOptions options; options.strictMath = true;
    RHIShaderPermutation permutation;
    if (stage && !permutation.Enable(stage, error)) return false;
    LX::Runtime::CompiledCompute shader;
    if (!permutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error) ||
        !LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
            "ShadowMeshletCullCS", permutation, options, shader, error)) return false;
    RHIComputePipelineDesc desc; desc.layout = layout;
    desc.csBytecode = shader.stage.bytecode.Data(); desc.csSize = shader.stage.bytecode.Size();
    return pipeline.Create(*context.psoManager, desc, std::move(shader.description), error);
    };
    return compile(m_meshletOcclusionCull, nullptr) && compile(m_meshletHistoryCull, "GEOMETRY_VISIBILITY_HISTORY") &&
        compile(m_meshletRecoveryCull, "GEOMETRY_VISIBILITY_RECOVERY");
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
    return PrepareInternal(context, viewProjection, candidates, bins, result, error, currentFrameOcclusion);
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

bool GpuGeometryVisibility::PrepareInternal(const EnhancedFrameContext& context,
                                          const math::matrix4x4& viewProjection,
                                          std::span<const Candidate> candidates, std::span<const Bin> bins,
                                          std::shared_ptr<const Frame>& result, std::string& error,
                                          bool currentFrameOcclusion,
                                          std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries)
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
        if (!geometries.empty()) range.count = bins[candidate.bin].indexCount * 2 + 1;
        else ++range.count;
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
    if (!geometries.empty() && !InitializeMeshlets(context, error)) return false;
    if (currentFrameOcclusion &&
        (!context.width || !context.height || context.width > (1u << kOcclusionLevels) ||
         context.height > (1u << kOcclusionLevels)))
    {
        return VisibilityFail(error, "GPU occlusion requires a nonempty view within the pyramid extent limit.");
    }
    if (currentFrameOcclusion && !(geometries.empty() ? InitializeOcclusion(context, error) : InitializeMeshletOcclusion(context, error)))
    {
        return false;
    }

    auto frame = std::shared_ptr<Frame>(new Frame);
    frame->m_device = m_device;
    frame->m_recording = m_device->GetCurrentUploadRecordingId();
    frame->m_preparedStats.candidateCount = static_cast<std::uint32_t>(candidates.size());
    if (!geometries.empty()) {
        frame->m_preparedStats.candidateCount = 0;
        for (const auto& geometry : geometries) frame->m_preparedStats.candidateCount += geometry->MeshletCount();
    }
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
    frame->m_meshletGeometries.assign(geometries.begin(), geometries.end());
    frame->m_reset = !geometries.empty() ? m_meshletReset.GetGeneration() : m_reset.GetGeneration();
    frame->m_cull = !geometries.empty() ? m_meshletCull.GetGeneration() : m_cull.GetGeneration();
    if (!geometries.empty()) frame->m_meshletFinalize = m_meshletFinalize.GetGeneration();
    if (currentFrameOcclusion)
    {
        frame->m_occlusionCull = geometries.empty() ? m_occlusionCull.GetGeneration() : m_meshletOcclusionCull.GetGeneration();
        frame->m_width = context.width;
        frame->m_height = context.height;
        frame->m_occlusionLevelCount = GpuGeometryOcclusion::LevelCount(context.width, context.height);
    }

    frame->m_bufferPool = m_bufferPool;
    frame->m_visibleBytes = std::uint64_t(frame->m_outputCount) * sizeof(std::uint32_t);
    frame->m_argumentBytes = bins.size() * sizeof(RHIDrawIndexedIndirectArguments);
    if (!m_bufferPool->Acquire(frame->m_visibleBytes, false, frame->m_visibleIds, error))
    {
        return false;
    }
    if (!m_bufferPool->Acquire(frame->m_argumentBytes, true, frame->m_arguments, error))
    {
        return false;
    }

    auto constants = VisibilityConstants(viewProjection, frame->m_candidateCount, frame->m_binCount);
    constants.width = frame->m_width;
    constants.height = frame->m_height;
    constants.occlusionLevels = frame->m_occlusionLevelCount;
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
    std::vector<RHIBindingDesc> vertices, topologies;
    for (std::uint32_t bin = 0; bin < geometries.size(); ++bin) {
        const auto& geometry = geometries[bin];
        const auto topology = geometry->Meshlets();
        if (!topology.IsValid() || !topology.size || topology.offset % 4 || topology.size % 4 ||
            topology.offset / 4 > UINT32_MAX || topology.size / 4 > UINT32_MAX)
            return VisibilityFail(error, "METEOR meshlet topology cannot form a raw SRV range.");
        vertices.push_back(RHIBindingDesc::SrvBuffer(geometry->Buffer(), geometry->Count(), sizeof(material_graph::SurfacePoint)));
        topologies.push_back(RHIBindingDesc::SrvBuffer(topology.buffer, static_cast<uint32_t>(topology.size / 4),
            0, static_cast<uint32_t>(topology.offset / 4)));
        frame->m_meshletGroups += (geometry->MeshletCount() + kVisibilityThreads - 1) / kVisibilityThreads;
        frame->m_meshletOffsets.push_back(ranges[bin].offset);
    }
    if (!geometries.empty()) {
        frame->m_meshletVertices = m_device->CreateBindings(vertices);
        frame->m_meshletTopologies = m_device->CreateBindings(topologies);
        if (!frame->m_meshletVertices.IsValid() || !frame->m_meshletTopologies.IsValid())
            return VisibilityFail(error, "METEOR meshlet input table allocation failed.");
        constants.padding = frame->m_meshletGroups;
        std::memcpy(frame->m_constants.cpuAddress, &constants, sizeof(constants));
    }

    const RHIBindingDesc outputs[]{
        RHIBindingDesc::UavBuffer(frame->m_visibleIds, frame->m_outputCount, sizeof(std::uint32_t)),
        RHIBindingDesc::UavBuffer(frame->m_arguments, frame->m_binCount, sizeof(RHIDrawIndexedIndirectArguments))};
    frame->m_outputs = m_device->CreateBindings(outputs);
    if (!frame->m_outputs.IsValid() || frame->m_recording != m_device->GetCurrentUploadRecordingId() ||
        (!geometries.empty() && (frame->m_meshletVertices.version != frame->m_outputs.version ||
            frame->m_meshletTopologies.version != frame->m_outputs.version)))
    {
        return VisibilityFail(error, "GPU visibility requires complete bindings in the same upload recording.");
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
    if (!count) return;
    const auto& pipeline = reset ? m_reset : (depth.IsValid() ? m_occlusionCull : m_cull);
    encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, m_candidates);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, m_bins);
    encoder.SetBindings(RHIBindPoint::Compute, 3, m_outputs);
    if (!m_meshletGeometries.empty()) {
        encoder.SetBindings(RHIBindPoint::Compute, 4, m_meshletVertices);
        encoder.SetBindings(RHIBindPoint::Compute, 5, m_meshletTopologies);
        if (reset) { DispatchGroups(encoder, VisibilityGroups(m_binCount)); return; }
        if (depth.IsValid()) encoder.SetBindings(RHIBindPoint::Compute, 6, depth);
        if (m_historyProjection.IsValid()) encoder.SetConstantBuffer(RHIBindPoint::Compute, 7, m_historyProjection);
        if (m_firstStage) encoder.SetRootBuffer(RHIBindPoint::Compute, 7,
            m_firstStage->VisibleIds(0, m_firstStage->m_outputCount));
        DispatchGroups(encoder, m_meshletGroups);
        return;
    }
    if (!reset && depth.IsValid()) encoder.SetBindings(RHIBindPoint::Compute, 4, depth);
    DispatchGroups(encoder, VisibilityGroups(count));
}

void GpuGeometryVisibility::Frame::DispatchMeshletFinalize(RHIEncoder& encoder) const
{
    if (!m_candidateCount) return;
    encoder.SetPipeline(RHIBindPoint::Compute, m_meshletFinalize->GetHandle());
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, m_candidates);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, m_bins);
    encoder.SetBindings(RHIBindPoint::Compute, 3, m_outputs);
    encoder.SetBindings(RHIBindPoint::Compute, 4, m_meshletVertices);
    encoder.SetBindings(RHIBindPoint::Compute, 5, m_meshletTopologies);
    DispatchGroups(encoder, VisibilityGroups(m_binCount));
}


void GpuGeometryVisibility::Frame::DeclareFirstStage(EnhancedRenderGraph& graph) const
{
    if (m_historyPyramid)
    {
        m_historyPyramid->ImportHistory(graph);
        DeclareWithOcclusion(graph, m_historyPyramid);
    }
    else
    {
        const std::shared_ptr<const Frame> frames[]{m_self.lock()};
        DeclareMeshletBatch(graph, frames, "METEOR.CameraMeshlet.First");
    }
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

void GpuGeometryVisibility::Frame::DeclareMeshletBatch(EnhancedRenderGraph& graph,
    std::span<const std::shared_ptr<const Frame>> frames, const char* passPrefix)
{
    if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
        throw std::runtime_error("METEOR meshlet visibility requires an ExplicitVersioned graph.");
    std::vector<std::shared_ptr<const Frame>> owners;
    std::vector<EnhancedRenderGraph::RGPassUsage> resetUses, cullUses, finalizeUses;
    std::set<std::pair<std::uint32_t, std::uint32_t>> geometryReads;
    // Reset -> Cull -> Finalize are separate passes over the same UAV lists.
    // Each step advances both versions, so the graph owns the UAV barriers between them.
    const auto advance = [&graph](const Frame& frame, std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool first) {
        frame.m_graphArguments = first ? graph.Write(frame.m_graphArguments) : graph.Modify(frame.m_graphArguments);
        frame.m_graphVisibleIds = first ? graph.Write(frame.m_graphVisibleIds) : graph.Modify(frame.m_graphVisibleIds);
        const auto access = first ? RGAccessMode::Write : RGAccessMode::ReadWrite;
        uses.push_back({frame.m_graphArguments, RHIResourceState::UnorderedAccess, access});
        uses.push_back({frame.m_graphVisibleIds, RHIResourceState::UnorderedAccess, access});
    };
    for (const auto& frame : frames) {
        if (!frame || frame->m_meshletGeometries.empty()) throw std::runtime_error("Invalid shadow meshlet batch member.");
        frame->CheckCurrent();
        if (frame->m_occlusionCull && !frame->m_pyramid)
            throw std::runtime_error("Camera meshlet occlusion requires its declared current-view HZB.");
        if (frame->m_graph) { frame->CheckCurrent(&graph); continue; }
        frame->m_graph = &graph; frame->m_graphEpoch = graph.ResourceEpoch();
        frame->m_graphVisibleIds = graph.ImportBuffer(frame->m_visibleIds, RHIResourceState::Common, "METEOR.ShadowMeshlet.IDs");
        frame->m_graphArguments = graph.ImportBuffer(frame->m_arguments, RHIResourceState::Common, "METEOR.ShadowMeshlet.Arguments");
        graph.RequireImportedFinalState(frame->m_graphVisibleIds, RHIResourceState::Common);
        graph.RequireImportedFinalState(frame->m_graphArguments, RHIResourceState::Common);
        advance(*frame, resetUses, true);
        advance(*frame, cullUses, false);
        for (const auto& geometry : frame->m_meshletGeometries)
            for (auto handle : {geometry->GraphOutput(graph), geometry->GraphMeshlets(graph)})
                if (geometryReads.emplace(handle.index, handle.version).second)
                    cullUses.push_back({handle, RHIResourceState::ShaderResource, RGAccessMode::Read});
        if (frame->m_pyramid) frame->m_pyramid->AddReadUsages(graph, cullUses);
        if (frame->m_firstStage)
        {
            frame->m_firstStage->CheckCurrent(&graph);
            cullUses.push_back({frame->m_firstStage->m_graphVisibleIds, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        advance(*frame, finalizeUses, false);
        owners.push_back(frame);
    }
    if (owners.empty()) return;
    const auto reset = graph.AddPass(std::string(passPrefix) + ".Reset", resetUses, [owners](const auto& execution) {
        for (const auto& owner : owners) { owner->CheckCurrent(execution.graph); owner->Dispatch(*execution.encoder, true); }
    });
    const auto cull = graph.AddPass(std::string(passPrefix) + ".Cull", cullUses, [owners](const auto& execution) {
        for (const auto& owner : owners) {
            owner->CheckCurrent(execution.graph);
            owner->Dispatch(*execution.encoder, false, owner->m_pyramid ? owner->m_pyramid->Bindings(execution) : RHIBindingTable{});
        }
    });
    const auto finalize = graph.AddPass(std::string(passPrefix) + ".Finalize", finalizeUses, [owners](const auto& execution) {
        for (const auto& owner : owners) { owner->CheckCurrent(execution.graph); owner->DispatchMeshletFinalize(*execution.encoder); }
    });
    for (const auto pass : {reset, cull, finalize})
    {
        graph.DeclareComputeCompatible(pass);
        graph.DeclareRecordingIndependent(pass);
    }
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
    if (!m_meshletGeometries.empty()) {
        const std::shared_ptr<const Frame> frames[]{owner};
        DeclareMeshletBatch(graph, frames, m_ordered ? "METEOR.BlendedMeshlet" : m_firstStage ? "METEOR.CameraMeshlet.Recovery" :
            m_historyPyramid ? "METEOR.CameraMeshlet.History" : m_pyramid ? "METEOR.CameraMeshlet.HZB" : "METEOR.ShadowMeshlet"); return;
    }
    // Reset and cull write the same arguments twice. RG1 cannot express that,
    // and no caller declares visibility on an ExplicitSingleWriter graph.
    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitSingleWriter)
    {
        throw std::runtime_error("GPU visibility requires a DeclarationOrder or ExplicitVersioned graph.");
    }
    m_graph = &graph;
    m_graphEpoch = graph.ResourceEpoch();
    m_graphVisibleIds = graph.ImportBuffer(m_visibleIds, RHIResourceState::Common, "Geometry.Visibility.IDs");
    m_graphArguments = graph.ImportBuffer(m_arguments, RHIResourceState::Common, "Geometry.Visibility.Arguments");
    // A retired allocation always returns in Common; consumers can still select
    // SRV/IndirectArgument states within this recording. The graph emits the
    // final transitions after its last readers, on both backends.
    graph.RequireImportedFinalState(m_graphVisibleIds, RHIResourceState::Common);
    graph.RequireImportedFinalState(m_graphArguments, RHIResourceState::Common);

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
    graph.DeclareRecordingIndependent(resetPass);
    graph.DeclareComputeCompatible(cullPass);
    graph.DeclareRecordingIndependent(cullPass);
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

void GpuGeometryVisibility::Frame::AddCommandInputRead(EnhancedRenderGraph& graph,
    std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    if (m_graph != &graph || m_graphEpoch != graph.ResourceEpoch() || !m_graphArguments.IsValid())
        throw std::runtime_error("METEOR commands require an explicitly declared visibility producer.");
    usages.push_back({m_graphArguments, RHIResourceState::ShaderResource, RGAccessMode::Read});
}

void GpuGeometryVisibility::Frame::AddVisibleIdsRead(EnhancedRenderGraph& graph,
    std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    if (m_graph != &graph || m_graphEpoch != graph.ResourceEpoch() || !m_graphVisibleIds.IsValid())
        throw std::runtime_error("METEOR raster requires an explicitly declared visibility producer.");
    usages.push_back({m_graphVisibleIds, RHIResourceState::ShaderResource, RGAccessMode::Read});
}

RGHandle GpuGeometryVisibility::Frame::GraphVisibleIds(const EnhancedRenderGraph& graph) const
{
    if (m_graph != &graph || m_graphEpoch != graph.ResourceEpoch() || !m_graphVisibleIds.IsValid())
        throw std::runtime_error("METEOR visibility list was not declared on this graph.");
    return m_graphVisibleIds;
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
    if (m_bufferPool) m_bufferPool->Close();
    if (m_device)
    {
        m_device->UnregisterUploadTransactionListener(this);
    }
    {
        std::lock_guard lock(m_recordingMutex);
        m_recordings.clear();
        m_completed = 0;
    }
    m_meshletReset = {};
    m_meshletCull = {};
    m_meshletFinalize = {};
    m_meshletOcclusionCull = {};
    m_meshletHistoryCull = {};
    m_meshletRecoveryCull = {};
    m_reset = {};
    m_cull = {};
    m_occlusionCull = {};

    m_device = nullptr;
    m_bufferPool.reset();
}

void GpuGeometryVisibility::OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end())
    {
        auto& entry = found->second;
        for (const auto& owner : entry.owners) owner->m_poolReusable.store(false, std::memory_order_release);
        entry.submitted = true;
        entry.completion = (std::max)(entry.completion, completion.value);
        entry.quarantined = entry.quarantined || !completion.IsValid();
        if (!entry.quarantined && entry.completion <= m_completed)
        {
            for (const auto& owner : entry.owners) owner->m_poolReusable.store(true, std::memory_order_release);
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
        const bool ready = entry.submitted && !entry.quarantined && entry.completion && entry.completion <= m_completed;
        if (ready) for (const auto& owner : entry.owners) owner->m_poolReusable.store(true, std::memory_order_release);
        return ready;
    });
}

void GpuGeometryVisibility::OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end() &&
        (!found->second.submitted || found->second.completion == completion.value))
    {
        for (const auto& owner : found->second.owners) owner->m_poolReusable.store(true, std::memory_order_release);
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
