#include "GpuGeometryVisibility.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "MaterialGraphMeshSurface.h"

#include "RHI/RHIShaderSource.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include <stddef.h>
#include <stdint.h>
#include "../Utility_Framework/StandardLibrary.h"

namespace
{
constexpr std::uint32_t kVisibilityThreads = 64;
constexpr std::uint32_t kVisibilityDispatchWidth = 65535;
constexpr std::uint32_t kMeshletBinCapacity = 4096;
// Root 6 of every meshlet layout: per-bin object-to-world columns.
constexpr std::uint32_t kMeshletWorldRegister = 8211;
constexpr std::uint32_t kOrderedScanDispatch = 0x40000000u;

std::uint32_t GroupMaskWords(std::uint32_t groups) { return (groups + 15u) / 16u; }
std::uint32_t FeedbackWords(std::uint32_t groups) { return (groups + 31u) / 32u; }

std::uint32_t MeshletDescriptorCount(const IRenderDeviceServices& device, std::uint32_t bins)
{
    return device.GetShaderArrayCapabilities().partiallyBoundSrvArrays ? bins : kMeshletBinCapacity;
}

std::uint64_t MeshletOutputWords(const GpuGeometryVisibility::Candidate& candidate,
                                  const GpuGeometryVisibility::Bin& bin)
{
    if ((candidate.flags & GpuGeometryVisibility::kGroupSelection) == 0)
        return std::uint64_t(bin.indexCount) * ((candidate.flags & 32u) ? 3u : 2u) + 1u;
    const auto groups = static_cast<std::uint32_t>(bin.baseVertex);
    const auto groupWords = (candidate.flags & GpuGeometryVisibility::kReuseBinGroups) ? 0u :
        GroupMaskWords(groups) * 2u + ((candidate.flags & 32u) ? FeedbackWords(groups) : 0u);
    return std::uint64_t(bin.indexCount) * 2u + 1u + groupWords +
           ((candidate.flags & GpuGeometryVisibility::kOrderedMeshlets) ? (bin.indexCount + 63u) / 64u : 0u);
}

std::uint64_t MeshletFeedbackOffset(const GpuGeometryVisibility::Bin& bin)
{
    return std::uint64_t(bin.firstIndex) + 1u + std::uint64_t(bin.indexCount) * 2u +
           GroupMaskWords(static_cast<std::uint32_t>(bin.baseVertex)) * 2u;
}

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

GeometryVisibilityConstants VisibilityConstants(const math::matrix4x4& matrix, std::uint32_t candidates,
                                                std::uint32_t bins)
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
        constants.projectionColumns[column] = {matrix.m[0][column], matrix.m[1][column], matrix.m[2][column],
                                               matrix.m[3][column]};
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
        if (std::isfinite(normalized.x) && std::isfinite(normalized.y) && std::isfinite(normalized.z) &&
            std::isfinite(normalized.w))
        {
            constants.planes[plane] = normalized;
            constants.planeMask |= 1u << plane;
        }
    }
    return constants;
}
} // namespace

// Sealed CPU input. Declaration chooses a single-view or shared upload once;
// no intermediate per-view upload/descriptor table is built for shared work.
struct GpuGeometryVisibility::MeshletInputs
{
    GeometryVisibilityConstants constants;
    std::vector<Candidate> candidates;
    std::vector<Bin> bins;
};

struct GpuGeometryVisibility::BufferPool
{
    struct Entry
    {
        RHIBufferHandle handle;
        std::uint64_t bytes;
        bool indirect;
    };
    IRenderDeviceServices& device;
    std::mutex mutex;
    std::vector<Entry> free;
    std::vector<RHIReadback> freeReadbacks;
    std::uint64_t readbackBytes{};
    std::uint64_t cachedBytes{};
    bool closed{};
    explicit BufferPool(IRenderDeviceServices& value) : device(value) { free.reserve(64); }
    ~BufferPool() { Close(); }
    bool Acquire(std::uint64_t bytes, bool indirect, RHIBufferHandle& handle, std::string& error)
    {
        std::lock_guard lock(mutex);
        if (closed)
            return VisibilityFail(error, "Geometry visibility buffer pool is closed.");
        const auto found = std::ranges::find_if(
            free, [&](const auto& entry) { return entry.bytes == bytes && entry.indirect == indirect; });
        if (found != free.end())
        {
            handle = found->handle;
            cachedBytes -= found->bytes;
            free.erase(found);
            return true;
        }
        RHIBufferDesc desc;
        desc.bytes = bytes;
        desc.allowUnorderedAccess = true;
        desc.allowIndirectArguments = indirect;
        desc.debugName = indirect ? L"Geometry.Visibility.Arguments" : L"Geometry.Visibility.IDs";
        return device.CreateBuffer(desc, handle, error);
    }
    void Return(RHIBufferHandle handle, std::uint64_t bytes, bool indirect, bool reusable)
    {
        if (!handle.IsValid())
            return;
        std::lock_guard lock(mutex);
        constexpr std::uint64_t budget = 32ull << 20;
        if (!closed && reusable && free.size() < 64 && bytes <= budget - cachedBytes)
        {
            free.push_back({handle, bytes, indirect});
            cachedBytes += bytes;
        }
        else
            device.ReleaseBuffer(handle);
    }
    void Close()
    {
        std::lock_guard lock(mutex);
        closed = true;
        for (const auto& entry : free)
            device.ReleaseBuffer(entry.handle);
        free.clear();
        cachedBytes = 0;
        for (auto readback : freeReadbacks)
        {
            device.ReleaseReadback(readback);
        }
        freeReadbacks.clear();
        readbackBytes = 0;
    }
    bool AcquireReadback(std::uint64_t bytes, RHIReadback& result, std::string& error)
    {
        std::lock_guard lock(mutex);
        const auto found =
            std::ranges::find_if(freeReadbacks, [&](const auto& entry) { return entry.sliceBytes == bytes; });
        if (closed)
        {
            return VisibilityFail(error, "Geometry visibility readback pool is closed.");
        }
        if (found != freeReadbacks.end())
        {
            result = *found;
            readbackBytes -= bytes;
            freeReadbacks.erase(found);
            return true;
        }
        ce::profile_scope allocationProfile{ce::marker<"GeometryFeedbackReadbackCreate">()};
        return device.CreateBufferReadback(bytes, result, error);
    }
    void ReturnReadback(RHIReadback readback, bool reusable)
    {
        if (!readback.buffer.IsValid())
        {
            return;
        }
        std::lock_guard lock(mutex);
        constexpr std::uint64_t budget = 8ull << 20;
        if (!closed && reusable && freeReadbacks.size() < 128 && readback.sliceBytes <= budget - readbackBytes)
        {
            freeReadbacks.push_back(readback);
            readbackBytes += readback.sliceBytes;
        }
        else
        {
            device.ReleaseReadback(readback);
        }
    }
};

