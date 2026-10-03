#include "MaterialGraphMeshSurface.h"
#include "../EngineDiagnostics/ProfileScope.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
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

        bool Finite(std::span<const float> values)
        {
            return std::ranges::all_of(values,
                                       [](float value) { return std::isfinite(value) && std::abs(value) <= 1e6f; });
        }

        bool Affine(const math::matrix4x4& matrix)
        {
            return Finite(std::bit_cast<std::array<float, 16>>(matrix)) && matrix.m[0][3] == 0 && matrix.m[1][3] == 0 &&
                   matrix.m[2][3] == 0 && matrix.m[3][3] == 1;
        }

        template <class T> T Read(const std::byte* vertex, std::uint32_t mask, assets::VertexAttribute attribute)
        {
            T value;
            std::memcpy(&value, vertex + assets::OffsetOf(mask, attribute), sizeof(value));
            return value;
        }

        bool Direction(const float* value)
        {
            return std::hypot(value[0], value[1], value[2]) > 1e-10;
        }
    } // namespace

    struct MeshSurfaceStaticBuffers
    {
        IRenderDeviceServices& device;
        std::shared_ptr<const MeshSurfaceInput> identity;
        RHIBufferSlice vertices, indices, lods;
        std::array<RHIBufferSlice, 3> staging;
        const EnhancedRenderGraph* graph{};
        std::uint64_t graphEpoch{};
        std::array<RGHandle, 3> graphBuffers;
        std::uint64_t bytes{}, recording{}, completion{};
        RHIUploadTransactionState state{RHIUploadTransactionState::Recording};
        bool prepared{};
        std::atomic<bool> usable{};
        explicit MeshSurfaceStaticBuffers(IRenderDeviceServices& owner) : device(owner)
        {
        }
        ~MeshSurfaceStaticBuffers()
        {
            for (auto buffer : {vertices.buffer, indices.buffer, lods.buffer})
            {
                if (buffer.IsValid())
                {
                    device.ReleaseBuffer(buffer);
                }
            }
        }
    };

    // The immutable partition owns the cache identity; a recycled CPU address or
    // another chunk of the same model cannot alias it. Upload transactions retain
    // new buffers through submission/abort, and eviction waits for their last use.
    struct MeshSurfaceStaticCache final : IRHIUploadTransactionListener
    {
        IRenderDeviceServices& device;
        mutable std::mutex mutex;
        std::vector<std::shared_ptr<MeshSurfaceStaticBuffers>> entries;
        std::map<std::uint64_t, std::vector<std::shared_ptr<MeshSurfaceStaticBuffers>>> recordings;
        std::uint64_t completed{};
        MeshSurfaceCacheStats stats;
        static constexpr std::uint64_t maxBytes = 128ull << 20;
        static constexpr std::size_t maxEntries = 256;

        explicit MeshSurfaceStaticCache(IRenderDeviceServices& owner) : device(owner)
        {
            device.RegisterUploadTransactionListener(this);
        }
        ~MeshSurfaceStaticCache() override
        {
            device.UnregisterUploadTransactionListener(this);
        }

        void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override
        {
            std::lock_guard lock(mutex);
            if (auto found = recordings.find(recording); found != recordings.end())
            {
                for (auto& entry : found->second)
                {
                    entry->completion = std::max(entry->completion, completion.value);
                    if (!completion.IsValid())
                    {
                        entry->state = RHIUploadTransactionState::Quarantined;
                    }
                    else if (entry->state == RHIUploadTransactionState::Recording && entry->recording == recording)
                    {
                        entry->state = RHIUploadTransactionState::Queued;
                    }
                }
                recordings.erase(found);
            }
        }
        void OnUploadCompleted(std::uint64_t value) override
        {
            std::lock_guard lock(mutex);
            completed = std::max(completed, value);
            for (auto& entry : entries)
            {
                if (entry->state == RHIUploadTransactionState::Queued && entry->completion <= completed)
                {
                    entry->state = RHIUploadTransactionState::Resident;
                }
            }
        }
        void OnUploadAborted(std::uint64_t recording) override
        {
            std::lock_guard lock(mutex);
            recordings.erase(recording);
            std::erase_if(entries, [&](const auto& entry) {
                if (entry->recording != recording || entry->state != RHIUploadTransactionState::Recording)
                {
                    return false;
                }
                entry->usable = false;
                stats.residentBytes -= entry->bytes;
                return true;
            });
        }
        MeshSurfaceCacheStats Stats() const
        {
            std::lock_guard lock(mutex);
            auto result = stats;
            result.entries = entries.size();
            return result;
        }
        bool Acquire(std::shared_ptr<const MeshSurfaceInput> identity,
                     std::shared_ptr<MeshSurfaceStaticBuffers>& result, std::string& error)
        {
            const auto recording = device.GetCurrentUploadRecordingId();
            const auto& geometry = identity->Geometry();
            const std::uint64_t sizes[]{geometry.vertexBytes,
                                        std::uint64_t(geometry.indexCount) * sizeof(std::uint32_t),
                                        identity->Lods().size_bytes()};
            const auto bytes = sizes[0] + sizes[1] + sizes[2];
            {
                std::lock_guard lock(mutex);
                for (const auto& entry : entries)
                {
                    if (entry->identity == identity && entry->prepared &&
                        ((entry->state == RHIUploadTransactionState::Resident && entry->usable) ||
                         (entry->state == RHIUploadTransactionState::Recording && entry->recording == recording)))
                    {
                        recordings[recording].push_back(entry);
                        ++stats.hits;
                        result = entry;
                        return true;
                    }
                }
                if (bytes > maxBytes)
                {
                    return true; // Budget pressure uses the transient path.
                }
                for (auto it = entries.begin();
                     it != entries.end() && (stats.residentBytes + bytes > maxBytes || entries.size() >= maxEntries);)
                {
                    const auto& entry = *it;
                    if (entry.use_count() == 1 && entry->state == RHIUploadTransactionState::Resident &&
                        entry->completion <= completed)
                    {
                        stats.residentBytes -= entry->bytes;
                        it = entries.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                if (stats.residentBytes + bytes > maxBytes || entries.size() >= maxEntries)
                {
                    return true;
                }
            }
            auto entry = std::make_shared<MeshSurfaceStaticBuffers>(device);
            entry->identity = std::move(identity);
            entry->bytes = bytes;
            entry->recording = recording;
            RHIBufferSlice* destinations[]{&entry->vertices, &entry->indices, &entry->lods};
            const wchar_t* names[]{L"LX.Mesh.StaticVertices", L"LX.Mesh.StaticIndices", L"LX.Mesh.StaticLods"};
            RHIUploadRequest requests[3];
            for (std::size_t i = 0; i < 3; ++i)
            {
                RHIBufferDesc description;
                description.bytes = sizes[i];
                description.debugName = names[i];
                if (!device.CreateBuffer(description, destinations[i]->buffer, error))
                {
                    return false;
                }
                destinations[i]->size = sizes[i];
                requests[i] = {sizes[i], RHIUploadUsage::BufferCopy, 16};
            }
            if (!device.ReserveUploadBatch(requests, entry->staging, error) ||
                recording != device.GetCurrentUploadRecordingId())
            {
                return false;
            }
            const void* sources[]{geometry.vertexData, geometry.indexData, entry->identity->Lods().data()};
            for (std::size_t i = 0; i < 3; ++i)
            {
                if (!entry->staging[i].IsWritable())
                {
                    return Fail(error, "Static mesh staging allocation is not writable.");
                }
                std::memcpy(entry->staging[i].cpuAddress, sources[i], sizes[i]);
            }
            // Copies belong to the graph. Prepare runs AFTER PrepareParallel, so an
            // immediate-encoder copy here would execute after the worker consumers.
            {
                std::lock_guard lock(mutex);
                entries.push_back(entry);
                recordings[recording].push_back(entry);
                stats.residentBytes += bytes;
                entry->prepared = true;
                ++stats.uploads;
                stats.uploadedBytes += bytes;
            }
            result = std::move(entry);
            error.clear();
            return true;
        }
    };

    RHIBufferSlice MeshSurfaceBatch::Indices() const
    {
        return staticBuffers_ ? staticBuffers_->indices : RHIBufferSlice{};
    }

    MeshSurfaceCacheStats MeshSurfaceEvaluator::CacheStats() const
    {
        auto stats = staticCache_ ? staticCache_->Stats() : MeshSurfaceCacheStats{};
        stats.transforms = transforms_;
        stats.transformHits = transformHits_;
        stats.cachedOutputBytes = transformedBytes_;
        return stats;
    }

    struct MeshSurfaceBufferPool
    {
        struct Entry
        {
            std::uint64_t bytes{}, completion{};
            RHIBufferHandle buffer;
            RHIResourceState state{};
        };
        explicit MeshSurfaceBufferPool(IRenderDeviceServices& owner) : device(owner)
        {
        }
        ~MeshSurfaceBufferPool()
        {
            for (const auto& entry : idle)
            {
                device.ReleaseBuffer(entry.buffer);
            }
        }
        bool Acquire(std::uint64_t bytes, RHIBufferHandle& buffer, RHIResourceState& state)
        {
            std::lock_guard lock(mutex);
            for (auto it = idle.begin(); it != idle.end(); ++it)
            {
                if (it->bytes == bytes && it->completion <= completed.load(std::memory_order_acquire))
                {
                    buffer = it->buffer;
                    state = it->state;
                    idle.erase(it);
                    return true;
                }
            }
            return false;
        }
        bool Recycle(std::uint64_t bytes, RHIBufferHandle buffer, RHIResourceState state, RHICompletionPoint completion)
        {
            if (!completion.IsValid())
            {
                return false;
            }
            std::lock_guard lock(mutex);
            if (idle.size() >= 64)
            {
                return false;
            }
            idle.push_back({bytes, completion.value, buffer, state});
            return true;
        }
        void Completed(std::uint64_t value)
        {
            auto prior = completed.load(std::memory_order_relaxed);
            while (prior < value &&
                   !completed.compare_exchange_weak(prior, value, std::memory_order_release, std::memory_order_relaxed))
            {
            }
        }
        IRenderDeviceServices& device;
        std::mutex mutex;
        std::atomic<std::uint64_t> completed{};
        std::vector<Entry> idle;
    };

    bool MeshSurfaceInput::Seal(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                std::shared_ptr<const MeshSurfaceInput>& result, std::string& error)
    {
        return SealImpl(draw, view, lods, IblBaker::MaxPoints, result, error);
    }

    bool MeshSurfaceInput::SealImpl(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                    std::uint32_t maxVertices, std::shared_ptr<const MeshSurfaceInput>& result,
                                    std::string& error)
    {
        const auto& geometry = draw.modelMeshView;
        if (!geometry.IsComplete() || geometry.vertexStride != assets::StrideOf(geometry.vertexAttributeMask) ||
            geometry.vertexBytes / geometry.vertexStride > maxVertices || geometry.indexCount % 3 != 0 ||
            geometry.vertexBytes / geometry.vertexStride != lods.size() || !view.sceneEpoch || !view.viewRevision ||
            !view.geometryRevision || !Finite(view.eye) || !Affine(draw.worldMatrix) || !Finite(lods) ||
            !std::ranges::all_of(lods, [](float lod) { return lod >= 0 && lod <= 32; }))
        {
            return Fail(error,
                        "Mesh surface needs complete bounded product geometry, affine world, explicit LOD and view.");
        }
        const bool skin = assets::Has(geometry.vertexAttributeMask, assets::VertexAttribute::BoneIndices);
        if ((draw.boneCount != 0 && !draw.bonePalette) || draw.boneCount > 256 || (!skin && draw.boneCount != 0))
        {
            return Fail(error, "Mesh surface pose does not match its product vertex layout.");
        }
        auto candidate = std::shared_ptr<MeshSurfaceInput>(new MeshSurfaceInput);
        candidate->geometry_ = geometry;
        candidate->world_ = draw.worldMatrix;
        candidate->view_ = view;
        candidate->lods_.assign(lods.begin(), lods.end());
        const auto* vertices = static_cast<const std::byte*>(geometry.vertexData);
        candidate->vertices_.assign(vertices, vertices + geometry.vertexBytes);
        candidate->indices_.assign(geometry.indexData, geometry.indexData + geometry.indexCount);
        candidate->geometry_.vertexData = candidate->vertices_.data();
        candidate->geometry_.indexData = candidate->indices_.data();
        if (!std::ranges::all_of(candidate->indices_, [&](auto index) { return index < candidate->Count(); }))
        {
            return Fail(error, "Mesh surface triangle index exceeds the sealed vertex count.");
        }
        for (std::uint32_t i = 0; i < draw.boneCount; ++i)
        {
            if (!Affine(draw.bonePalette[i]))
            {
                return Fail(error, "Mesh surface bone palette contains a nonfinite/nonaffine matrix.");
            }
            candidate->bones_.push_back(PackedBoneMatrix::From(draw.bonePalette[i]));
        }
        for (std::uint32_t i = 0; i < candidate->Count(); ++i)
        {
            const auto* vertex = candidate->vertices_.data() + std::size_t(i) * geometry.vertexStride;
            const auto mask = geometry.vertexAttributeMask;
            for (const auto& attribute : assets::kVertexAttributeTable)
            {
                if (!assets::Has(mask, attribute.attribute) ||
                    attribute.attribute == assets::VertexAttribute::BoneIndices)
                {
                    continue;
                }
                const auto bytes = assets::SizeOf(attribute.format);
                std::array<float, 4> value{};
                std::memcpy(value.data(), vertex + assets::OffsetOf(mask, attribute.attribute), bytes);
                if (!Finite(value))
                {
                    return Fail(error, "Mesh surface vertex " + std::to_string(i) + " contains a nonfinite attribute.");
                }
            }
            const auto normal = Read<std::array<float, 3>>(vertex, mask, assets::VertexAttribute::Normal);
            const auto tangent = Read<std::array<float, 4>>(vertex, mask, assets::VertexAttribute::Tangent);
            if (!Direction(normal.data()) || (tangent[3] != -1.f && tangent[3] != 1.f))
            {
                return Fail(error, "Mesh surface vertex " + std::to_string(i) +
                                       " needs a nonzero normal and an authored handedness sign.");
            }
            if (skin)
            {
                const auto indices =
                    Read<std::array<std::uint8_t, 4>>(vertex, mask, assets::VertexAttribute::BoneIndices);
                const auto weights = Read<std::array<float, 4>>(vertex, mask, assets::VertexAttribute::BoneWeights);
                const float sum = weights[0] + weights[1] + weights[2] + weights[3];
                if (!std::ranges::all_of(weights, [](float weight) { return weight >= 0 && weight <= 1; }) ||
                    (sum != 0 && std::abs(sum - 1.f) > 1e-5f) || (sum > 0 && weights[0] <= 0))
                {
                    return Fail(error, "Mesh surface vertex " + std::to_string(i) + " has invalid skin weights.");
                }
                // Product vertices use 255 for unused influences. Only positive weights
                // reference the palette; the GPU must use the same rule before loading.
                for (std::size_t influence = 0; influence < weights.size(); ++influence)
                {
                    if (!candidate->bones_.empty() && weights[influence] > 0 &&
                        indices[influence] >= candidate->bones_.size())
                    {
                        return Fail(error, "Mesh surface vertex " + std::to_string(i) + " influence " +
                                               std::to_string(influence) + " exceeds the sealed bone palette.");
                    }
                }
            }
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool MeshSurfaceInput::Matches(const MeshSurfaceInput& other) const
    {
        const auto sameVertices =
            geometry_.vertexBytes == other.geometry_.vertexBytes &&
            std::memcmp(geometry_.vertexData, other.geometry_.vertexData, geometry_.vertexBytes) == 0;
        const auto sameIndices = geometry_.indexCount == other.geometry_.indexCount &&
                                 std::memcmp(geometry_.indexData, other.geometry_.indexData,
                                             std::size_t(geometry_.indexCount) * sizeof(std::uint32_t)) == 0;
        return geometry_.handle == other.geometry_.handle &&
               geometry_.vertexAttributeMask == other.geometry_.vertexAttributeMask && view_.eye == other.view_.eye &&
               view_.sceneEpoch == other.view_.sceneEpoch && view_.viewRevision == other.view_.viewRevision &&
               view_.geometryRevision == other.view_.geometryRevision &&
               std::memcmp(&world_, &other.world_, sizeof(world_)) == 0 && sameVertices && sameIndices &&
               std::ranges::equal(Lods(), other.Lods()) && Bones().size() == other.Bones().size() &&
               (Bones().empty() || std::memcmp(Bones().data(), other.Bones().data(), Bones().size_bytes()) == 0);
    }

    bool MeshSurfacePlan::Build(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                const MeshSurfacePlanBudget& budget, std::shared_ptr<const MeshSurfacePlan>& result,
                                std::string& error)
    {
        return BuildPartitioned(draw, view, lods, budget, IblBaker::MaxPoints, result, error);
    }

    bool MeshSurfacePlan::BuildPartitioned(const EnhancedDrawItem& draw, const SurfaceView& view,
                                           std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                           std::uint32_t chunkCeiling, std::shared_ptr<const MeshSurfacePlan>& result,
                                           std::string& error)
    {
        const auto& geometry = draw.modelMeshView;
        if (!geometry.IsComplete() || budget.maxChunkPoints < 3 || budget.maxChunkPoints > chunkCeiling ||
            !budget.maxChunks || geometry.vertexBytes / geometry.vertexStride > budget.maxSourceVertices ||
            geometry.indexCount % 3 != 0 || geometry.indexCount / 3 > budget.maxTriangles ||
            lods.size() != geometry.vertexBytes / geometry.vertexStride)
        {
            return Fail(error, "Mesh surface partition exceeds its source/chunk/triangle budget.");
        }
        auto candidate = std::shared_ptr<MeshSurfacePlan>(new MeshSurfacePlan);
        candidate->chunkLimit_ = budget.maxChunkPoints;
        auto& cost = candidate->cost_;
        cost.sourceVertices = static_cast<std::uint32_t>(geometry.vertexBytes / geometry.vertexStride);
        cost.triangles = geometry.indexCount / 3;
        const std::uint64_t boneBytes = std::uint64_t(draw.boneCount) * sizeof(PackedBoneMatrix);
        cost.cpuPayloadBytes = geometry.vertexBytes + std::uint64_t(geometry.indexCount) * sizeof(std::uint32_t) +
                               lods.size_bytes() + boneBytes;
        if (cost.cpuPayloadBytes > budget.maxCpuPayloadBytes)
        {
            return Fail(error, "Mesh surface partition source exceeds its CPU payload budget.");
        }
        if (!MeshSurfaceInput::SealImpl(draw, view, lods, budget.maxSourceVertices, candidate->source_, error))
        {
            return false;
        }
        const auto& source = *candidate->source_;
        const auto* sourceBytes = static_cast<const std::byte*>(source.Geometry().vertexData);
        constexpr auto unused = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> remap(cost.sourceVertices, unused);
        std::vector<bool> referenced(cost.sourceVertices);
        std::shared_ptr<MeshSurfaceInput> chunk;
        MeshSurfaceChunk description;
        const auto start = [&](std::uint32_t firstTriangle) {
            chunk = std::shared_ptr<MeshSurfaceInput>(new MeshSurfaceInput);
            chunk->geometry_ = source.geometry_;
            chunk->world_ = source.world_;
            chunk->view_ = source.view_;
            chunk->bones_ = source.bones_;
            description = {};
            description.firstTriangle = firstTriangle;
        };
        const auto finish = [&]() {
            chunk->geometry_.vertexData = chunk->vertices_.data();
            chunk->geometry_.vertexBytes = chunk->vertices_.size();
            chunk->geometry_.indexData = chunk->indices_.data();
            chunk->geometry_.indexCount = static_cast<std::uint32_t>(chunk->indices_.size());
            for (auto index : description.sourceVertices)
            {
                remap[index] = unused;
            }
            description.input = std::move(chunk);
            candidate->chunks_.push_back(std::move(description));
        };
        start(0);
        for (std::uint32_t triangle = 0; triangle < cost.triangles; ++triangle)
        {
            const std::array indices{source.indices_[std::size_t(triangle) * 3],
                                     source.indices_[std::size_t(triangle) * 3 + 1],
                                     source.indices_[std::size_t(triangle) * 3 + 2]};
            std::uint32_t additional = 0;
            for (std::size_t corner = 0; corner < indices.size(); ++corner)
            {
                if (remap[indices[corner]] == unused &&
                    std::find(indices.begin(), indices.begin() + corner, indices[corner]) == indices.begin() + corner)
                {
                    ++additional;
                }
            }
            if (chunk->Count() + additional > budget.maxChunkPoints)
            {
                finish();
                if (candidate->chunks_.size() >= budget.maxChunks)
                {
                    return Fail(error, "Mesh surface partition exceeds its chunk count budget.");
                }
                start(triangle);
                additional = static_cast<std::uint32_t>(indices[0] != indices[1] && indices[0] != indices[2]) +
                             static_cast<std::uint32_t>(indices[1] != indices[2]) + 1;
            }
            const std::uint64_t cpuAddition = std::uint64_t(additional) * (geometry.vertexStride + 8ull) + 12 +
                                              (chunk->indices_.empty() ? boneBytes : 0);
            // Payload only: source upload + world/material/bake buffers and index/LOD.
            // Descriptor allocation, alignment, texture residency and frame overlap
            // remain the native host's separate budget.
            const std::uint64_t gpuAddition =
                std::uint64_t(additional) * (geometry.vertexStride + sizeof(float) + sizeof(SurfacePoint) +
                                             sizeof(IblBakePoint) + sizeof(IblBakeSample)) +
                12 + (chunk->indices_.empty() ? std::max(boneBytes, std::uint64_t(sizeof(PackedBoneMatrix))) : 0);
            if (cpuAddition > budget.maxCpuPayloadBytes - cost.cpuPayloadBytes ||
                gpuAddition > budget.maxGpuPayloadBytes - cost.gpuPayloadBytes)
            {
                return Fail(error, "Mesh surface partition exceeds its CPU/GPU payload budget.");
            }
            cost.cpuPayloadBytes += cpuAddition;
            cost.gpuPayloadBytes += gpuAddition;
            for (auto index : indices)
            {
                if (remap[index] == unused)
                {
                    remap[index] = chunk->Count();
                    description.sourceVertices.push_back(index);
                    const auto* vertex = sourceBytes + std::size_t(index) * geometry.vertexStride;
                    chunk->vertices_.insert(chunk->vertices_.end(), vertex, vertex + geometry.vertexStride);
                    chunk->lods_.push_back(source.lods_[index]);
                    ++cost.points;
                    if (!referenced[index])
                    {
                        referenced[index] = true;
                        ++cost.referencedVertices;
                    }
                }
                chunk->indices_.push_back(remap[index]);
            }
        }
        finish();
        cost.duplicatedPoints = cost.points - cost.referencedVertices;
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool MeshSurfacePlan::Matches(const MeshSurfacePlan& other) const
    {
        return chunkLimit_ == other.chunkLimit_ && source_->Matches(*other.source_);
    }

    bool MeshSurfacePlan::BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                                        std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                        std::shared_ptr<const MeshSurfacePlan>& result, std::string& error)
    {
        // The raster scene computes the actual mip footprint from pixel UV
        // derivatives. Only its zero-initialized vertex LOD transport is reusable.
        if (!draw.modelMeshView.IsComplete() ||
            lods.size() != draw.modelMeshView.vertexBytes / draw.modelMeshView.vertexStride ||
            !std::ranges::all_of(lods, [](float value) { return value == 0.f; }))
        {
            return Build(draw, view, lods, budget, result, error);
        }
        return BuildSceneZeroLod(draw, view, lods, budget, result, error);
    }

    bool MeshSurfacePlan::BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                                        const MeshSurfacePlanBudget& budget,
                                        std::shared_ptr<const MeshSurfacePlan>& result, std::string& error)
    {
        return BuildSceneZeroLod(draw, view, {}, budget, result, error);
    }

    bool MeshSurfacePlan::BuildSceneZeroLod(const EnhancedDrawItem& draw, const SurfaceView& view,
                                            std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                            std::shared_ptr<const MeshSurfacePlan>& result, std::string& error)
    {
        ce::profile_scope profile{ce::marker<"MaterialMeshPlanForScene">()};

        struct Entry
        {
            std::shared_ptr<const MeshSurfacePlan> plan;
            MeshSurfacePlanBudget budget;
            const void* vertexData{};
            const std::uint32_t* indexData{};
        };
        static std::mutex cacheMutex;
        static std::map<assets::ModelMeshHandle, Entry> cache;
        static std::deque<assets::ModelMeshHandle> insertionOrder;
        static std::uint64_t cachedBytes{};
        constexpr std::uint64_t maxCachedBytes = 128ull << 20;
        constexpr std::size_t maxEntries = 64;

        const auto& geometry = draw.modelMeshView;
        const auto sameBudget = [](const MeshSurfacePlanBudget& a, const MeshSurfacePlanBudget& b) {
            return a.maxSourceVertices == b.maxSourceVertices && a.maxTriangles == b.maxTriangles &&
                   a.maxChunkPoints == b.maxChunkPoints && a.maxChunks == b.maxChunks &&
                   a.maxCpuPayloadBytes == b.maxCpuPayloadBytes && a.maxGpuPayloadBytes == b.maxGpuPayloadBytes;
        };
        std::shared_ptr<const MeshSurfacePlan> reusable;
        if (geometry.IsComplete())
        {
            std::lock_guard lock(cacheMutex);
            if (const auto found = cache.find(geometry.handle);
                found != cache.end() && sameBudget(found->second.budget, budget) &&
                found->second.vertexData == geometry.vertexData && found->second.indexData == geometry.indexData &&
                found->second.plan->source_->geometry_.vertexBytes == geometry.vertexBytes &&
                found->second.plan->source_->geometry_.indexCount == geometry.indexCount &&
                found->second.plan->source_->geometry_.vertexStride == geometry.vertexStride &&
                found->second.plan->source_->geometry_.vertexAttributeMask == geometry.vertexAttributeMask &&
                found->second.plan->source_->geometry_.vertexLayoutHash == geometry.vertexLayoutHash)
            {
                reusable = found->second.plan;
            }
        }
        if (!reusable)
        {
            std::vector<float> zeroLods;
            if (lods.empty() && geometry.IsComplete())
            {
                if (geometry.vertexBytes / geometry.vertexStride > budget.maxSourceVertices)
                {
                    return Fail(error, "Scene mesh exceeds its source vertex budget.");
                }
                zeroLods.resize(geometry.vertexBytes / geometry.vertexStride);
                lods = zeroLods;
            }
            // Raster consumes world points directly, not the bounded IBL point batch.
            // One million vertices also stays within the transform dispatch X limit.
            if (!BuildPartitioned(draw, view, lods, budget, 1u << 20, result, error))
            {
                return false;
            }
            if (result->cost_.cpuPayloadBytes <= maxCachedBytes)
            {
                std::lock_guard lock(cacheMutex);
                if (const auto found = cache.find(geometry.handle); found != cache.end())
                {
                    cachedBytes -= found->second.plan->cost_.cpuPayloadBytes;
                    cache.erase(found);
                    std::erase(insertionOrder, geometry.handle);
                }
                cache.emplace(geometry.handle, Entry{result, budget, geometry.vertexData, geometry.indexData});
                insertionOrder.push_back(geometry.handle);
                cachedBytes += result->cost_.cpuPayloadBytes;
                while (cachedBytes > maxCachedBytes || cache.size() > maxEntries)
                {
                    const auto oldest = insertionOrder.front();
                    insertionOrder.pop_front();
                    cachedBytes -= cache.at(oldest).plan->cost_.cpuPayloadBytes;
                    cache.erase(oldest);
                }
            }
            return true;
        }

        if (!Affine(draw.worldMatrix) || !view.sceneEpoch || !view.viewRevision || !view.geometryRevision ||
            !Finite(view.eye) || draw.boneCount > 256 || draw.boneCount != reusable->source_->bones_.size() ||
            (draw.boneCount && !draw.bonePalette))
        {
            return Fail(error, "Scene mesh reuse needs a valid current view and matching pose layout.");
        }

        auto candidate = std::shared_ptr<MeshSurfacePlan>(new MeshSurfacePlan);
        candidate->staticOwner_ = reusable;
        candidate->cost_ = reusable->cost_;
        candidate->chunkLimit_ = reusable->chunkLimit_;
        const auto bind = [&](const std::shared_ptr<const MeshSurfaceInput>& original) {
            auto input = std::shared_ptr<MeshSurfaceInput>(new MeshSurfaceInput);
            input->staticOwner_ = original;
            input->geometry_ = original->geometry_;
            input->world_ = draw.worldMatrix;
            input->view_ = view;
            input->poseOwner_ = candidate->source_;
            if (!input->poseOwner_)
            {
                input->bones_.reserve(draw.boneCount);
                for (std::uint32_t i = 0; i < draw.boneCount; ++i)
                {
                    input->bones_.push_back(PackedBoneMatrix::From(draw.bonePalette[i]));
                }
            }
            return input;
        };
        for (std::uint32_t i = 0; i < draw.boneCount; ++i)
        {
            if (!Affine(draw.bonePalette[i]))
            {
                return Fail(error, "Scene mesh reuse rejected a nonfinite/nonaffine bone pose.");
            }
        }
        candidate->source_ = bind(reusable->source_);
        candidate->chunks_.reserve(reusable->chunks_.size());
        for (const auto& previous : reusable->chunks_)
        {
            MeshSurfaceChunk chunk;
            chunk.input = bind(previous.input);
            chunk.sourceVertices = previous.sourceVertices;
            chunk.firstTriangle = previous.firstTriangle;
            candidate->chunks_.push_back(std::move(chunk));
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool ResolveSurfaceLod(const SurfaceTextureFootprint& footprint, float& lod, std::string& error)
    {
        if (!footprint.width || !footprint.height || footprint.width > 65536 || footprint.height > 65536 ||
            !footprint.mipLevels || footprint.mipLevels > std::bit_width(std::max(footprint.width, footprint.height)) ||
            !Finite(footprint.uvDx) || !Finite(footprint.uvDy) || !Finite({&footprint.bias, 1}))
        {
            return Fail(error, "Surface LOD needs finite UV derivatives and a valid texture mip extent.");
        }
        const double dx =
            std::hypot(double(footprint.uvDx[0]) * footprint.width, double(footprint.uvDx[1]) * footprint.height);
        const double dy =
            std::hypot(double(footprint.uvDy[0]) * footprint.width, double(footprint.uvDy[1]) * footprint.height);
        const double value = std::log2(std::max({dx, dy, 1e-30})) + footprint.bias;
        lod = static_cast<float>(std::clamp(value, 0.0, double(footprint.mipLevels - 1)));
        error.clear();
        return true;
    }

    MeshSurfaceBatch::~MeshSurfaceBatch()
    {
        if (transformedOwner_)
        {
            return;
        }
        if (device_ && buffer_.IsValid())
        {
            if (!bufferPool_ ||
                !bufferPool_->Recycle(std::uint64_t(count_) * sizeof(SurfacePoint), buffer_, bufferState_, completion_))
            {
                device_->ReleaseBuffer(buffer_);
            }
        }
    }

    bool MeshSurfaceBatch::ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const
    {
        if (!IsReadyForEvaluation() || points.size() != Count() || (source_ && !source_->IsValidated()))
        {
            return Fail(error, "Mesh surface readback needs its exact count and accepted source geometry.");
        }
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto& point = points[i];
            const auto& eye = input_->View().eye;
            if (!Finite(std::bit_cast<std::array<float, 20>>(point)) || point.position[3] != 0 ||
                point.uvLod[3] != (IsSampled() ? samples_[i].lod : input_->Lods()[i]) ||
                !Direction(point.normal.data()) || !ValidSurfaceTangentPair(point) ||
                std::hypot(double(eye[0]) - point.position[0], double(eye[1]) - point.position[1],
                           double(eye[2]) - point.position[2]) <= 1e-10)
            {
                return Fail(error, std::string("Mesh surface rejected ") + (IsSampled() ? "sample " : "vertex ") +
                                       std::to_string(i) + ": invalid transformed frame/view.");
            }
        }
        validated_ = true;
        error.clear();
        return true;
    }

    bool MeshSurfaceEvaluator::Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                                          IRenderPipelineCache& pipelines, const RHIShaderBlob& shader,
                                          std::string& error, LX::Runtime::ComputeShaderDescription identity)
    {
        if ((device_ && device_ != &device) || !shader.IsValid())
        {
            return Fail(error, "Mesh surface evaluator needs a compiled host artifact on its owning device.");
        }
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
                                                  RHILayout::Srv(2), RHILayout::UavBufferTable(1, 0)};
        RHIPipelineLayoutDesc layoutDescription;
        layoutDescription.params = parameters;
        const auto layout = roots.GetOrCreate(layoutDescription, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIComputePipelineDesc description;
        description.layout = layout;
        description.csBytecode = shader.Data();
        description.csSize = shader.Size();
        LX::Runtime::ComputePipeline pipeline;
        if (!pipeline.Create(pipelines, description, std::move(identity), error))
        {
            return false;
        }
        device_ = &device;
        pipeline_ = pipeline.GetGeneration();
        if (!bufferPool_)
        {
            bufferPool_ = std::make_shared<MeshSurfaceBufferPool>(device);
        }
        error.clear();
        return true;
    }

    void MeshSurfaceEvaluator::NotifyCompleted(std::uint64_t completed) const
    {
        if (bufferPool_)
        {
            bufferPool_->Completed(completed);
        }
    }

    bool MeshSurfaceEvaluator::Record(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceInput> input,
                                      std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error)
    {
        std::shared_ptr<const MeshSurfaceBatch> candidate;
        if (!Prepare(device, std::move(input), candidate, error))
        {
            return false;
        }
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{candidate->buffer_, RHIResourceState::Common,
                                         RHIResourceState::UnorderedAccess};
        encoder.ResourceBarriers({{}, {&before, 1}});
        if (!candidate->RecordCommands(encoder, error))
        {
            return false;
        }
        const RHIBufferTransition after{candidate->buffer_, RHIResourceState::UnorderedAccess,
                                        RHIResourceState::ShaderResource};
        encoder.ResourceBarriers({{}, {&after, 1}});
        candidate->recordedStages_.fetch_or(2);
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool MeshSurfaceEvaluator::Prepare(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceInput> input,
                                       std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error,
                                       bool cacheStatic)
    {
        ce::profile_scope profile{ce::marker<"MaterialMeshGpuPrepare">()};
        if (device_ != &device || !(pipeline_ && pipeline_->IsValid()) || !device.GetCurrentUploadRecordingId() ||
            !input)
        {
            return Fail(error, "Mesh surface transform needs a sealed input on its initialized device in a recording.");
        }
        auto candidate = std::shared_ptr<MeshSurfaceBatch>(new MeshSurfaceBatch);
        candidate->device_ = &device;
        candidate->input_ = std::move(input);
        candidate->count_ = candidate->input_->Count();
        candidate->recordingId_ = device.GetCurrentUploadRecordingId();
        candidate->descriptorVersion_ = device.GetDescriptorVersionToken();
        candidate->pipeline_ = pipeline_;
        const auto& source = *candidate->input_;
        if (cacheStatic)
        {
            if (!staticCache_)
            {
                staticCache_ = std::make_shared<MeshSurfaceStaticCache>(device);
            }
            auto identity = candidate->input_;
            while (identity->staticOwner_)
            {
                identity = identity->staticOwner_;
            }
            if (!staticCache_->Acquire(std::move(identity), candidate->staticBuffers_, error))
            {
                return false;
            }
            if (candidate->staticBuffers_ && bufferPool_)
            {
                for (const auto& previous : transformedCache_)
                {
                    const auto& prior = *previous->input_;
                    if (previous->staticBuffers_->identity != candidate->staticBuffers_->identity ||
                        previous->pipeline_ != pipeline_ || !previous->IsReadyForEvaluation() ||
                        !previous->completion_.IsValid() ||
                        previous->completion_.value > bufferPool_->completed.load(std::memory_order_acquire) ||
                        previous->bufferState_ != RHIResourceState::ShaderResource ||
                        previous->lastReuseRecording_ == candidate->recordingId_ ||
                        std::memcmp(&prior.World(), &source.World(), sizeof(math::matrix4x4)) != 0 ||
                        prior.Bones().size() != source.Bones().size() ||
                        (!source.Bones().empty() &&
                         std::memcmp(prior.Bones().data(), source.Bones().data(), source.Bones().size_bytes()) != 0))
                    {
                        continue;
                    }
                    // Eye/view revision belongs to the fresh frame input: this kernel
                    // produces world geometry only and never reads the camera.
                    candidate->transformedOwner_ = previous;
                    candidate->buffer_ = previous->buffer_;
                    candidate->self_ = candidate;
                    previous->lastReuseRecording_ = candidate->recordingId_;
                    ++transformHits_;
                    result = std::move(candidate);
                    error.clear();
                    return true;
                }
            }
        }
        RHIBufferDesc description;
        description.bytes = candidate->Count() * sizeof(SurfacePoint);
        description.allowUnorderedAccess = true;
        description.debugName = L"LX.Material.MeshWorldPoints";
        candidate->bufferPool_ = bufferPool_;
        if ((!candidate->bufferPool_ ||
             !candidate->bufferPool_->Acquire(description.bytes, candidate->buffer_, candidate->bufferState_)) &&
            !device.CreateBuffer(description, candidate->buffer_, error))
        {
            return false;
        }
        struct Constants
        {
            math::matrix4x4 world;
            std::array<std::uint32_t, 4> countStrideMaskBones;
            std::array<std::uint32_t, 8> offsets;
        };
        Constants constants{source.World(),
                            {candidate->Count(), source.Geometry().vertexStride, source.Geometry().vertexAttributeMask,
                             static_cast<std::uint32_t>(source.Bones().size())},
                            {}};
        for (const auto& attribute : assets::kVertexAttributeTable)
        {
            constants.offsets[static_cast<std::size_t>(attribute.attribute)] =
                assets::OffsetOf(source.Geometry().vertexAttributeMask, attribute.attribute);
        }
        const auto vertex = candidate->staticBuffers_
                                ? candidate->staticBuffers_->vertices
                                : device.AllocateUpload({source.Geometry().vertexBytes, RHIUploadUsage::Raw, 16});
        const auto boneBytes = std::max(sizeof(PackedBoneMatrix), source.Bones().size_bytes());
        const auto bones = device.AllocateUpload({boneBytes, RHIUploadUsage::Raw, 16});
        const auto lods = candidate->staticBuffers_
                              ? candidate->staticBuffers_->lods
                              : device.AllocateUpload({source.Lods().size_bytes(), RHIUploadUsage::Raw, 16});
        const auto uniform = device.UploadConstants(&constants, sizeof(constants));
        const auto output = RHIBindingDesc::UavBuffer(candidate->buffer_, candidate->Count(), sizeof(SurfacePoint));
        const auto table = device.CreateBindings({&output, 1});
        if (!vertex.IsValid() || !bones.IsWritable() || !lods.IsValid() || !uniform.IsValid() || !table.IsValid() ||
            candidate->recordingId_ != device.GetCurrentUploadRecordingId() ||
            candidate->descriptorVersion_ != device.GetDescriptorVersionToken())
        {
            return Fail(error, "Mesh surface allocation failed or changed recording/descriptors; retry with the "
                               "accepted batch retained.");
        }
        if (!candidate->staticBuffers_)
        {
            if (!vertex.IsWritable() || !lods.IsWritable())
            {
                return Fail(error, "Mesh input upload is not writable.");
            }
            std::memcpy(vertex.cpuAddress, source.Geometry().vertexData, source.Geometry().vertexBytes);
            std::memcpy(lods.cpuAddress, source.Lods().data(), source.Lods().size_bytes());
        }
        if (source.Bones().empty())
        {
            const auto identity = PackedBoneMatrix::Identity();
            std::memcpy(bones.cpuAddress, &identity, sizeof(identity));
        }
        else
        {
            std::memcpy(bones.cpuAddress, source.Bones().data(), source.Bones().size_bytes());
        }
        candidate->vertices_ = vertex;
        candidate->bones_ = bones;
        candidate->lods_ = lods;
        candidate->uniform_ = uniform;
        candidate->output_ = table;
        candidate->self_ = candidate;
        if (cacheStatic && candidate->staticBuffers_)
        {
            // Eviction drops only the cache reference. Submitted frames retain their
            // owners until completion, including any later readers of this output.
            constexpr std::uint64_t maxOutputBytes = 32ull << 20;
            while (!transformedCache_.empty() &&
                   (transformedCache_.size() >= 256 || transformedBytes_ + description.bytes > maxOutputBytes))
            {
                transformedBytes_ -= std::uint64_t(transformedCache_.front()->Count()) * sizeof(SurfacePoint);
                transformedCache_.erase(transformedCache_.begin());
            }
            if (description.bytes <= maxOutputBytes)
            {
                transformedCache_.push_back(candidate);
                transformedBytes_ += description.bytes;
            }
            ++transforms_;
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool MeshSurfaceBatch::IsPreparedForGraph() const
    {
        return (pipeline_ && pipeline_->IsValid()) && (recordedStages_.load() & 4) == 0 && recordingId_ != 0 &&
               device_->GetCurrentUploadRecordingId() == recordingId_ &&
               device_->GetDescriptorVersionToken() == descriptorVersion_;
    }

    RGHandle MeshSurfaceBatch::GraphOutput(const EnhancedRenderGraph& graph) const
    {
        return graph_ == &graph && graphEpoch_ == graph.ResourceEpoch() &&
                       graph.ResolveBufferHandle(graphOutput_) == buffer_
                   ? graphOutput_
                   : RGHandle{};
    }

    bool MeshSurfaceBatch::RecordCommands(RHIEncoder& encoder, std::string& error) const
    {
        if (!IsPreparedForGraph() || (recordedStages_.fetch_or(1) & 1) != 0)
        {
            recordedStages_.fetch_or(4);
            return Fail(error, "Mesh transform has stale uploads/descriptors or was already recorded.");
        }
        encoder.SetPipeline(RHIBindPoint::Compute, pipeline_->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, vertices_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 2, bones_);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 3, lods_);
        encoder.SetBindings(RHIBindPoint::Compute, 4, output_);
        encoder.Dispatch((Count() + 31) / 32, 1, 1);
        error.clear();
        return true;
    }

    bool MeshSurfaceBatch::Declare(EnhancedRenderGraph& graph, std::string& error) const
    {
        const auto owner = self_.lock();
        if (!owner || &graph.DeviceServices() != device_ || !IsPreparedForGraph() || recordedStages_.load() != 0 ||
            declared_.exchange(true))
        {
            return Fail(error, "Mesh declaration needs a fresh prepared world transform on its owning recording.");
        }
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        graph_ = &graph;
        graphEpoch_ = graph.ResourceEpoch();
        std::vector<EnhancedRenderGraph::RGPassUsage> inputs;
        if (staticBuffers_)
        {
            auto& buffers = *staticBuffers_;
            if (buffers.graph != &graph || buffers.graphEpoch != graphEpoch_)
            {
                buffers.graph = &graph;
                buffers.graphEpoch = graphEpoch_;
                const bool needsCopy = !buffers.usable;
                const RHIBufferHandle handles[]{buffers.vertices.buffer, buffers.indices.buffer, buffers.lods.buffer};
                for (unsigned i = 0; i < 3; ++i)
                {
                    buffers.graphBuffers[i] = graph.ImportBuffer(
                        handles[i],
                        needsCopy ? RHIResourceState::Common
                                  : (i == 1 ? RHIResourceState::IndexBuffer : RHIResourceState::ShaderResource),
                        "LX.Mesh.StaticInput");
                }
                if (needsCopy)
                {
                    if (buffers.recording != recordingId_)
                    {
                        return Fail(error, "Static mesh staging belongs to another recording.");
                    }
                    if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
                    {
                        for (auto& buffer : buffers.graphBuffers)
                        {
                            buffer = graph.Write(buffer);
                        }
                    }
                    graph.AddPass("LX.MeshInputUpload",
                                  {{buffers.graphBuffers[0], RHIResourceState::CopyDest, writeAccess},
                                   {buffers.graphBuffers[1], RHIResourceState::CopyDest, writeAccess},
                                   {buffers.graphBuffers[2], RHIResourceState::CopyDest, writeAccess}},
                                  [retained = staticBuffers_](const auto& context) {
                                      const RHIBufferSlice destinations[]{retained->vertices, retained->indices,
                                                                          retained->lods};
                                      for (unsigned i = 0; i < 3; ++i)
                                      {
                                          if (!context.encoder->CopyBuffer(destinations[i], retained->staging[i]))
                                          {
                                              throw std::runtime_error("Static mesh buffer copy was rejected.");
                                          }
                                      }
                                      retained->usable = true;
                                  });
                }
            }
            // Index reads are represented as well: downstream raster passes depend
            // on this world output, so their index buffer is ready on every backend.
            for (unsigned i = 0; i < 3; ++i)
            {
                inputs.push_back({buffers.graphBuffers[i],
                                  i == 1 ? RHIResourceState::IndexBuffer : RHIResourceState::ShaderResource,
                                  readAccess});
            }
        }
        auto& state = transformedOwner_ ? transformedOwner_->bufferState_ : bufferState_;
        graphOutput_ = graph.ImportBuffer(buffer_, state, "LX.Mesh.WorldPoints", &state);
        if (transformedOwner_)
        {
            inputs.push_back({graphOutput_, RHIResourceState::ShaderResource, readAccess});
            graph.AddPass(
                "LX.MeshWorldReuse", inputs,
                [owner](const auto&) {
                    if (!owner->IsPreparedForGraph())
                    {
                        owner->recordedStages_.fetch_or(4);
                        throw std::runtime_error("Stale reused mesh recording.");
                    }
                    owner->recordedStages_.store(3);
                },
                true);
            error.clear();
            return true;
        }
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            graphOutput_ = graph.Write(graphOutput_);
        }
        inputs.push_back({graphOutput_, RHIResourceState::UnorderedAccess, writeAccess});
        graph.AddPass("LX.TransformMeshWorld", inputs, [owner](const auto& context) {
            std::string failure;
            if (!context.graph || !context.encoder || !owner->GraphOutput(*context.graph).IsValid() ||
                !owner->RecordCommands(*context.encoder, failure))
            {
                owner->recordedStages_.fetch_or(4);
                throw std::runtime_error("Mesh graph recording failed: " + failure);
            }
        });
        graph.AddPass(
            "LX.MeshWorldReady", {{graphOutput_, RHIResourceState::ShaderResource, readAccess}},
            [owner](const auto&) {
                if (!owner->IsPreparedForGraph())
                {
                    owner->recordedStages_.fetch_or(4);
                    throw std::runtime_error("Stale mesh readiness recording.");
                }
                owner->recordedStages_.fetch_or(2);
            },
            true);
        error.clear();
        return true;
    }

    bool MeshSurfaceEvaluator::InitializeSampler(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                                                 IRenderPipelineCache& pipelines, const RHIShaderBlob& shader,
                                                 std::string& error, LX::Runtime::ComputeShaderDescription identity)
    {
        if (device_ != &device || !(pipeline_ && pipeline_->IsValid()) || !shader.IsValid())
        {
            return Fail(error, "Mesh surface sampler needs a compiled artifact on its initialized transform device.");
        }
        const RHIPipelineLayoutParam parameters[]{RHILayout::Cbv(2), RHILayout::Srv(4), RHILayout::Srv(5),
                                                  RHILayout::Srv(6), RHILayout::UavBufferTable(1, 1)};
        const auto layout = roots.GetOrCreate({parameters}, error);
        if (!layout.IsValid())
        {
            return false;
        }
        RHIComputePipelineDesc description;
        description.layout = layout;
        description.csBytecode = shader.Data();
        description.csSize = shader.Size();
        LX::Runtime::ComputePipeline pipeline;
        if (!pipeline.Create(pipelines, description, std::move(identity), error))
        {
            return false;
        }
        samplerPipeline_ = pipeline.GetGeneration();
        error.clear();
        return true;
    }

    bool MeshSurfaceEvaluator::RecordSamples(IRenderDeviceServices& device,
                                             std::shared_ptr<const MeshSurfaceBatch> vertices,
                                             std::span<const MeshSurfaceSample> samples,
                                             std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error)
    {
        if (device_ != &device || !(samplerPipeline_ && samplerPipeline_->IsValid()) ||
            !device.GetCurrentUploadRecordingId() || !vertices || vertices->Device() != &device ||
            !vertices->Buffer().IsValid() || !vertices->IsReadyForEvaluation() || vertices->IsSampled() ||
            samples.empty() || samples.size() > IblBaker::MaxPoints ||
            (vertices->RecordingId() != device.GetCurrentUploadRecordingId() && !vertices->IsValidated()))
        {
            return Fail(error,
                        "Mesh surface sampling needs bounded triangle requests and an accepted/current vertex source.");
        }
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto& sample = samples[i];
            if (sample.triangle >= vertices->Input()->Geometry().indexCount / 3 ||
                !Finite(std::array{sample.b, sample.c, sample.lod}) || sample.b < 0 || sample.c < 0 ||
                sample.b + sample.c > 1 || sample.lod < 0 || sample.lod > 32)
            {
                return Fail(error,
                            "Mesh surface sample " + std::to_string(i) + " has an invalid triangle, weight or LOD.");
            }
        }
        auto candidate = std::shared_ptr<MeshSurfaceBatch>(new MeshSurfaceBatch);
        candidate->device_ = &device;
        candidate->input_ = vertices->Input();
        candidate->source_ = std::move(vertices);
        candidate->samplePipeline_ = samplerPipeline_;
        candidate->samples_.assign(samples.begin(), samples.end());
        candidate->count_ = static_cast<std::uint32_t>(samples.size());
        candidate->recordingId_ = device.GetCurrentUploadRecordingId();
        const auto descriptorVersion = device.GetDescriptorVersionToken();
        RHIBufferDesc description;
        description.bytes = candidate->Count() * sizeof(SurfacePoint);
        description.allowUnorderedAccess = true;
        description.debugName = L"LX.Material.TriangleSamples";
        if (!device.CreateBuffer(description, candidate->buffer_, error))
        {
            return false;
        }
        const auto& geometry = candidate->input_->Geometry();
        const auto indexBytes = std::uint64_t(geometry.indexCount) * sizeof(std::uint32_t);
        const auto indices = device.AllocateUpload({indexBytes, RHIUploadUsage::Raw, 16});
        const auto requests = device.AllocateUpload({samples.size_bytes(), RHIUploadUsage::Raw, 16});
        const std::array<std::uint32_t, 4> constants{candidate->Count(), 0, 0, 0};
        const auto uniform = device.UploadConstants(constants.data(), sizeof(constants));
        const auto output = RHIBindingDesc::UavBuffer(candidate->buffer_, candidate->Count(), sizeof(SurfacePoint));
        const auto table = device.CreateBindings({&output, 1});
        if (!indices.IsWritable() || !requests.IsWritable() || !uniform.IsValid() || !table.IsValid() ||
            candidate->recordingId_ != device.GetCurrentUploadRecordingId() ||
            descriptorVersion != device.GetDescriptorVersionToken())
        {
            return Fail(error,
                        "Mesh surface sample allocation changed recording/descriptors; retain and retry the source.");
        }
        std::memcpy(indices.cpuAddress, geometry.indexData, indexBytes);
        std::memcpy(requests.cpuAddress, candidate->samples_.data(), samples.size_bytes());
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{candidate->buffer_, RHIResourceState::Common,
                                         RHIResourceState::UnorderedAccess};
        encoder.ResourceBarriers({{}, {&before, 1}});
        encoder.SetPipeline(RHIBindPoint::Compute, samplerPipeline_->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, uniform);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, RHIBufferSlice::Whole(candidate->source_->Buffer()));
        encoder.SetRootBuffer(RHIBindPoint::Compute, 2, indices);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 3, requests);
        encoder.SetBindings(RHIBindPoint::Compute, 4, table);
        encoder.Dispatch((candidate->Count() + 31) / 32, 1, 1);
        const RHIBufferTransition after{candidate->buffer_, RHIResourceState::UnorderedAccess,
                                        RHIResourceState::ShaderResource};
        encoder.ResourceBarriers({{}, {&after, 1}});
        candidate->recordedStages_.store(3);
        result = std::move(candidate);
        error.clear();
        return true;
    }
} // namespace material_graph