#include "GpuGeometryVisibilityShared.inl"

GpuGeometryVisibility::~GpuGeometryVisibility()
{
    // Owners destroy render passes only after the device/submission drain, just
    // as for their other persistent render resources.
    ShutdownAfterIdle();
}

GpuGeometryVisibility::Frame::~Frame()
{
    for (auto readback : m_streamingFeedback)
    {
        if (m_bufferPool)
        {
            m_bufferPool->ReturnReadback(readback, m_poolReusable.load(std::memory_order_acquire));
        }
        else if (m_device && readback.buffer.IsValid())
        {
            m_device->ReleaseReadback(readback);
        }
    }
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
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
                                              RHILayout::UavBufferTable(2, 0)};
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
    if (m_meshletCull.GetGeneration())
        return true;
    const auto source = RHIShaderSource::Resolve("GeometryVisibility.slang").string();
    RHIShaderCompileOptions options;
    options.strictMath = true;
    const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0),
                                              RHILayout::Srv(0),
                                              RHILayout::Srv(1),
                                              RHILayout::UavBufferTable(2, 0),
                                              RHILayout::SrvBufferArray(kMeshletBinCapacity, 18),
                                              RHILayout::SrvBufferArray(kMeshletBinCapacity, 18 + kMeshletBinCapacity),
                                              RHILayout::Srv(kMeshletWorldRegister)};
    const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
    if (!layout.IsValid())
        return false;
    LX::Runtime::ComputePipeline reset, cull, count, finalize, scatter;
    const auto compile = [&](const char* entry, LX::Runtime::ComputePipeline& pipeline) {
        LX::Runtime::CompiledCompute shader;
        if (!LX::Runtime::CompileCompute(source, entry, {}, options, shader, error))
            return false;
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data();
        desc.csSize = shader.stage.bytecode.Size();
        return pipeline.Create(*context.psoManager, desc, std::move(shader.description), error);
    };
    if (!compile("ShadowMeshletResetCS", reset) || !compile("ShadowMeshletCullCS", cull) ||
        !compile("MeshletCountCS", count) ||
        !compile("MeshletFinalizeCS", finalize) || !compile("MeshletScatterCS", scatter))
        return false;
    m_meshletReset = std::move(reset);
    m_meshletCull = std::move(cull);
    m_meshletCount = std::move(count);
    m_meshletFinalize = std::move(finalize);
    m_meshletScatter = std::move(scatter);
    return true;
}

bool GpuGeometryVisibility::InitializeSharedMeshlets(const EnhancedFrameContext& context, std::string& error)
{
    if (m_sharedReset.GetGeneration() && m_sharedCull.GetGeneration() &&
        m_sharedOcclusion.GetGeneration() && m_sharedCount.GetGeneration() && m_sharedFinalize.GetGeneration() &&
        m_sharedScatter.GetGeneration())
    {
        return true;
    }
    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0),
                                          RHILayout::Srv(0),
                                          RHILayout::Srv(1),
                                          RHILayout::UavBufferTable(2, 0),
                                          RHILayout::SrvBufferArray(kMeshletBinCapacity, 18),
                                          RHILayout::SrvBufferArray(kMeshletBinCapacity, 4114),
                                          RHILayout::Srv(8211),
                                          RHILayout::Srv(8212),
                                          RHILayout::UavBufferArray(kSharedViewCapacity, 2),
                                          RHILayout::UavBufferArray(kSharedViewCapacity, 10),
                                          RHILayout::SrvBufferArray(kSharedViewCapacity, 8213),
                                          RHILayout::SrvArray(kSharedViewCapacity * kOcclusionLevels, 16384)};
    const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
    if (!layout.IsValid())
    {
        return false;
    }
    const auto compile = [&](const char* entry, bool occlusion, LX::Runtime::ComputePipeline& pipeline) {
        RHIShaderPermutation permutation;
        if (!permutation.Enable("GEOMETRY_VISIBILITY_SHARED_VIEWS", error) ||
            (occlusion && !permutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error)))
        {
            return false;
        }
        LX::Runtime::CompiledCompute shader;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        if (!LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(), entry,
                                         permutation, options, shader, error))
        {
            return false;
        }
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data();
        desc.csSize = shader.stage.bytecode.Size();
        return pipeline.Create(*context.psoManager, desc, std::move(shader.description), error);
    };
    return compile("ShadowMeshletResetCS", false, m_sharedReset) &&
           compile("ShadowMeshletCullCS", false, m_sharedCull) &&
           compile("ShadowMeshletCullCS", true, m_sharedOcclusion) &&
           compile("MeshletCountCS", false, m_sharedCount) &&
           compile("MeshletFinalizeCS", false, m_sharedFinalize) &&
           compile("MeshletScatterCS", false, m_sharedScatter);
}

bool GpuGeometryVisibility::MeshletQueueStorageWords(std::uint64_t outputWords, std::uint64_t tasks, std::uint64_t bins,
                                                     std::uint32_t& storageWords)
{
    storageWords = 0;
    constexpr std::uint64_t budget = 32ull << 20;
    constexpr std::uint64_t maximumWords = budget / sizeof(std::uint32_t);
    if (!outputWords || !bins || bins > kMeshletBinCapacity || outputWords > maximumWords || tasks > maximumWords / 2)
    {
        return false;
    }
    const auto words = outputWords + kWorkRingHeaderWords + kWorkRingSlots;
    if (words * sizeof(std::uint32_t) + bins * sizeof(RHIDrawIndexedIndirectArguments) > budget)
    {
        return false;
    }
    storageWords = static_cast<std::uint32_t>(words);
    return true;
}

bool GpuGeometryVisibility::PrepareMeshlets(
    const EnhancedFrameContext& context, const math::matrix4x4& projection,
    std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries,
    std::shared_ptr<const Frame>& result, std::string& error, bool currentFrameOcclusion,
    std::shared_ptr<const GpuGeometryOcclusion::Pyramid> history, std::shared_ptr<const Frame> firstStage, bool ordered,
    float maxPixelError, uint32_t lodWidth, uint32_t lodHeight)
{
    ce::profile_scope profile{ce::marker<"GeometryVisibilityPrepare">()};
    std::vector<Candidate> candidates;
    std::vector<Bin> bins;
    // Direct-cluster chunks retain one immutable stream/hierarchy and residency
    // snapshot. Identical transforms in this view share only their group masks;
    // each chunk still owns its classification, IDs and indirect arguments.
    using GroupKey = std::tuple<std::uintptr_t, std::uintptr_t, std::array<std::uint32_t, 16>>;
    std::map<GroupKey, std::uint32_t> groupOwners;
    std::uint64_t offset{}, groups{};
    result.reset();
    if (geometries.size() > kMeshletBinCapacity)
        return VisibilityFail(error, "METEOR meshlet work exceeds 4096 bins; no per-bin dispatch route.");
    if (!context.resources)
        return VisibilityFail(error, "Meshlet work requires its current device.");
    candidates.reserve(geometries.size());
    bins.reserve(geometries.size());
    for (const auto& geometry : geometries)
    {
        if (!geometry || !geometry->MeshletCount() || geometry->Device() != context.resources)
            return VisibilityFail(error, "Shadow meshlet culling requires its nonempty transformed topology.");
        offset = (offset + kOutputAlignment - 1) / kOutputAlignment * kOutputAlignment;
        const auto count = geometry->MeshletCount();
        const auto dispatchX = (std::min)(count, kVisibilityDispatchWidth);
        const auto dispatchY =
            static_cast<uint32_t>((uint64_t(count) + kVisibilityDispatchWidth - 1) / kVisibilityDispatchWidth);
        if (!context.resources->GetMeshShaderCapabilities().SupportsDispatch(dispatchX, dispatchY, 1))
            return VisibilityFail(error,
                                  "Meshlet range exceeds native 2D dispatch limits; split and recook the asset.");
        const auto groupCount = (std::max)(1u, geometry->Input()->Hierarchy().groupCount);
        if (groupCount > static_cast<std::uint32_t>(INT32_MAX))
            return VisibilityFail(error, "Meshlet group count exceeds its sealed GPU range.");
        const auto bin = static_cast<std::uint32_t>(bins.size());
        auto groupOwner = bin;
        if (geometry->UsesDirectClusters())
        {
            const auto stream = geometry->Input()->ClusterStream();
            const auto& residency = geometry->ClusterResidency();
            if (!stream || !residency)
                return VisibilityFail(error, "Direct cluster groups require their sealed stream and residency.");
            std::array<std::uint32_t, 16> worldBits;
            const auto& world = geometry->ObjectToWorld();
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned column = 0; column < 4; ++column)
                    worldBits[row * 4u + column] = std::bit_cast<std::uint32_t>(world.m[row][column]);
            const GroupKey key{reinterpret_cast<std::uintptr_t>(stream.get()),
                               reinterpret_cast<std::uintptr_t>(residency.get()), worldBits};
            const auto [owner, inserted] = groupOwners.try_emplace(key, bin);
            groupOwner = owner->second;
            if (!inserted && static_cast<std::uint32_t>(bins[groupOwner].baseVertex) != groupCount)
                return VisibilityFail(error, "Shared cluster groups have incompatible hierarchy domains.");
        }
        // One leading word contains the GPU survivor count; IDs follow it.
        // CPU seals only one range per material/geometry bin. Compute enumerates
        // the source meshlets; no CPU-sized list of individual work items exists.
        // sourceIndex is the first 64-thread group of this range. Padding each
        // range to a group keeps descriptor selection uniform across a wave.
        // The unused sphere Z word identifies a same-view group owner.
        candidates.push_back({{float(lodWidth ? lodWidth : context.width),
                               float(lodHeight ? lodHeight : context.height), float(groupOwner), maxPixelError},
                              bin,
                              static_cast<uint32_t>(groups),
                              static_cast<uint32_t>(offset),
                              8u | kGroupSelection | (ordered ? kOrderedMeshlets : 0u) |
                                  (groupOwner != bin ? kReuseBinGroups : 0u) |
                                  (geometry->UsesDirectClusters() ? 32u : 0u)});
        bins.push_back({count, static_cast<uint32_t>(offset), static_cast<std::int32_t>(groupCount),
                        static_cast<std::uint32_t>(groups)});
        groups += (uint64_t(count) + kVisibilityThreads - 1) / kVisibilityThreads;
        offset += MeshletOutputWords(candidates.back(), bins.back());
        std::uint32_t storageWords{};
        if (!MeshletQueueStorageWords(offset, groups, bins.size(), storageWords))
            return VisibilityFail(error, "Meshlet lists, group masks and arguments exceed the 32 MiB view budget.");
    }
    if (history && firstStage)
        return VisibilityFail(error, "Meshlet stage cannot both read history and recover.");
    if (ordered && (history || firstStage))
        return VisibilityFail(error, "Ordered meshlet lists are single-stage.");
    if (!PrepareInternal(context, projection, candidates, bins, result, error,
                         currentFrameOcclusion || bool(history) || bool(firstStage), geometries))
        return false;
    if (!result)
        return true;
    auto frame = std::const_pointer_cast<Frame>(result);
    frame->m_ordered = ordered;
    if (history)
    {
        history->RequireView(frame->m_occlusionView);
        if (!history->HistoryOwner())
            return VisibilityFail(error, "First stage requires retained history.");
        frame->m_historyPyramid = std::move(history);
        frame->m_occlusionCull = m_meshletHistoryCull.GetGeneration();
    }
    bool reuseGroups = false;
    if (firstStage)
    {
        if (firstStage->m_occlusionView != frame->m_occlusionView ||
            firstStage->m_meshletGeometries != frame->m_meshletGeometries ||
            firstStage->m_meshletOffsets != frame->m_meshletOffsets)
            return VisibilityFail(error, "Meshlet recovery requires the same sealed first-stage geometry and view.");
        // Occlusion history changes visibility, not desired/resident LOD.
        // Include the explicit LOD metric in the first-stage reuse proof.
        reuseGroups = !firstStage->m_firstStage && firstStage->m_meshletInputs &&
                      firstStage->m_meshletInputs->candidates.size() == candidates.size();
        for (std::size_t bin = 0; reuseGroups && bin < candidates.size(); ++bin)
            reuseGroups = std::memcmp(&candidates[bin].sphere,
                                      &firstStage->m_meshletInputs->candidates[bin].sphere,
                                      sizeof(math::vector4)) == 0;
        if (reuseGroups)
            for (auto& candidate : frame->m_meshletInputs->candidates)
                candidate.flags |= kReuseFirstGroups;
        frame->m_firstStage = std::move(firstStage);
        frame->m_occlusionCull = m_meshletRecoveryCull.GetGeneration();
    }
    if (!reuseGroups)
    {
        frame->m_streamingFeedback.resize(geometries.size());
        for (std::size_t bin = 0; bin < geometries.size(); ++bin)
        {
            const auto words = FeedbackWords(static_cast<std::uint32_t>(bins[bin].baseVertex));
            if (geometries[bin]->UsesDirectClusters() && (candidates[bin].flags & kReuseBinGroups) == 0 &&
                !m_bufferPool->AcquireReadback(std::uint64_t(words) * 4u, frame->m_streamingFeedback[bin], error))
                return false;
        }
    }
    return true;
}

bool GpuGeometryVisibility::InitializeMeshletOcclusion(const EnhancedFrameContext& context, std::string& error)
{
    if (m_meshletOcclusionCull.GetGeneration() && m_meshletHistoryCull.GetGeneration() &&
        m_meshletRecoveryCull.GetGeneration())
        return true;
    const auto compile = [&](LX::Runtime::ComputePipeline& pipeline, const char* stage) {
        std::vector<RHIPipelineLayoutParam> parameters{
            RHILayout::Cbv(0),
            RHILayout::Srv(0),
            RHILayout::Srv(1),
            RHILayout::UavBufferTable(2, 0),
            RHILayout::SrvBufferArray(kMeshletBinCapacity, 18),
            RHILayout::SrvBufferArray(kMeshletBinCapacity, 18 + kMeshletBinCapacity),
            RHILayout::Srv(kMeshletWorldRegister),
            RHILayout::SrvTable(kOcclusionLevels, 2)};
        if (stage)
            parameters.push_back(std::string_view(stage) == "GEOMETRY_VISIBILITY_HISTORY" ? RHILayout::Cbv(1)
                                                                                          : RHILayout::Srv(8210));
        const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
        if (!layout.IsValid())
            return false;
        RHIShaderCompileOptions options;
        options.strictMath = true;
        RHIShaderPermutation permutation;
        if (stage && !permutation.Enable(stage, error))
            return false;
        LX::Runtime::CompiledCompute shader;
        if (!permutation.Enable("GEOMETRY_VISIBILITY_OCCLUSION", error) ||
            !LX::Runtime::CompileCompute(RHIShaderSource::Resolve("GeometryVisibility.slang").string(),
                                         "ShadowMeshletCullCS", permutation, options, shader, error))
            return false;
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = shader.stage.bytecode.Data();
        desc.csSize = shader.stage.bytecode.Size();
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
    const RHIPipelineLayoutParam cullParameters[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
                                                  RHILayout::UavBufferTable(2, 0),
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
        !context.resources->GetCurrentUploadRecordingId() ||
        !GpuGeometryOcclusion::LevelCount(context.width, context.height))
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

bool GpuGeometryVisibility::PrepareMeshletPipelines(const EnhancedFrameContext& context, std::string& error)
{
    if (!context.resources || !context.rootSignatures || !context.psoManager ||
        !context.resources->GetCurrentUploadRecordingId())
        return VisibilityFail(error, "Meshlet preflight requires pipeline services and an active recording.");
    return Initialize(context, error) && InitializeMeshlets(context, error) &&
           InitializeSharedMeshlets(context, error) && InitializeMeshletOcclusion(context, error);
}

bool GpuGeometryVisibility::PrepareInternal(
    const EnhancedFrameContext& context, const math::matrix4x4& viewProjection, std::span<const Candidate> candidates,
    std::span<const Bin> bins, std::shared_ptr<const Frame>& result, std::string& error, bool currentFrameOcclusion,
    std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries)
{
    ce::profile_scope profile{ce::marker<"GpuVisibility.PrepareInternal">()};
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
        if (!geometries.empty())
        {
            const auto words = MeshletOutputWords(candidate, bins[candidate.bin]);
            if (words > maximum)
                return VisibilityFail(error, "Meshlet group range exceeds 32-bit addressing.");
            range.count = static_cast<std::uint32_t>(words);
        }
        else
            ++range.count;
    }
    std::vector<std::vector<std::uint8_t>> preservedSources(bins.size());
    for (std::size_t bin = 0; bin < bins.size(); ++bin)
    {
        if (geometries.empty() && bins[bin].preservedInstanceCount != 0u)
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
    if (!geometries.empty() && (!InitializeMeshlets(context, error) || !InitializeSharedMeshlets(context, error)))
        return false;
    if (currentFrameOcclusion && (!context.width || !context.height || context.width > (1u << kOcclusionLevels) ||
                                  context.height > (1u << kOcclusionLevels)))
    {
        return VisibilityFail(error, "GPU occlusion requires a nonempty view within the pyramid extent limit.");
    }
    if (currentFrameOcclusion &&
        !(geometries.empty() ? InitializeOcclusion(context, error) : InitializeMeshletOcclusion(context, error)))
    {
        return false;
    }

    auto frame = std::shared_ptr<Frame>(new Frame);
    frame->m_device = m_device;
    frame->m_sharedReset = m_sharedReset.GetGeneration();
    frame->m_sharedCull = m_sharedCull.GetGeneration();
    frame->m_sharedOcclusion = m_sharedOcclusion.GetGeneration();
    frame->m_sharedFinalize = m_sharedFinalize.GetGeneration();
    frame->m_sharedCount = m_sharedCount.GetGeneration();
    frame->m_sharedScatter = m_sharedScatter.GetGeneration();
    frame->m_recording = m_device->GetCurrentUploadRecordingId();
    frame->m_preparedStats.candidateCount = static_cast<std::uint32_t>(candidates.size());
    if (!geometries.empty())
    {
        frame->m_preparedStats.candidateCount = 0;
        for (const auto& geometry : geometries)
            frame->m_preparedStats.candidateCount += geometry->MeshletCount();
    }
    for (const auto& bin : bins)
    {
        if (geometries.empty() && bin.preservedInstanceCount != 0u)
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
        if ((candidate.flags & kConservative) != 0u || !(candidate.sphere.w > 0.f) ||
            !std::isfinite(candidate.sphere.x) || !std::isfinite(candidate.sphere.y) ||
            !std::isfinite(candidate.sphere.z) || !std::isfinite(candidate.sphere.w))
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
    if (!geometries.empty())
    {
        frame->m_meshletCount = m_meshletCount.GetGeneration();
        frame->m_meshletFinalize = m_meshletFinalize.GetGeneration();
        frame->m_meshletScatter = m_meshletScatter.GetGeneration();
    }
    if (currentFrameOcclusion)
    {
        frame->m_occlusionCull =
            geometries.empty() ? m_occlusionCull.GetGeneration() : m_meshletOcclusionCull.GetGeneration();
        frame->m_width = context.width;
        frame->m_height = context.height;
        frame->m_occlusionLevelCount = GpuGeometryOcclusion::LevelCount(context.width, context.height);
    }

    frame->m_bufferPool = m_bufferPool;
    // Queue storage shares the recording-owned UAV allocation and its RG versions.
    // Public ID slices still end at m_outputCount; consumers cannot address the queue.
    std::uint64_t storageWords = frame->m_outputCount;
    if (!geometries.empty())
    {
        std::uint64_t tasks{};
        for (const auto& geometry : geometries)
        {
            tasks += (std::uint64_t(geometry->MeshletCount()) + 63u) / 64u;
        }
        std::uint32_t checkedWords{};
        if (!MeshletQueueStorageWords(storageWords, tasks, bins.size(), checkedWords))
        {
            return VisibilityFail(error, "Meshlet work queue exceeds its view byte budget.");
        }
        storageWords = checkedWords;
    }
    if (storageWords > UINT32_MAX)
    {
        return VisibilityFail(error, "Meshlet work queue exceeds 32-bit UAV addressing.");
    }
    frame->m_visibleBytes = storageWords * sizeof(std::uint32_t);
    frame->m_argumentBytes = bins.size() * sizeof(RHIDrawIndexedIndirectArguments);
    {
        ce::profile_scope bufferProfile{ce::marker<"GpuVisibility.PrepareBuffers">()};
        if (!m_bufferPool->Acquire(frame->m_visibleBytes, false, frame->m_visibleIds, error))
        {
            return false;
        }
        if (!m_bufferPool->Acquire(frame->m_argumentBytes, true, frame->m_arguments, error))
        {
            return false;
        }
    }

    auto constants = VisibilityConstants(viewProjection, frame->m_candidateCount, frame->m_binCount);
    constants.width = frame->m_width;
    constants.height = frame->m_height;
    constants.occlusionLevels = frame->m_occlusionLevelCount;
    for (std::uint32_t bin = 0; bin < geometries.size(); ++bin)
    {
        const auto& geometry = geometries[bin];
        const auto topology = geometry->Meshlets();
        if (!topology.IsValid() || !topology.size || topology.offset % 4 || topology.size % 4 ||
            topology.offset / 4 > UINT32_MAX || topology.size / 4 > UINT32_MAX)
            return VisibilityFail(error, "METEOR meshlet topology cannot form a raw SRV range.");
        frame->m_meshletGroups += (geometry->MeshletCount() + kVisibilityThreads - 1) / kVisibilityThreads;
        frame->m_meshletJobs += geometry->MeshletCount();
        frame->m_meshletOffsets.push_back(bins[bin].firstIndex);
    }
    if (!geometries.empty())
    {
        constants.padding = frame->m_meshletJobs;
        constants.planeMask |= kWaveWorkQueue;
        if ((candidates.front().flags & kOrderedMeshlets) != 0u)
            constants.planeMask |= kOrderedScanDispatch;
        frame->m_meshletInputs = std::make_shared<MeshletInputs>();
        frame->m_meshletInputs->constants = constants;
        frame->m_meshletInputs->candidates.assign(candidates.begin(), candidates.end());
        frame->m_meshletInputs->bins.assign(bins.begin(), bins.end());
    }
    else
    {
        const RHIUploadRequest requests[]{
            {(std::max)(std::size_t{1}, candidates.size()) * sizeof(Candidate), RHIUploadUsage::Raw, 256},
            {bins.size_bytes(), RHIUploadUsage::Raw, 256},
            {sizeof(constants), RHIUploadUsage::ConstantBuffer, 256}};
        std::array<RHIBufferSlice, 3> uploads;
        if (!m_device->ReserveUploadBatch(requests, uploads, error))
            return false;
        for (const auto& upload : uploads)
            if (!upload.IsWritable() || !upload.IsValid())
                return VisibilityFail(error, "GPU visibility upload reservation is not writable.");
        frame->m_candidates = uploads[0];
        frame->m_bins = uploads[1];
        frame->m_constants = uploads[2];
        if (!candidates.empty())
            std::memcpy(frame->m_candidates.cpuAddress, candidates.data(), candidates.size_bytes());
        else
            std::memset(frame->m_candidates.cpuAddress, 0, sizeof(Candidate));
        std::memcpy(frame->m_bins.cpuAddress, bins.data(), bins.size_bytes());
        std::memcpy(frame->m_constants.cpuAddress, &constants, sizeof(constants));
        const RHIBindingDesc outputs[]{
            RHIBindingDesc::UavBuffer(frame->m_visibleIds, static_cast<std::uint32_t>(storageWords), 4),
            RHIBindingDesc::UavBuffer(frame->m_arguments, frame->m_binCount, sizeof(RHIDrawIndexedIndirectArguments))};
        frame->m_outputs = m_device->CreateBindings(outputs);
        if (!frame->m_outputs.IsValid())
            return VisibilityFail(error, "GPU visibility output bindings are incomplete.");
    }
    if (frame->m_recording != m_device->GetCurrentUploadRecordingId())
        return VisibilityFail(error, "GPU visibility preparation changed upload recording.");
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
    auto identity =
        context.resources->AllocateUpload({std::uint64_t(count) * sizeof(std::uint32_t), RHIUploadUsage::Raw, 256});
    if (!identity.IsWritable() || !identity.IsValid() || recording != context.resources->GetCurrentUploadRecordingId())
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
        (graph && (m_graph != graph || m_graphEpoch != graph->ResourceEpoch() || &graph->DeviceServices() != m_device)))
    {
        throw std::runtime_error("GPU visibility frame escaped its recording, descriptors or graph epoch.");
    }
}

void GpuGeometryVisibility::Frame::Dispatch(RHIEncoder& encoder, bool reset, RHIBindingTable depth) const
{
    if (!m_meshletGeometries.empty())
    {
        DispatchMeshletStage(encoder, reset ? MeshletStage::Reset : MeshletStage::Cull, depth);
        return;
    }
    const auto count = reset ? m_binCount : m_candidateCount;
    if (!count)
        return;
    const auto& pipeline = reset ? m_reset : (depth.IsValid() ? m_occlusionCull : m_cull);
    encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, m_candidates);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, m_bins);
    encoder.SetBindings(RHIBindPoint::Compute, 3, m_outputs);
    if (!reset && depth.IsValid())
        encoder.SetBindings(RHIBindPoint::Compute, 4, depth);
    DispatchGroups(encoder, VisibilityGroups(count));
}

void GpuGeometryVisibility::Frame::DispatchMeshletStage(RHIEncoder& encoder, MeshletStage stage,
                                                        RHIBindingTable depth) const
{
    if (!m_candidateCount)
        return;
    const auto& pipeline = stage == MeshletStage::Reset ? m_reset :
                           stage == MeshletStage::Cull ? (depth.IsValid() ? m_occlusionCull : m_cull) :
                           stage == MeshletStage::Count ? m_meshletCount :
                           stage == MeshletStage::Finalize ? m_meshletFinalize : m_meshletScatter;
    encoder.SetPipeline(RHIBindPoint::Compute, pipeline->GetHandle());
    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, m_constants);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 1, m_candidates);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 2, m_bins);
    encoder.SetBindings(RHIBindPoint::Compute, 3, m_outputs);
    encoder.SetBindings(RHIBindPoint::Compute, 4, m_meshletVertices);
    encoder.SetBindings(RHIBindPoint::Compute, 5, m_meshletTopologies);
    encoder.SetRootBuffer(RHIBindPoint::Compute, 6, m_meshletWorlds);
    if (stage == MeshletStage::Cull)
    {
        if (depth.IsValid())
            encoder.SetBindings(RHIBindPoint::Compute, 7, depth);
        if (m_historyProjection.IsValid())
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 8, m_historyProjection);
        if (m_firstStage)
            encoder.SetRootBuffer(RHIBindPoint::Compute, 8, m_firstStage->VisibleIds(0, m_firstStage->m_outputCount));
    }
    DispatchGroups(encoder, stage == MeshletStage::Cull
                                ? (std::min)(m_binCount * kInitialSubdivisions, kPersistentConsumerGroups)
                            : stage == MeshletStage::Count || stage == MeshletStage::Scatter ? m_meshletGroups
                            : stage == MeshletStage::Finalize && !m_ordered ? VisibilityGroups(m_binCount)
                                                                                           : m_binCount);
}

void GpuGeometryVisibility::Frame::DeclareFirstStage(EnhancedRenderGraph& graph, const char* passPrefix) const
{
    const std::shared_ptr<const Frame> frames[]{m_self.lock()};
    DeclareFirstStageBatch(graph, frames, passPrefix);
}

void GpuGeometryVisibility::Frame::DeclareFirstStageBatch(EnhancedRenderGraph& graph,
                                                          std::span<const std::shared_ptr<const Frame>> frames,
                                                          const char* passPrefix)
{
    for (const auto& frame : frames)
    {
        if (!frame)
        {
            throw std::runtime_error("Invalid first-stage shared view.");
        }
        frame->CheckCurrent();
        if (!frame->m_graph && frame->m_historyPyramid)
        {
            frame->m_historyPyramid->RequireView(frame->m_occlusionView);
            frame->m_historyPyramid->ImportHistory(graph);
            frame->m_pyramid = frame->m_historyPyramid;
        }
    }
    DeclareMeshletBatch(graph, frames, passPrefix);
}

void GpuGeometryVisibility::Frame::DeclareOcclusionBatch(
    EnhancedRenderGraph& graph, std::span<const std::shared_ptr<const Frame>> frames,
    std::span<const std::shared_ptr<const GpuGeometryOcclusion::Pyramid>> pyramids, const char* passPrefix)
{
    if (frames.size() != pyramids.size())
    {
        throw std::runtime_error("Shared occlusion view/pyramid count mismatch.");
    }
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        const auto& frame = frames[index];
        if (!frame || !pyramids[index] || frame->m_graph || !frame->m_occlusionCull ||
            pyramids[index]->LevelCount() != frame->m_occlusionLevelCount)
        {
            throw std::runtime_error("Shared occlusion requires undeclared compatible views.");
        }
        frame->CheckCurrent();
        pyramids[index]->RequireView(frame->m_occlusionView);
        frame->m_pyramid = pyramids[index];
    }
    DeclareMeshletBatch(graph, frames, passPrefix);
}

void GpuGeometryVisibility::Frame::DeclareWithOcclusion(EnhancedRenderGraph& graph,
                                                        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> pyramid,
                                                        const char* passPrefix) const
{
    CheckCurrent();
    if (m_graph || !m_occlusionCull || !pyramid || pyramid->LevelCount() != m_occlusionLevelCount ||
        graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
    {
        throw std::runtime_error("GPU visibility requires an undeclared occlusion-capable frame and matching pyramid.");
    }
    pyramid->RequireView(m_occlusionView);
    m_pyramid = std::move(pyramid);
    if (passPrefix && !m_meshletGeometries.empty())
    {
        const std::shared_ptr<const Frame> frames[]{m_self.lock()};
        DeclareMeshletBatch(graph, frames, passPrefix);
        return;
    }
    Declare(graph);
}

void GpuGeometryVisibility::Frame::DeclareMeshletBatch(EnhancedRenderGraph& graph,
                                                       std::span<const std::shared_ptr<const Frame>> frames,
                                                       const char* passPrefix)
{
    ce::profile_scope profile{ce::marker<"GpuVisibility.MeshletDeclare">()};
    if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
        throw std::runtime_error("METEOR meshlet visibility requires an ExplicitVersioned graph.");
    std::uint64_t totalBins{};
    for (const auto& frame : frames)
    {
        if (!frame)
        {
            throw std::runtime_error("Null shared visibility view.");
        }
        totalBins += frame->m_binCount;
    }
    if (frames.size() > kSharedViewCapacity || totalBins > kMeshletBinCapacity)
    {
        std::vector<std::shared_ptr<const Frame>> chunk;
        std::uint32_t bins{}, number{};
        for (const auto& frame : frames)
        {
            if (!chunk.empty() &&
                (chunk.size() == kSharedViewCapacity || bins + frame->m_binCount > kMeshletBinCapacity))
            {
                const auto prefix = std::string(passPrefix) + ".Batch" + std::to_string(number++);
                DeclareMeshletBatch(graph, chunk, prefix.c_str());
                chunk.clear();
                bins = 0;
            }
            chunk.push_back(frame);
            bins += frame->m_binCount;
        }
        if (!chunk.empty())
        {
            const auto prefix = std::string(passPrefix) + ".Batch" + std::to_string(number);
            DeclareMeshletBatch(graph, chunk, prefix.c_str());
        }
        return;
    }
    std::vector<std::shared_ptr<const Frame>> owners;
    std::vector<EnhancedRenderGraph::RGPassUsage> resetUses, cullUses, countUses, finalizeUses, scatterUses;
    const bool ordered = std::ranges::any_of(frames, [](const auto& frame) {
        return !frame->m_graph && frame->m_ordered;
    });
    owners.reserve(frames.size());
    resetUses.reserve(frames.size() * 2u + 1u);
    finalizeUses.reserve(frames.size() * 2u);
    cullUses.reserve(static_cast<std::size_t>(totalBins) * 2u +
        frames.size() * (3u + GpuGeometryOcclusion::kMaximumLevels) + 1u);
    std::set<std::pair<std::uint32_t, std::uint32_t>> geometryReads, topologyReads;
    // Group reset/selection, lane work and stable compaction use separate RG
    // versions. No shader relies on a cross-dispatch implicit UAV barrier.
    const auto advance = [&graph](const Frame& frame, std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool first) {
        frame.m_graphArguments = first ? graph.Write(frame.m_graphArguments) : graph.Modify(frame.m_graphArguments);
        frame.m_graphVisibleIds = first ? graph.Write(frame.m_graphVisibleIds) : graph.Modify(frame.m_graphVisibleIds);
        const auto access = first ? RGAccessMode::Write : RGAccessMode::ReadWrite;
        uses.push_back({frame.m_graphArguments, RHIResourceState::UnorderedAccess, access});
        uses.push_back({frame.m_graphVisibleIds, RHIResourceState::UnorderedAccess, access});
    };
    for (const auto& frame : frames)
    {
        if (!frame || frame->m_meshletGeometries.empty())
            throw std::runtime_error("Invalid shadow meshlet batch member.");
        frame->CheckCurrent();
        if (frame->m_occlusionCull && !frame->m_pyramid)
            throw std::runtime_error("Camera meshlet occlusion requires its declared current-view HZB.");
        if (frame->m_graph)
        {
            frame->CheckCurrent(&graph);
            continue;
        }
        frame->m_graph = &graph;
        frame->m_graphEpoch = graph.ResourceEpoch();
        frame->m_graphVisibleIds =
            graph.ImportBuffer(frame->m_visibleIds, RHIResourceState::Common, "METEOR.ShadowMeshlet.IDs");
        frame->m_graphArguments =
            graph.ImportBuffer(frame->m_arguments, RHIResourceState::Common, "METEOR.ShadowMeshlet.Arguments");
        graph.RequireImportedFinalState(frame->m_graphVisibleIds, RHIResourceState::Common);
        graph.RequireImportedFinalState(frame->m_graphArguments, RHIResourceState::Common);
        advance(*frame, resetUses, true);
        advance(*frame, cullUses, false);
        for (const auto& geometry : frame->m_meshletGeometries)
        {
            const auto topology = geometry->GraphMeshlets(graph);
            // Group selection reads the immutable hierarchy/residency and CPU
            // world columns. Posed vertex output is needed only by the cull.
            if (topologyReads.emplace(topology.index, topology.version).second)
                resetUses.push_back({topology, RHIResourceState::ShaderResource, RGAccessMode::Read});
            for (auto handle : {geometry->GraphOutput(graph), topology})
                if (geometryReads.emplace(handle.index, handle.version).second)
                    cullUses.push_back({handle, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        if (frame->m_pyramid)
            frame->m_pyramid->AddReadUsages(graph, cullUses);
        if (frame->m_firstStage)
        {
            frame->m_firstStage->CheckCurrent(&graph);
            cullUses.push_back(
                {frame->m_firstStage->m_graphVisibleIds, RHIResourceState::ShaderResource, RGAccessMode::Read});
        }
        if (ordered)
            advance(*frame, countUses, false);
        advance(*frame, finalizeUses, false);
        if (ordered)
            advance(*frame, scatterUses, false);
        owners.push_back(frame);
    }
    if (owners.empty())
        return;
    const auto shared = owners.size() > 1 ? BuildShared(owners) : std::shared_ptr<SharedBatch>{};
    if (!shared)
        owners.front()->UploadMeshletInputs();
    if (shared)
    {
        auto queue = graph.ImportBuffer(shared->queue, RHIResourceState::Common, "METEOR.SharedVisibility.Queue");
        graph.RequireImportedFinalState(queue, RHIResourceState::Common);
        queue = graph.Write(queue);
        resetUses.push_back({queue, RHIResourceState::UnorderedAccess, RGAccessMode::Write});
        queue = graph.Modify(queue);
        cullUses.push_back({queue, RHIResourceState::UnorderedAccess, RGAccessMode::ReadWrite});
        shared->graphQueue = queue;
        for (const auto& owner : owners)
        {
            owner->m_sharedBatch = shared;
        }
    }
    const auto declare = [&](const char* suffix, std::vector<EnhancedRenderGraph::RGPassUsage> uses,
                             MeshletStage stage) {
        const auto pass = graph.AddPass(std::string(passPrefix) + suffix, std::move(uses),
            [owners, shared, stage](const auto& execution) {
                for (const auto& owner : owners)
                    owner->CheckCurrent(execution.graph);
                if (shared)
                {
                    shared->Dispatch(execution, stage);
                    return;
                }
                const auto& owner = owners.front();
                owner->DispatchMeshletStage(*execution.encoder, stage,
                    stage == MeshletStage::Cull && owner->m_pyramid
                        ? owner->m_pyramid->Bindings(execution) : RHIBindingTable{});
            });
        graph.DeclareComputeCompatible(pass);
        graph.DeclareRecordingIndependent(pass);
    };
    declare(".Reset", std::move(resetUses), MeshletStage::Reset);
    declare(".Cull", std::move(cullUses), MeshletStage::Cull);
    if (ordered)
        declare(".CountTiles", std::move(countUses), MeshletStage::Count);
    declare(".Finalize", std::move(finalizeUses), MeshletStage::Finalize);
    if (ordered)
        declare(".Scatter", std::move(scatterUses), MeshletStage::Scatter);
    for (const auto& owner : owners)
    {
        if (std::ranges::none_of(owner->m_streamingFeedback, [](const auto& r) { return r.buffer.IsValid(); }))
            continue;
        graph.AddPass(
            std::string(passPrefix) + ".StreamingFeedback",
            {{owner->m_graphVisibleIds, RHIResourceState::CopySource, RGAccessMode::Read}},
            [owner](const auto& execution) {
                for (uint32_t bin = 0; bin < owner->m_streamingFeedback.size(); ++bin)
                {
                    const auto& readback = owner->m_streamingFeedback[bin];
                    if (!readback.buffer.IsValid())
                        continue;
                    const auto offset = MeshletFeedbackOffset(owner->m_meshletInputs->bins[bin]) * sizeof(uint32_t);
                    execution.encoder->CopyBufferToReadback(readback, owner->m_visibleIds, offset);
                }
                owner->m_feedbackRecorded.store(true, std::memory_order_release);
            },
            true);
    }
}

void GpuGeometryVisibility::Frame::ConsumeStreamingFeedback(std::span<const std::shared_ptr<const Frame>> frames)
{
    struct Pending
    {
        std::shared_ptr<assets::cluster::Stream> stream;
        std::vector<std::uint32_t> words;
        std::uint32_t groups{};
    };
    std::map<assets::cluster::Stream*, Pending> pending;
    // The completion ledger has already detached these owners. Mapping, union
    // and streaming admission run without its mutex or any GPU wait.
    for (const auto& frame : frames)
        frame->m_poolReusable.store(true, std::memory_order_release);
    for (const auto& frame : frames)
    {
        if (!frame->m_feedbackRecorded.exchange(false, std::memory_order_acq_rel))
            continue;
        for (std::size_t bin = 0; bin < frame->m_streamingFeedback.size(); ++bin)
        {
            const auto& readback = frame->m_streamingFeedback[bin];
            if (!readback.buffer.IsValid())
                continue;
            RHIReadbackImage image;
            std::string error;
            if (!frame->m_device->MapReadback(readback, image, error))
                throw std::runtime_error("Completed streaming feedback readback: " + error);
            const auto groups = static_cast<std::uint32_t>(frame->m_meshletInputs->bins[bin].baseVertex);
            const auto demands = image.Elements<std::uint32_t>();
            if (demands.size() != FeedbackWords(groups))
                throw std::runtime_error("Streaming group feedback extent mismatch.");
            const auto& stream = frame->m_meshletGeometries[bin]->Input()->ClusterStream();
            auto& collected = pending[stream.get()];
            if (!collected.stream)
            {
                collected.stream = stream;
                collected.groups = groups;
                collected.words.resize(demands.size());
            }
            if (collected.groups != groups)
                throw std::runtime_error("Streaming feedback mixed incompatible group domains.");
            for (std::size_t word = 0; word < demands.size(); ++word)
                collected.words[word] |= demands[word];
        }
    }
    for (auto& item : pending)
    {
        auto& collected = item.second;
        std::vector<std::uint32_t> requests;
        for (std::size_t word = 0; word < collected.words.size(); ++word)
        {
            auto bits = collected.words[word];
            while (bits)
            {
                const auto group = static_cast<std::uint32_t>(word * 32u + std::countr_zero(bits));
                if (group >= collected.groups)
                    throw std::runtime_error("Streaming feedback set a padding group bit.");
                requests.push_back(group);
                bits &= bits - 1u;
            }
        }
        // Includes desired resident groups: they refresh streaming keepalive.
        // A word walk already yields one ascending group set across all views.
        if (!requests.empty())
            collected.stream->SubmitGpuRequests(requests);
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
    if (!m_meshletGeometries.empty())
    {
        const std::shared_ptr<const Frame> frames[]{owner};
        DeclareMeshletBatch(graph, frames,
                            m_ordered          ? "METEOR.BlendedMeshlet"
                            : m_firstStage     ? "METEOR.CameraMeshlet.Recovery"
                            : m_historyPyramid ? "METEOR.CameraMeshlet.History"
                            : m_pyramid        ? "METEOR.CameraMeshlet.HZB"
                                               : "METEOR.ShadowMeshlet");
        return;
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
    const auto cullPass = graph.AddPass(m_pyramid ? "Geometry.Visibility.CullOcclusion" : "Geometry.Visibility.Cull",
                                        usages, [owner](const auto& execution) {
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

void GpuGeometryVisibility::Frame::AddReadUsages(EnhancedRenderGraph& graph,
                                                 std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const
{
    Declare(graph);
    const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState
                                                                                        : RGAccessMode::Read;
    for (const auto& usage :
         {EnhancedRenderGraph::RGPassUsage{m_graphArguments, RHIResourceState::IndirectArgument, access},
          EnhancedRenderGraph::RGPassUsage{m_graphVisibleIds, RHIResourceState::ShaderResource, access}})
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
    if (!count || outputOffset % kOutputAlignment != 0 || std::uint64_t(outputOffset) + count > m_outputCount)
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
    if (m_bufferPool)
        m_bufferPool->Close();
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
    m_meshletCount = {};
    m_meshletScatter = {};
    m_sharedReset = {};
    m_sharedCount = {};
    m_sharedScatter = {};
    m_sharedCull = {};
    m_sharedOcclusion = {};
    m_sharedFinalize = {};
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
    std::vector<std::shared_ptr<const Frame>> ready;
    {
        std::lock_guard lock(m_recordingMutex);
        const auto found = m_recordings.find(recording);
        if (found != m_recordings.end())
        {
            auto& entry = found->second;
            for (const auto& owner : entry.owners)
                owner->m_poolReusable.store(false, std::memory_order_release);
            entry.submitted = true;
            entry.completion = (std::max)(entry.completion, completion.value);
            entry.quarantined = entry.quarantined || !completion.IsValid();
            if (!entry.quarantined && entry.completion <= m_completed)
            {
                ready = std::move(entry.owners);
                m_recordings.erase(found);
            }
        }
    }
    Frame::ConsumeStreamingFeedback(ready);
}

void GpuGeometryVisibility::OnUploadCompleted(std::uint64_t completed)
{
    if (!completed || completed == UINT64_MAX)
        return; // Device loss does not certify feedback or reusable GPU storage.
    std::vector<std::shared_ptr<const Frame>> ready;
    {
        std::lock_guard lock(m_recordingMutex);
        m_completed = (std::max)(m_completed, completed);
        std::erase_if(m_recordings, [&](const auto& recording) {
            const auto& entry = recording.second;
            const bool complete = entry.submitted && !entry.quarantined && entry.completion &&
                                  entry.completion <= m_completed;
            if (complete)
                ready.insert(ready.end(), entry.owners.begin(), entry.owners.end());
            return complete;
        });
    }
    Frame::ConsumeStreamingFeedback(ready);
}

void GpuGeometryVisibility::OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion)
{
    std::lock_guard lock(m_recordingMutex);
    const auto found = m_recordings.find(recording);
    if (found != m_recordings.end() && (!found->second.submitted || found->second.completion == completion.value))
    {
        for (const auto& owner : found->second.owners)
            owner->m_poolReusable.store(true, std::memory_order_release);
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
