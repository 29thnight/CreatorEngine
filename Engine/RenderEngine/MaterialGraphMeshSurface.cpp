#include "../Utility_Framework/Ownership.h"
#include "../Utility_Framework/DynamicBitset.h"
#include "MaterialGraphMeshSurface.h"
#include "MeshTransformDeclarationPlan.h"
#include "Assets/QuantizedGeometry.h"
#include "MaterialGraphSceneAccess.h"
#include "RHI/RHIModelMeshletUpload.h"
#include "RHI/RHISharedSparsePagePool.h"
#include "Render/Graph/RenderGraphDeclarationTask.h"
#include "Experiment/Import/MeshLodBuilder.h"
#include "../EngineDiagnostics/ProfileScope.h"

#include <optional>
#include <stdint.h>
#include <stdio.h>
#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
    namespace
    {
        struct ScenePlanCache
        {
            struct Entry
            {
                own::shared_owner<const MeshSurfacePlan> plan;
                MeshSurfacePlanBudget budget;
                const void* vertexData{};
                const std::uint32_t* indexData{};
            };
            using Key = std::pair<assets::ModelMeshHandle, uint32_t>;

            std::mutex mutex;
            std::map<Key, Entry> entries;
            std::deque<Key> insertionOrder;
            std::uint64_t bytes{};
        };

        ScenePlanCache& GetScenePlanCache()
        {
            static ScenePlanCache cache;
            return cache;
        }

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

        template<class T>
        T Read(const std::byte* vertex, std::uint32_t mask, assets::VertexAttribute attribute)
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

#include "MeshletStreamingTopology.inl"
#include "MeshletSparseStorage.inl"

    // Only the CPU prefetch order is cached. Stream::Prepare must still observe
    // every recording/view so feedback, pending reads and residency budgets advance.
    // An exact immutable partition plus world/eye values determines this order;
    // frame/view revisions alone would discard it even for a stationary camera.
    struct MeshSurfaceRequestCache
    {
        using Key = std::tuple<std::uintptr_t, std::uint64_t, std::uint64_t, std::array<std::uint32_t, 16>,
                               std::array<std::uint32_t, 4>>;
        struct Entry
        {
            own::weak_owner<const MeshSurfaceInput> identity;
            own::shared_owner<const std::vector<std::uint32_t>> requests;
            std::uint64_t generation{}, lastUse{};
        };
        std::map<Key, Entry> entries;
        std::uint64_t bytes{}, clock{};
        static constexpr std::uint64_t maxBytes = 8ull << 20;
        static constexpr std::size_t maxEntries = 1024;

        void Trim(std::uint64_t generation)
        {
            std::erase_if(entries,
                          [&](const auto& item)
                          {
                              if (!item.second.identity.expired() && generation - item.second.generation <= 2)
                              {
                                  return false;
                              }
                              bytes -= item.second.requests->size() * sizeof(std::uint32_t);
                              return true;
                          });
        }

        own::shared_owner<const std::vector<std::uint32_t>> Acquire(
            const own::shared_owner<const MeshSurfaceInput>& identity, const MeshSurfaceInput& input, std::uint64_t view,
            std::uint64_t generation)
        {
            const Key key{reinterpret_cast<std::uintptr_t>(identity.operator->()), view, input.View().sceneEpoch,
                          std::bit_cast<std::array<std::uint32_t, 16>>(input.World()),
                          std::bit_cast<std::array<std::uint32_t, 4>>(input.View().eye)};
            if (const auto found = entries.find(key); found != entries.end())
            {
                // The live caller pins the partition. Compare control-block identity
                // without a temporary strong lock; recycled addresses cannot alias it.
                if (own::identity(found->second.identity) == own::identity(identity))
                {
                    found->second.generation = generation;
                    found->second.lastUse = ++clock;
                    return found->second.requests;
                }
                bytes -= found->second.requests->size() * sizeof(std::uint32_t);
                entries.erase(found);
            }

            ce::profile_scope sortProfile{ce::marker<"MaterialClusterRequestSort">()};
            const auto count = input.Hierarchy().groupCount ? input.Hierarchy().groupCount : 1;
            const auto& groups = input.Hierarchy().groups;
            auto requests = own::make_shared<std::vector<std::uint32_t>>(count);
            std::vector<double> distances(count);
            for (std::uint32_t group = 0; group < count; ++group)
            {
                (*requests)[group] = group;
                if (groups.empty())
                {
                    continue;
                }
                const auto& sphere = groups[group].sphere;
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    double world = input.World().m[3][axis];
                    for (unsigned i = 0; i < 3; ++i)
                    {
                        world += sphere[i] * double(input.World().m[i][axis]);
                    }
                    const auto delta = world - input.View().eye[axis];
                    distances[group] += delta * delta;
                }
            }
            // Compute each world-space distance once. Stable ordering retains the
            // old ascending group priority when distances tie.
            std::stable_sort(requests->begin(), requests->end(),
                             [&](auto a, auto b) { return distances[a] < distances[b]; });
            const auto charge = requests->size() * sizeof(std::uint32_t);
            if (charge <= maxBytes)
            {
                while (!entries.empty() && (entries.size() >= maxEntries || bytes + charge > maxBytes))
                {
                    const auto oldest =
                        std::ranges::min_element(entries, {}, [](const auto& item) { return item.second.lastUse; });
                    bytes -= oldest->second.requests->size() * sizeof(std::uint32_t);
                    entries.erase(oldest);
                }
                entries.emplace(key, Entry{identity, requests, generation, ++clock});
                bytes += charge;
            }
            return requests;
        }
    };

    struct MeshSurfaceStaticBuffers
    {
        IRenderDeviceServices& device;
        own::shared_owner<const MeshSurfaceInput> identity;
        own::shared_owner<const assets::cluster::ResidentSet> residency;
        own::shared_owner<MeshletSparseStorage> sparse;
        std::vector<std::pair<RHIBufferSlice, RHIBufferSlice>> sparseCopies;
        SparseClusterPayload sparsePayload;
        std::vector<std::byte> sparseMetadata;
        uint64_t sparseRevision{}, sparseSerial{};
        RHIBufferSlice vertices, indices, lods, meshlets;
        uint32_t meshletCount{};
        std::array<RHIBufferSlice, 4> staging;
        const EnhancedRenderGraph* graph{};
        std::uint64_t graphEpoch{};
        std::array<RGHandle, 4> graphBuffers;
        std::uint64_t bytes{}, recording{}, completion{};
        RHIUploadTransactionState state{RHIUploadTransactionState::Recording};
        bool prepared{};
        std::atomic<bool> usable{};
        explicit MeshSurfaceStaticBuffers(IRenderDeviceServices& owner) : device(owner) {}
        ~MeshSurfaceStaticBuffers()
        {
            for (auto buffer :
                 {vertices.buffer, indices.buffer, lods.buffer, sparse ? RHIBufferHandle{} : meshlets.buffer})
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
        std::vector<own::shared_owner<MeshSurfaceStaticBuffers>> entries;
        // Guarded by mutex; reuse storage while checking every current owner.
        std::vector<std::pair<MeshletSparseStorage*, bool>> idleScratch;
        std::map<std::uint64_t, std::vector<own::shared_owner<MeshSurfaceStaticBuffers>>> recordings;
        struct ProvisionalEntry
        {
            own::shared_owner<MeshSurfaceStaticBuffers> entry;
            std::uint64_t previousCompletion{};
            bool initialUpload{};
        };
        struct ProvisionalSubmission
        {
            RHICompletionPoint completion;
            std::vector<ProvisionalEntry> entries;
        };
        std::map<std::uint64_t, ProvisionalSubmission> provisional;
        std::uint64_t completed{};
        MeshSurfaceCacheStats stats;
        static constexpr std::uint64_t maxBytes = 128ull << 20;
        static constexpr std::size_t maxEntries = 256;

        explicit MeshSurfaceStaticCache(IRenderDeviceServices& owner) : device(owner)
        {
            device.RegisterUploadTransactionListener(this);
        }
        ~MeshSurfaceStaticCache() override { device.UnregisterUploadTransactionListener(this); }

        void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override
        {
            std::lock_guard lock(mutex);
            if (auto found = recordings.find(recording); found != recordings.end())
            {
                auto& submission = provisional[recording];
                submission.completion = completion;
                for (auto& entry : found->second)
                {
                    if (std::ranges::none_of(submission.entries,
                                             [&](const auto& prior) { return prior.entry.operator->() == entry.operator->(); }))
                    {
                        submission.entries.push_back(
                            {entry, entry->completion,
                             entry->state == RHIUploadTransactionState::Recording && entry->recording == recording});
                    }
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
                    if (entry->sparse)
                    {
                        entry->sparse->Commit(entry->sparseSerial, entry->sparseRevision, entry->sparsePayload,
                                              entry->sparseMetadata);
                    }
                }
            }
            std::erase_if(provisional, [&](const auto& item)
                          { return item.second.completion.IsValid() && item.second.completion.value <= completed; });
        }
        void OnUploadAborted(std::uint64_t recording) override
        {
            std::lock_guard lock(mutex);
            recordings.erase(recording);
            std::erase_if(entries,
                          [&](const auto& entry)
                          {
                              if (entry->recording != recording || entry->state != RHIUploadTransactionState::Recording)
                              {
                                  return false;
                              }
                              entry->usable = false;
                              stats.residentBytes -= entry->bytes;
                              return true;
                          });
        }
        void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion) override
        {
            std::lock_guard lock(mutex);
            const auto found = provisional.find(recording);
            if (found != provisional.end() && found->second.completion.IsValid() &&
                found->second.completion.value != completion.value)
            {
                return;
            }
            if (found != provisional.end())
            {
                for (const auto& pending : found->second.entries)
                {
                    const auto& entry = pending.entry;
                    if (!pending.initialUpload && entry->completion == completion.value)
                    {
                        // 이미 resident인 입력의 거절된 read lease는 이전 사용
                        // 완료점만 복원한다. 실제 자산 내용은 폐기하지 않는다.
                        entry->completion = pending.previousCompletion;
                    }
                }
                provisional.erase(found);
            }
            // Submitted 통지 도중 예외가 난 경우 아직 Recording인 입력도
            // 같은 never-admitted R의 일부다. 완료된 기존 자산은 건드리지 않는다.
            std::erase_if(entries,
                          [&](const auto& entry)
                          {
                              if (entry->recording != recording)
                              {
                                  return false;
                              }
                              entry->usable = false;
                              entry->state = RHIUploadTransactionState::Quarantined;
                              stats.residentBytes -= entry->bytes;
                              return true;
                          });
            recordings.erase(recording);
        }
        MeshSurfaceCacheStats Stats() const
        {
            std::lock_guard lock(mutex);
            auto result = stats;
            result.entries = entries.size();
            return result;
        }
        void TrimIdle()
        {
            std::lock_guard lock(mutex);
            const bool pressure = device.StreamingMemoryPressure();
            for (auto it = entries.begin();
                 it != entries.end() && (pressure || stats.residentBytes > maxBytes || entries.size() > maxEntries);)
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
        }
        bool Acquire(own::shared_owner<const MeshSurfaceInput> identity,
                     own::shared_owner<MeshSurfaceStaticBuffers>& result, std::string& error,
                     own::shared_owner<const assets::cluster::ResidentSet> residency = {})
        {
            const auto recording = device.GetCurrentUploadRecordingId();
            const auto& geometry = identity->Geometry();
            RHIModelMeshletUpload topology;
            SparseClusterPayload sparsePayload;
            const bool sparseClusters = residency && device.SparseBufferTileBytes();
            own::shared_owner<MeshletSparseStorage> sparseStorage;
            // Transform-only consumers may have no meshlets. Scene admission
            // rejects missing cooked topology before reaching this cache.
            std::uint64_t sizes[]{
                residency ? 16 : identity->PackedVertices().size(),
                identity->StreamedTopology() ? 16 : std::uint64_t(geometry.indexCount) * sizeof(std::uint32_t),
                residency ? 16 : identity->Lods().size_bytes(), sizeof(RHIMeshletBufferHeader)};
            auto bytes = sizes[0] + sizes[1] + sizes[2] + sizes[3];
            {
                std::lock_guard lock(mutex);
                // All views/recordings of a storage must be idle before any of its
                // pages can be reused by another asset. A new acquisition pins it again.
                idleScratch.clear();
                idleScratch.reserve(entries.size());
                for (const auto& cached : std::span(entries))
                {
                    if (cached->sparse)
                    {
                        const bool eligible = cached.use_count() == 1 &&
                                              cached->state == RHIUploadTransactionState::Resident &&
                                              cached->completion <= completed;
                        idleScratch.emplace_back(cached->sparse.operator->(), eligible);
                    }
                }
                auto idle = std::span(idleScratch);
                std::ranges::sort(idle, [](const auto& left, const auto& right)
                                  { return std::less<MeshletSparseStorage*>{}(left.first, right.first); });
                for (std::size_t index = 0; index < idle.size();)
                {
                    const auto storage = idle[index].first;
                    bool eligible = idle[index++].second;
                    while (index < idle.size() && idle[index].first == storage)
                    {
                        eligible = idle[index++].second && eligible;
                    }
                    storage->SetEvictable(eligible);
                }
                for (const auto& entry : std::span(entries))
                {
                    if (sparseClusters && entry->identity.operator->() == identity.operator->() && entry->sparse)
                    {
                        sparseStorage = entry->sparse;
                        sparseStorage->SetEvictable(false);
                    }
                    if (entry->identity.operator->() == identity.operator->() && bool(entry->residency) == bool(residency) &&
                        (!residency || (entry->residency->revision == residency->revision &&
                                        entry->residency->residentLimit == residency->residentLimit &&
                                        entry->residency->residencyAlignment == residency->residencyAlignment)) &&
                        entry->prepared && (!entry->sparse || entry->sparseRevision == entry->sparse->Revision()) &&
                        ((entry->state == RHIUploadTransactionState::Resident && entry->usable) ||
                         (entry->state == RHIUploadTransactionState::Recording && entry->recording == recording)))
                    {
                        recordings[recording].push_back(entry);
                        ++stats.hits;
                        result = entry;
                        return true;
                    }
                }
                // Resident hits reuse the exact immutable GPU payload. Serialize
                // and validate only a cold entry, never once per draw/frame.
                topology.bytes.resize(sizeof(RHIMeshletBufferHeader));
                if (identity->StreamedTopology())
                {
                    PackStreamedMeshletGpuPayload(topology, *identity);
                }
                else if (identity->Meshlets().HasMeshlets() &&
                         !PackMeteorMeshletGpuPayload(identity->Meshlets(), identity->Count(), geometry.vertexStride,
                                                      geometry.vertexAttributeMask, geometry.sourceLodIndex,
                                                      identity->SourcePrimitiveCount(), identity->SourceVertexCount(),
                                                      identity->SourceVertexIds(), topology, error,
                                                      &identity->Hierarchy()))
                {
                    return false;
                }
                if (residency)
                {
                    AppendClusterTopology(topology, *identity, *residency, sparseClusters ? &sparsePayload : nullptr);
                }
                sizes[3] = topology.bytes.size();
                bytes = sizes[0] + sizes[1] + sizes[2] + sizes[3];
                // Budgets trim idle entries, never reject the live scene's
                // immutable inputs. Output owners pin entries until retirement.
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
            }
            auto entry = own::make_shared<MeshSurfaceStaticBuffers>(device);
            entry->identity = std::move(identity);
            entry->residency = std::move(residency);
            if (sparseClusters)
            {
                entry->sparse = sparseStorage ? sparseStorage : own::make_shared<MeshletSparseStorage>(device);
                if (!entry->sparse->Prepare(sparsePayload, topology.bytes.size(), entry->residency->residentLimit,
                                            error))
                {
                    return false;
                }
            }
            if (entry->sparse)
            {
                entry->sparseRevision = entry->sparse->Revision();
                entry->sparseSerial = entry->sparse->serial;
                entry->sparsePayload = sparsePayload;
                entry->sparseMetadata = topology.bytes;
            }
            entry->bytes = bytes;
            entry->recording = recording;
            entry->meshletCount = topology.meshletCount;
            RHIBufferSlice* destinations[]{&entry->vertices, &entry->indices, &entry->lods, &entry->meshlets};
            const wchar_t* names[]{L"METEOR.Mesh.StaticVertices", L"METEOR.Mesh.StaticIndices",
                                   L"METEOR.Mesh.StaticLods", L"METEOR.Mesh.Meshlets"};
            RHIUploadRequest requests[4];
            for (std::size_t i = 0; i < 4; ++i)
            {
                RHIBufferDesc description;
                description.bytes = sizes[i];
                description.debugName = names[i];
                if (i == 3 && entry->sparse)
                {
                    destinations[i]->buffer = entry->sparse->buffer;
                }
                else if (!device.CreateBuffer(description, destinations[i]->buffer, error))
                {
                    return false;
                }
                destinations[i]->size = sizes[i];
                requests[i] = {i == 3 && entry->sparse ? 16 : sizes[i], RHIUploadUsage::BufferCopy, 16};
            }
            if (!device.ReserveUploadBatch(requests, entry->staging, error) ||
                recording != device.GetCurrentUploadRecordingId())
            {
                return false;
            }
            const std::array<std::byte, 16> empty{};
            const void* sources[]{entry->residency ? empty.data() : entry->identity->PackedVertices().data(),
                                  entry->identity->StreamedTopology() ? static_cast<const void*>(empty.data())
                                                                      : static_cast<const void*>(geometry.indexData),
                                  entry->residency ? empty.data()
                                                   : static_cast<const void*>(entry->identity->Lods().data()),
                                  topology.bytes.data()};
            for (std::size_t i = 0; i < 4; ++i)
            {
                if (!entry->staging[i].IsWritable())
                {
                    return Fail(error, "Static mesh staging allocation is not writable.");
                }
                if (i != 3 || !entry->sparse)
                {
                    std::memcpy(entry->staging[i].cpuAddress, sources[i], sizes[i]);
                }
            }
            if (entry->sparse)
            {
                const auto stage = [&](uint64_t offset, const void* data, uint64_t size)
                {
                    const auto source = device.AllocateUpload({size, RHIUploadUsage::BufferCopy, 16});
                    if (!source.IsWritable() || recording != device.GetCurrentUploadRecordingId())
                    {
                        return false;
                    }
                    std::memcpy(source.cpuAddress, data, size);
                    auto destination = RHIBufferSlice::Whole(entry->sparse->buffer);
                    destination.offset = offset;
                    destination.size = size;
                    entry->sparseCopies.emplace_back(destination, source);
                    return true;
                };
                const auto& previous = entry->sparse->committedMetadata;
                for (size_t offset = 0; offset < topology.bytes.size();)
                {
                    if (offset + 16 <= previous.size() &&
                        std::memcmp(previous.data() + offset, topology.bytes.data() + offset, 16) == 0)
                    {
                        offset += 16;
                        continue;
                    }
                    const auto begin = offset;
                    do
                    {
                        offset += 16;
                    } while (offset < topology.bytes.size() &&
                             (offset + 16 > previous.size() ||
                              std::memcmp(previous.data() + offset, topology.bytes.data() + offset, 16) != 0));
                    if (!stage(begin, topology.bytes.data() + begin, offset - begin))
                    {
                        return Fail(error, "Sparse metadata staging belongs to another recording.");
                    }
                }
                // A queued metadata update invalidates the old comparison baseline,
                // including when that recording is later aborted or rejected.
                if (!entry->sparseCopies.empty())
                {
                    entry->sparse->committedMetadata.clear();
                }
                for (const auto& packet : sparsePayload.packets)
                {
                    if (!entry->sparse->NeedsPacket(packet))
                    {
                        continue;
                    }
                    const auto source = device.AllocateUpload({packet.bytes->size(), RHIUploadUsage::BufferCopy, 16});
                    if (!source.IsWritable() || recording != device.GetCurrentUploadRecordingId())
                    {
                        return Fail(error, "Sparse packet staging belongs to another recording.");
                    }
                    std::memcpy(source.cpuAddress, packet.bytes->data(), packet.bytes->size());
                    auto destination = RHIBufferSlice::Whole(entry->sparse->buffer);
                    destination.offset = packet.offset;
                    destination.size = packet.bytes->size();
                    entry->sparseCopies.emplace_back(destination, source);
                }
                entry->meshlets.size = entry->sparse->virtualBytes;
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
                if (entry->sparse)
                {
                    stats.uploadedBytes += sizes[0] + sizes[1] + sizes[2];
                    for (const auto& copy : entry->sparseCopies)
                    {
                        stats.uploadedBytes += copy.first.size;
                    }
                }
                else
                {
                    stats.uploadedBytes += bytes;
                }
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

    RHIBufferSlice MeshSurfaceBatch::Meshlets() const
    {
        return staticBuffers_ ? staticBuffers_->meshlets : RHIBufferSlice{};
    }

    RGHandle MeshSurfaceBatch::GraphMeshlets(const EnhancedRenderGraph& graph) const
    {
        if (!staticBuffers_ || staticBuffers_->graph != graph.DeclarationGraph() ||
            staticBuffers_->graphEpoch != graphEpoch_)
        {
            throw std::runtime_error("METEOR meshlet topology has no current graph declaration.");
        }
        return staticBuffers_->graphBuffers[3];
    }

    uint32_t MeshSurfaceBatch::MeshletCount() const
    {
        return staticBuffers_ ? staticBuffers_->meshletCount : 0;
    }

    MeshSurfaceCacheStats MeshSurfaceEvaluator::CacheStats() const
    {
        auto stats = staticCache_ ? staticCache_->Stats() : MeshSurfaceCacheStats{};
        stats.transforms = transforms_;
        stats.transformHits = transformHits_;
        stats.cachedOutputBytes = transformedBytes_;
        stats.cachedOutputs = transformedCache_.size();
        return stats;
    }

    struct MeshTransformDeclarationScratch
    {
        MeshTransformDeclarationPlan m_plan;
        std::array<RHIBindingDesc, MeshTransformDeclarationPlan::kMaxJobs> m_outputs;
    };

    struct MeshSurfaceBufferPool
    {
        struct Entry
        {
            std::uint64_t bytes{}, completion{};
            RHIBufferHandle buffer;
            RHIResourceState state{};
        };
        explicit MeshSurfaceBufferPool(IRenderDeviceServices& owner) : device(owner) {}
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
        // GPU 버퍼 풀의 수명 동안 한 번만 만든다. 선언별 원본 포인터와 출력
        // 설명만 덮어쓰며, 버퍼 회수 mutex를 쥔 채 워커에 합류하지 않는다.
        std::mutex m_declarationMutex;
        own::unique_owner<MeshTransformDeclarationScratch> m_declarationScratch;
    };

    void MeshSurfaceInput::SealInstanceBounds()
    {
        m_instanceMinimum = {};
        m_instanceMaximum = {};
        if (meshlets_.descriptors.empty())
        {
            return;
        }
        const double positionError = clusterArchive_ ? clusterArchive_->grid.positionError : 0.0;
        if (!std::isfinite(positionError) || positionError < 0.0)
        {
            return;
        }
        const auto infinity = std::numeric_limits<double>::infinity();
        std::array<double, 3> minimum{infinity, infinity, infinity};
        std::array<double, 3> maximum{-infinity, -infinity, -infinity};
        for (const auto& descriptor : meshlets_.descriptors)
        {
            // QMC2 페이지 검증과 같은 오차 범위를 포함한다. 캐시 적중/매 프레임
            // 경로에서는 정점이나 메시렛을 다시 순회하지 않는다.
            const double radius = double(descriptor.sphereRadius) + positionError * 1.732051 + 1e-5;
            if (!std::isfinite(radius) || descriptor.sphereRadius < 0.0f)
            {
                return;
            }
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                const double center = descriptor.sphereCenter[axis];
                if (!std::isfinite(center))
                {
                    return;
                }
                minimum[axis] = (std::min)(minimum[axis], center - radius);
                maximum[axis] = (std::max)(maximum[axis], center + radius);
            }
        }
        math::vector4 lower{}, upper{};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            lower[axis] = std::nextafter(static_cast<float>(minimum[axis]), -std::numeric_limits<float>::infinity());
            upper[axis] = std::nextafter(static_cast<float>(maximum[axis]), std::numeric_limits<float>::infinity());
            if (!std::isfinite(lower[axis]) || !std::isfinite(upper[axis]))
            {
                return;
            }
        }
        lower.w = upper.w = 1.0f;
        m_instanceMinimum = lower;
        m_instanceMaximum = upper;
    }

    bool MeshSurfaceInput::Seal(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                own::shared_owner<const MeshSurfaceInput>& result, std::string& error)
    {
        return SealImpl(draw, view, lods, IblBaker::MaxPoints, result, error);
    }

    bool MeshSurfaceInput::SealImpl(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                    std::uint32_t maxVertices, own::shared_owner<const MeshSurfaceInput>& result,
                                    std::string& error, bool includeHierarchy)
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
        auto candidate = own::make_shared<MeshSurfaceInput>(MeshSurfaceInput::ConstructionKey{});
        candidate->geometry_ = geometry;
        candidate->originalIndexCount_ = geometry.indexCount;
        candidate->sourcePrimitiveCount_ = geometry.indexCount / 3;
        candidate->sourceVertexCount_ = static_cast<uint32_t>(geometry.vertexBytes / geometry.vertexStride);
        if (const auto* meshlets = geometry.Meshlets())
        {
            candidate->meshlets_ = *meshlets;
        }
        candidate->world_ = draw.worldMatrix;
        candidate->view_ = view;
        candidate->lods_.assign(lods.begin(), lods.end());
        const auto* vertices = static_cast<const std::byte*>(geometry.vertexData);
        candidate->vertices_.assign(vertices, vertices + geometry.vertexBytes);
        candidate->indices_.assign(geometry.indexData, geometry.indexData + geometry.indexCount);
        if (includeHierarchy && geometry.SourceMesh() && !geometry.SourceMesh()->coarseLods.IsEmpty())
        {
            const auto& chain = geometry.SourceMesh()->coarseLods;
            // Geometry admission validated the immutable cooked chain. Revalidate
            // its ranges at this CPU ownership boundary before flattening it.
            if (chain.groups.empty() || chain.groups.size() > experiment::kMeshClusterMaxGroups || draw.boneCount)
            {
                return Fail(error, "METEOR cluster hierarchy is invalid for this pose; force recook.");
            }
            experiment::Mesh original;
            if (!original.vertices.AssignPacked(geometry.vertexAttributeMask, candidate->sourceVertexCount_,
                                                candidate->vertices_))
            {
                return Fail(error, "METEOR hierarchy lost its packed source vertices.");
            }
            original.indices = candidate->indices_;
            original.meshlets = candidate->meshlets_;
            if (!experiment::importer::ValidateMeshLods(original, chain, error))
            {
                return false;
            }
            auto& hierarchy = candidate->hierarchy_;
            hierarchy.groupCount = static_cast<uint32_t>(chain.groups.size());
            hierarchy.rootGroup = hierarchy.groupCount - 1;
            hierarchy.groups = chain.groups;
            for (uint32_t level = 0; level < chain.levels.size(); ++level)
            {
                const auto& coarse = chain.levels[level];
                auto& payload = candidate->meshlets_;
                const uint32_t vertices = static_cast<uint32_t>(payload.vertexRemap.size());
                const uint32_t triangles = static_cast<uint32_t>(payload.triangleIndices.size());
                const uint32_t primitives = static_cast<uint32_t>(payload.primitiveRemap.size());
                const uint32_t basePrimitive = static_cast<uint32_t>(candidate->indices_.size() / 3);
                for (auto d : coarse.meshlets.descriptors)
                {
                    d.vertexOffset += vertices;
                    d.triangleOffset += triangles;
                    d.primitiveOffset += primitives;
                    d.clusterId = static_cast<uint32_t>(payload.descriptors.size());
                    d.reservedBounds = basePrimitive;
                    payload.descriptors.push_back(d);
                }
                payload.vertexRemap.insert(payload.vertexRemap.end(), coarse.meshlets.vertexRemap.begin(),
                                           coarse.meshlets.vertexRemap.end());
                payload.triangleIndices.insert(payload.triangleIndices.end(), coarse.meshlets.triangleIndices.begin(),
                                               coarse.meshlets.triangleIndices.end());
                for (auto primitive : coarse.meshlets.primitiveRemap)
                {
                    payload.primitiveRemap.push_back(basePrimitive + primitive);
                }
                candidate->indices_.insert(candidate->indices_.end(), coarse.indices.begin(), coarse.indices.end());
            }
            for (uint32_t group = 0; group < hierarchy.groupCount; ++group)
            {
                const auto& node = hierarchy.groups[group];
                for (uint32_t m = 0; m < node.meshletCount; ++m)
                {
                    candidate->meshlets_.descriptors[node.firstMeshlet + m].reserved = group;
                }
            }
            candidate->meshlets_.lod0 = {0, static_cast<uint32_t>(candidate->meshlets_.descriptors.size())};
            candidate->geometry_.indexCount = static_cast<uint32_t>(candidate->indices_.size());
            candidate->sourcePrimitiveCount_ = candidate->geometry_.indexCount / 3;
        }
        candidate->geometry_.vertexData = candidate->vertices_.data();
        candidate->geometry_.indexData = candidate->indices_.data();
        // The sealed surface owns its copied bytes. It must not retain borrowed
        // provenance into an independently evictable upload/CPU-use payload.
        candidate->geometry_.sourceGeneration = nullptr;
        candidate->geometry_.sourceDescriptor = nullptr;
        candidate->geometry_.sourcePayload = nullptr;
        candidate->geometry_.sourceMeshIndex = UINT32_MAX;
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
        if (!assets::quantized::Encode(geometry.vertexAttributeMask, candidate->sourceVertexCount_,
                                       candidate->vertices_, candidate->packedVertices_, error))
        {
            return false;
        }
        if (includeHierarchy && candidate->meshlets_.HasMeshlets())
        {
            candidate->clusterArchive_ =
                geometry.SourceMesh() && geometry.SourceMesh()->clusterArchive
                    ? geometry.SourceMesh()->clusterArchive
                    : assets::cluster::Build(geometry.vertexAttributeMask, candidate->sourceVertexCount_,
                                             candidate->vertices_, candidate->meshlets_);
            candidate->clusterStream_ =
                own::make_shared<assets::cluster::Stream>(candidate->clusterArchive_, candidate->hierarchy_.groups);
        }
        candidate->SealInstanceBounds();
        result = std::move(candidate);
        error.clear();
        return true;
    }

    bool MeshSurfaceInput::Matches(const MeshSurfaceInput& other) const
    {
        if (StreamedTopology() || other.StreamedTopology())
        {
            return StreamedTopology() && other.StreamedTopology() && ClusterArchive().operator->() == other.ClusterArchive().operator->() &&
                   geometry_.handle == other.geometry_.handle && Count() == other.Count() &&
                   view_.sceneEpoch == other.view_.sceneEpoch && view_.viewRevision == other.view_.viewRevision &&
                   view_.geometryRevision == other.view_.geometryRevision && view_.eye == other.view_.eye &&
                   std::memcmp(&world_, &other.world_, sizeof(world_)) == 0 && Bones().size() == other.Bones().size() &&
                   (Bones().empty() || std::memcmp(Bones().data(), other.Bones().data(), Bones().size_bytes()) == 0);
        }
        const auto sameVertices =
            geometry_.vertexBytes == other.geometry_.vertexBytes &&
            std::memcmp(geometry_.vertexData, other.geometry_.vertexData, geometry_.vertexBytes) == 0;
        const auto sameIndices = geometry_.indexCount == other.geometry_.indexCount &&
                                 std::memcmp(geometry_.indexData, other.geometry_.indexData,
                                             std::size_t(geometry_.indexCount) * sizeof(std::uint32_t)) == 0;
        return Hierarchy().groupCount == other.Hierarchy().groupCount &&
               Hierarchy().rootGroup == other.Hierarchy().rootGroup && Hierarchy().groups == other.Hierarchy().groups &&
               geometry_.handle == other.geometry_.handle &&
               geometry_.vertexAttributeMask == other.geometry_.vertexAttributeMask && view_.eye == other.view_.eye &&
               view_.sceneEpoch == other.view_.sceneEpoch && view_.viewRevision == other.view_.viewRevision &&
               view_.geometryRevision == other.view_.geometryRevision &&
               std::memcmp(&world_, &other.world_, sizeof(world_)) == 0 && sameVertices && sameIndices &&
               std::ranges::equal(Lods(), other.Lods()) && Bones().size() == other.Bones().size() &&
               (Bones().empty() || std::memcmp(Bones().data(), other.Bones().data(), Bones().size_bytes()) == 0);
    }

    bool MeshSurfacePlan::Build(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                const MeshSurfacePlanBudget& budget, own::shared_owner<const MeshSurfacePlan>& result,
                                std::string& error)
    {
        return BuildPartitioned(draw, view, lods, budget, IblBaker::MaxPoints, result, error);
    }

    bool MeshSurfacePlan::BuildPartitioned(const EnhancedDrawItem& draw, const SurfaceView& view,
                                           std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                           std::uint32_t chunkCeiling, own::shared_owner<const MeshSurfacePlan>& result,
                                           std::string& error, bool includeHierarchy)
    {
        const auto& geometry = draw.modelMeshView;
        if (!geometry.IsComplete() || budget.maxChunkPoints < 3 || budget.maxChunkPoints > chunkCeiling ||
            !budget.maxChunks || geometry.vertexBytes / geometry.vertexStride > budget.maxSourceVertices ||
            geometry.indexCount % 3 != 0 || geometry.indexCount / 3 > budget.maxTriangles ||
            lods.size() != geometry.vertexBytes / geometry.vertexStride)
        {
            return Fail(error, "Mesh surface partition exceeds its source/chunk/triangle budget.");
        }
        auto candidate = own::make_shared<MeshSurfacePlan>(MeshSurfacePlan::ConstructionKey{});
        candidate->chunkLimit_ = budget.maxChunkPoints;
        auto& cost = candidate->cost_;
        cost.sourceVertices = static_cast<std::uint32_t>(geometry.vertexBytes / geometry.vertexStride);
        if (!MeshSurfaceInput::SealImpl(draw, view, lods, budget.maxSourceVertices, candidate->source_, error,
                                        includeHierarchy))
        {
            return false;
        }
        cost.triangles = candidate->source_->Geometry().indexCount / 3;
        if (cost.triangles > budget.maxTriangles)
        {
            return Fail(error, "METEOR all-LOD topology exceeds its triangle budget.");
        }
        const std::uint64_t boneBytes = std::uint64_t(draw.boneCount) * sizeof(PackedBoneMatrix);
        cost.cpuPayloadBytes =
            (candidate->source_->ClusterArchive() ? candidate->source_->ClusterArchive()->Bytes() * 2 : 0) +
            geometry.vertexBytes * 2ull + assets::quantized::HeaderBytes +
            std::uint64_t(candidate->source_->Geometry().indexCount) * sizeof(std::uint32_t) + lods.size_bytes() +
            boneBytes;
        if (const auto* cooked = &candidate->source_->Meshlets())
        {
            cost.cpuPayloadBytes += cooked->descriptors.size() * sizeof(experiment::MeshletDescriptor) +
                                    cooked->vertexRemap.size() * 4ull + cooked->triangleIndices.size() +
                                    cooked->primitiveRemap.size() * 4ull;
        }
        cost.cpuPayloadBytes += candidate->source_->Hierarchy().groups.size() * sizeof(experiment::MeshClusterGroup);
        if (cost.cpuPayloadBytes > budget.maxCpuPayloadBytes)
        {
            return Fail(error, "Mesh surface partition source exceeds its CPU payload budget.");
        }
        const auto& source = *candidate->source_;
        const auto* sourceBytes = static_cast<const std::byte*>(source.Geometry().vertexData);
        constexpr auto unused = std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> remap(cost.sourceVertices, unused);
        ce::dynamic_bitset referenced(cost.sourceVertices);
        own::shared_owner<MeshSurfaceInput> chunk;
        MeshSurfaceChunk description;
        own::shared_owner<std::vector<std::uint32_t>> sourceVertices;
        const auto start = [&](std::uint32_t firstTriangle)
        {
            chunk = own::make_shared<MeshSurfaceInput>(MeshSurfaceInput::ConstructionKey{});
            chunk->geometry_ = source.geometry_;
            chunk->hierarchy_ = source.Hierarchy();
            chunk->originalIndexCount_ = source.OriginalIndexCount();
            chunk->world_ = source.world_;
            chunk->view_ = source.view_;
            chunk->bones_ = source.bones_;
            chunk->clusterArchive_ = source.ClusterArchive();
            chunk->clusterStream_ = source.ClusterStream();
            description = {};
            sourceVertices = own::make_shared<std::vector<std::uint32_t>>();
            description.firstTriangle = firstTriangle;
        };
        const auto finish = [&]()
        {
            chunk->sourceVertexIds_ = *sourceVertices;
            chunk->sourcePrimitiveCount_ = cost.triangles;
            chunk->sourceVertexCount_ = cost.sourceVertices;
            const auto& cooked = source.Meshlets();
            chunk->meshlets_.settings = cooked.settings;
            chunk->meshlets_.geometryDigest = cooked.geometryDigest;
            const uint32_t end = description.firstTriangle + static_cast<uint32_t>(chunk->indices_.size() / 3);
            for (const auto& original : cooked.descriptors)
            {
                if (original.primitiveOffset < description.firstTriangle || original.primitiveOffset >= end)
                {
                    continue;
                }
                if (original.primitiveOffset + original.triangleCount > end)
                {
                    throw std::runtime_error("METEOR partition split a cooked cluster.");
                }
                auto descriptor = original;
                descriptor.vertexOffset = static_cast<uint32_t>(chunk->meshlets_.vertexRemap.size());
                descriptor.triangleOffset = static_cast<uint32_t>(chunk->meshlets_.triangleIndices.size());
                descriptor.primitiveOffset = static_cast<uint32_t>(chunk->meshlets_.primitiveRemap.size());
                for (uint32_t i = 0; i < original.vertexCount; ++i)
                {
                    const auto index = cooked.vertexRemap[original.vertexOffset + i];
                    if (remap[index] == unused)
                    {
                        throw std::runtime_error("METEOR cooked cluster lost a source vertex.");
                    }
                    chunk->meshlets_.vertexRemap.push_back(remap[index]);
                }
                const auto first = cooked.triangleIndices.begin() + original.triangleOffset;
                chunk->meshlets_.triangleIndices.insert(chunk->meshlets_.triangleIndices.end(), first,
                                                        first + original.triangleCount * 3);
                const auto primitive = cooked.primitiveRemap.begin() + original.primitiveOffset;
                chunk->meshlets_.primitiveRemap.insert(chunk->meshlets_.primitiveRemap.end(), primitive,
                                                       primitive + original.triangleCount);
                chunk->meshlets_.descriptors.push_back(descriptor);
            }
            chunk->meshlets_.lod0 = {0, static_cast<uint32_t>(chunk->meshlets_.descriptors.size())};
            chunk->geometry_.vertexData = chunk->vertices_.data();
            chunk->geometry_.vertexBytes = chunk->vertices_.size();
            chunk->geometry_.indexData = chunk->indices_.data();
            chunk->geometry_.indexCount = static_cast<std::uint32_t>(chunk->indices_.size());
            if (!assets::quantized::Encode(chunk->geometry_.vertexAttributeMask, chunk->Count(), chunk->vertices_,
                                           chunk->packedVertices_, error))
            {
                throw std::runtime_error(error);
            }
            for (auto index : *sourceVertices)
            {
                remap[index] = unused;
            }
            chunk->SealInstanceBounds();
            description.input = std::move(chunk);
            description.sourceVerticesOwner_ = sourceVertices;
            description.sourceVertices = *sourceVertices;
            candidate->chunks_.push_back(std::move(description));
        };
        start(0);
        uint32_t clusterIndex{};
        const auto& clusters = source.Meshlets().descriptors;
        for (std::uint32_t triangle = 0; triangle < cost.triangles; ++triangle)
        {
            if (clusterIndex < clusters.size() && clusters[clusterIndex].primitiveOffset == triangle)
            {
                const auto& cluster = clusters[clusterIndex++];
                const uint64_t topologyBytes =
                    sizeof(experiment::MeshletDescriptor) + cluster.vertexCount * 8ull + cluster.triangleCount * 7ull +
                    (source.ClusterArchive() ? source.ClusterArchive()->PacketBytes(cluster.clusterId) + 20 : 0);
                if (topologyBytes > budget.maxCpuPayloadBytes - cost.cpuPayloadBytes ||
                    topologyBytes > budget.maxGpuPayloadBytes - cost.gpuPayloadBytes)
                {
                    return Fail(error, "METEOR cooked topology exceeds its CPU/GPU payload budget.");
                }
                cost.cpuPayloadBytes += topologyBytes;
                cost.gpuPayloadBytes += topologyBytes;
                uint32_t clusterAddition{};
                for (uint32_t i = 0; i < cluster.vertexCount; ++i)
                {
                    clusterAddition += remap[source.Meshlets().vertexRemap[cluster.vertexOffset + i]] == unused;
                }
                if (cluster.vertexCount > budget.maxChunkPoints)
                {
                    return Fail(error, "METEOR chunk budget cannot contain a cooked cluster.");
                }
                if (chunk->Count() + clusterAddition > budget.maxChunkPoints)
                {
                    finish();
                    if (candidate->chunks_.size() >= budget.maxChunks)
                    {
                        return Fail(error, "METEOR partition exceeds its chunk count budget.");
                    }
                    start(triangle);
                }
            }
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
            if (clusters.empty() && chunk->Count() + additional > budget.maxChunkPoints)
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
            const std::uint64_t cpuAddition =
                std::uint64_t(additional) * (geometry.vertexStride * 2ull + 12ull) + 12 +
                (chunk->indices_.empty() ? assets::quantized::HeaderBytes + boneBytes +
                                               source.Hierarchy().groups.size() * sizeof(experiment::MeshClusterGroup)
                                         : 0);
            // Payload only: source upload + world/material/bake buffers and index/LOD.
            // Descriptor allocation, alignment, texture residency and frame overlap
            // remain the native host's separate budget.
            const std::uint64_t gpuAddition =
                std::uint64_t(additional) * ((source.ClusterArchive() ? 8 : 0) + geometry.vertexStride + sizeof(float) +
                                             sizeof(SurfacePoint) + sizeof(IblBakePoint) + sizeof(IblBakeSample)) +
                12 +
                (chunk->indices_.empty()
                     ? (source.ClusterArchive() ? source.Hierarchy().groups.size() * 4 + 192 : 0) +
                           assets::quantized::HeaderBytes +
                           std::max(boneBytes, std::uint64_t(sizeof(PackedBoneMatrix))) +
                           (clusters.empty() ? 0 : sizeof(RHIMeshletBufferHeader) + source.Hierarchy().GpuBytes())
                     : 0);
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
                    sourceVertices->push_back(index);
                    const auto* vertex = sourceBytes + std::size_t(index) * geometry.vertexStride;
                    chunk->vertices_.insert(chunk->vertices_.end(), vertex, vertex + geometry.vertexStride);
                    chunk->lods_.push_back(source.lods_[index]);
                    ++cost.points;
                    if (!referenced.test(index))
                    {
                        referenced.set(index, true);
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

#include "MeshletStreamedPlan.inl"

    bool MeshSurfacePlan::Matches(const MeshSurfacePlan& other) const
    {
        return chunkLimit_ == other.chunkLimit_ && source_->Matches(*other.source_);
    }

    bool MeshSurfacePlan::SharesSceneTopology(const MeshSurfacePlan& other) const
    {
        const auto root = [](const MeshSurfacePlan* plan)
        {
            while (plan->staticOwner_)
            {
                plan = plan->staticOwner_.operator->();
            }
            return plan;
        };
        // The immutable cached partition includes exact generation, source LOD,
        // layout and chunk budget. A cache eviction is a conservative miss.
        return root(this) == root(&other);
    }

    own::weak_owner<const MeshSurfaceInput> MeshSurfacePlan::SceneTopologyIdentity() const
    {
        const auto* root = this;
        while (root->staticOwner_)
        {
            root = root->staticOwner_.operator->();
        }
        // source는 partition 생성마다 새로 봉인된다. 재생성/eviction은 보수적인 cache miss다.
        return root->source_;
    }

    bool MeshSurfacePlan::BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                                        std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                        own::shared_owner<const MeshSurfacePlan>& result, std::string& error)
    {
        // The raster scene computes the actual mip footprint from pixel UV
        // derivatives. Only its zero-initialized vertex LOD transport is reusable.
        if (!draw.modelMeshView.IsSceneComplete() ||
            lods.size() != draw.modelMeshView.vertexBytes / draw.modelMeshView.vertexStride ||
            !std::ranges::all_of(lods, [](float value) { return value == 0.f; }))
        {
            return Build(draw, view, lods, budget, result, error);
        }
        return BuildSceneZeroLod(draw, view, lods, budget, result, error);
    }

    bool MeshSurfacePlan::BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                                        const MeshSurfacePlanBudget& budget,
                                        own::shared_owner<const MeshSurfacePlan>& result, std::string& error)
    {
        return BuildSceneZeroLod(draw, view, {}, budget, result, error);
    }

    void MeshSurfacePlan::ReleaseCachedScenePlans()
    {
        auto& state = GetScenePlanCache();
        decltype(state.entries) retired;
        {
            std::lock_guard lock(state.mutex);
            retired.swap(state.entries);
            state.insertionOrder.clear();
            state.bytes = 0;
        }
        // Stream readers close outside the cache mutex. Shared plans retained
        // by active consumers continue to own their exact artifact leases.
    }

    bool MeshSurfacePlan::BuildSceneZeroLod(const EnhancedDrawItem& draw, const SurfaceView& view,
                                            std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                            own::shared_owner<const MeshSurfacePlan>& result, std::string& error)
    {
        ce::profile_scope profile{ce::marker<"MaterialMeshPlanForScene">()};

        auto& state = GetScenePlanCache();
        auto& cacheMutex = state.mutex;
        auto& cache = state.entries;
        auto& insertionOrder = state.insertionOrder;
        auto& cachedBytes = state.bytes;
        using Key = ScenePlanCache::Key;
        using Entry = ScenePlanCache::Entry;
        constexpr std::uint64_t maxCachedBytes = 128ull << 20;
        constexpr std::size_t maxEntries = 64;

        const auto& geometry = draw.modelMeshView;
        if (geometry.sourceLodIndex != 0 || !geometry.Meshlets() ||
            geometry.Meshlets()->settings.profileVersion != experiment::kMeshletProfileVersion)
        {
            return Fail(error, "METEOR Scene requires current cooked meshlet topology; force reimport/recook.");
        }
        const Key key{geometry.handle, geometry.sourceLodIndex};
        const auto sameBudget = [](const MeshSurfacePlanBudget& a, const MeshSurfacePlanBudget& b)
        {
            return a.maxSourceVertices == b.maxSourceVertices && a.maxTriangles == b.maxTriangles &&
                   a.maxChunkPoints == b.maxChunkPoints && a.maxChunks == b.maxChunks &&
                   a.maxCpuPayloadBytes == b.maxCpuPayloadBytes && a.maxGpuPayloadBytes == b.maxGpuPayloadBytes;
        };
        own::shared_owner<const MeshSurfacePlan> reusable;
        if (geometry.IsSceneComplete())
        {
            std::lock_guard lock(cacheMutex);
            if (const auto found = cache.find(key);
                found != cache.end() && sameBudget(found->second.budget, budget) &&
                found->second.vertexData == geometry.vertexData && found->second.indexData == geometry.indexData &&
                (found->second.plan->source_->StreamedTopology()
                     ? uint64_t(found->second.plan->source_->SourceVertexCount()) * geometry.vertexStride ==
                           geometry.vertexBytes
                     : found->second.plan->source_->geometry_.vertexBytes == geometry.vertexBytes) &&
                found->second.plan->source_->OriginalIndexCount() == geometry.indexCount &&
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
            // Scene raster consumes posed vertex streams, not the bounded IBL point batch.
            // One million vertices also stays within the transform dispatch X limit.
            const bool built = geometry.StreamMetadata()
                                   ? BuildStreamed(draw, view, budget, result, error)
                                   : BuildPartitioned(draw, view, lods, budget, 1u << 20, result, error, true);
            if (!built)
            {
                return false;
            }
            if (result->cost_.cpuPayloadBytes <= maxCachedBytes)
            {
                std::lock_guard lock(cacheMutex);
                if (const auto found = cache.find(key); found != cache.end())
                {
                    cachedBytes -= found->second.plan->cost_.cpuPayloadBytes;
                    cache.erase(found);
                    std::erase(insertionOrder, key);
                }
                cache.emplace(key, Entry{result, budget, geometry.vertexData, geometry.indexData});
                insertionOrder.push_back(key);
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

        if (!Affine(draw.worldMatrix))
        {
            std::string values;
            for (const float value : std::bit_cast<std::array<float, 16>>(draw.worldMatrix))
            {
                values += std::to_string(value) + "[" + std::to_string(std::bit_cast<std::uint32_t>(value)) + "],";
            }

            return Fail(error, "Scene mesh reuse rejected a nonfinite/nonaffine world transform: " + values);
        }

        if (!view.sceneEpoch || !view.viewRevision || !view.geometryRevision || !Finite(view.eye))
        {
            return Fail(error, "Scene mesh reuse needs a valid current view.");
        }

        if (draw.boneCount > 256 || (draw.boneCount && !draw.bonePalette))
        {
            return Fail(error, "Scene mesh reuse needs a valid bone palette.");
        }

        if (draw.boneCount != reusable->source_->bones_.size())
        {
            return Fail(error, "Scene mesh reuse pose layout mismatch: current=" + std::to_string(draw.boneCount) +
                                   " cached=" + std::to_string(reusable->source_->bones_.size()) + ".");
        }

        auto candidate = own::make_shared<MeshSurfacePlan>(MeshSurfacePlan::ConstructionKey{});
        candidate->staticOwner_ = reusable;
        candidate->cost_ = reusable->cost_;
        candidate->chunkLimit_ = reusable->chunkLimit_;
        const auto bind = [&](const own::shared_owner<const MeshSurfaceInput>& original)
        {
            auto input = own::make_shared<MeshSurfaceInput>(MeshSurfaceInput::ConstructionKey{});
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
            chunk.sourceVerticesOwner_ = previous.sourceVerticesOwner_;
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
            if (!bufferPool_ || !bufferPool_->Recycle(BufferBytes(), buffer_, bufferState_, completion_))
            {
                device_->ReleaseBuffer(buffer_);
            }
        }
    }

    const math::matrix4x4& MeshSurfaceBatch::ObjectToWorld() const
    {
        static const math::matrix4x4 identity = math::matrix4x4::identity();
        return objectSpace_ ? input_->World() : identity;
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
                                                  RHILayout::Srv(2), RHILayout::UavBufferTable(4096, 0)};
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
            bufferPool_ = own::make_shared<MeshSurfaceBufferPool>(device);
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

    void MeshSurfaceEvaluator::BeginFrame(std::uint64_t recording)
    {
        if (!recording || recording == cacheRecording_)
        {
            return;
        }
        cacheRecording_ = recording;
        ++cacheGeneration_;
        if (requestCache_)
        {
            requestCache_->Trim(cacheGeneration_);
        }
        const auto completed = bufferPool_ ? bufferPool_->completed.load(std::memory_order_acquire) : 0;
        std::erase_if(transformedCache_,
                      [&](const auto& batch)
                      {
                          // Keep the preceding two recordings for motion/history and queue
                          // overlap. Submitted consumers independently retain their owners.
                          if (cacheGeneration_ - batch->lastUsedGeneration_ <= 2 || batch.use_count() != 1 ||
                              (batch->completion_.IsValid() && batch->completion_.value > completed))
                          {
                              return false;
                          }
                          transformedBytes_ -= batch->BufferBytes();
                          return true;
                      });
        if (staticCache_)
        {
            staticCache_->TrimIdle();
        }
    }

    bool MeshSurfaceEvaluator::Record(IRenderDeviceServices& device, own::shared_owner<const MeshSurfaceInput> input,
                                      own::shared_owner<const MeshSurfaceBatch>& result, std::string& error)
    {
        own::shared_owner<const MeshSurfaceBatch> candidate;
        if (!Prepare(device, std::move(input), candidate, error))
        {
            return false;
        }
        // Upload/descriptor allocation may submit a command-list prefix and rotate
        // its encoder. Reject that recording change before any command references
        // this candidate: failure must not release a submitted GPU resource.
        const own::shared_owner<const MeshSurfaceBatch> batches[]{candidate};
        own::shared_owner<MeshTransformDispatch> dispatch;
        if (!MeshSurfaceBatch::PrepareDispatch(batches, dispatch, error))
        {
            return false;
        }
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{candidate->buffer_, RHIResourceState::Common,
                                         RHIResourceState::UnorderedAccess};
        encoder.ResourceBarriers({{}, {&before, 1}});
        if (!MeshSurfaceBatch::RecordDispatch(encoder, batches, *dispatch, error))
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

    bool MeshSurfaceEvaluator::Prepare(IRenderDeviceServices& device, own::shared_owner<const MeshSurfaceInput> input,
                                       own::shared_owner<const MeshSurfaceBatch>& result, std::string& error,
                                       bool cacheStatic, std::uint64_t auditRealFrame, std::uint64_t auditView,
                                       bool objectSpace, bool directClusters,
                                       const assets::cluster::StreamBudget& clusterBudget)
    {
        ce::profile_scope profile{ce::marker<"MaterialMeshGpuPrepare">()};
        std::optional<ce::profile_scope> detailProfile;
        const auto nextDetail = [&](ce::marker_id marker)
        {
            detailProfile.reset();
            detailProfile.emplace(marker);
        };

        nextDetail(ce::marker<"MeshPrepareValidate">());
        if (device_ != &device || !(pipeline_ && pipeline_->IsValid()) || !device.GetCurrentUploadRecordingId() ||
            !input)
        {
            return Fail(error, "Mesh surface transform needs a sealed input on its initialized device in a recording.");
        }
        nextDetail(ce::marker<"MeshPrepareOwner">());
        auto candidate = own::make_shared<MeshSurfaceBatch>(MeshSurfaceBatch::ConstructionKey{});
        candidate->device_ = &device;
        candidate->input_ = std::move(input);
        candidate->count_ = candidate->input_->Count();
        candidate->recordingId_ = device.GetCurrentUploadRecordingId();
        candidate->auditRealFrame_ = auditRealFrame;
        candidate->auditView_ = auditView;
        candidate->descriptorVersion_ = device.GetDescriptorVersionToken();
        candidate->pipeline_ = pipeline_;
        candidate->objectSpace_ = objectSpace;
        candidate->directClusters_ =
            directClusters && objectSpace && candidate->input_->ClusterArchive() && candidate->input_->ClusterStream();
        if (candidate->input_->StreamedTopology() && !candidate->directClusters_)
        {
            return Fail(error, "Streamed connectivity requires direct GPU cluster consumption.");
        }
        nextDetail(ce::marker<"MeshPrepareIdentity">());
        auto identity = candidate->input_;
        while (identity->staticOwner_)
        {
            identity = identity->staticOwner_;
        }
        if (cacheStatic || candidate->directClusters_)
        {
            BeginFrame(candidate->recordingId_);
        }
        nextDetail(ce::marker<"MeshPrepareClusters">());
        if (candidate->directClusters_)
        {
            if (!requestCache_)
            {
                requestCache_ = own::make_shared<MeshSurfaceRequestCache>();
            }
            own::shared_owner<const std::vector<std::uint32_t>> requests;
            {
                ce::profile_scope requestProfile{ce::marker<"MeshClusterRequests">()};
                requests = requestCache_->Acquire(identity, *candidate->input_, auditView, cacheGeneration_);
            }
            auto budget = clusterBudget;
            const auto tileBytes = device.SparseBufferTileBytes();
            if (tileBytes)
            {
                budget.residencyAlignment = 16;
            }
            if (device.StreamingMemoryPressure())
            {
                budget.residentBytes =
                    (std::min)(budget.residentBytes,
                               candidate->input_->ClusterStream()->PinnedBytes(budget.residencyAlignment));
            }
            ce::profile_scope residencyProfile{ce::marker<"MeshClusterResidencyPrepare">()};
            candidate->clusterResidency_ =
                candidate->input_->ClusterStream()->Prepare(candidate->recordingId_, *requests, budget);
        }
        const auto& source = *candidate->input_;
        nextDetail(ce::marker<"MeshPrepareStaticCache">());
        if (cacheStatic)
        {
            candidate->lastUsedGeneration_ = cacheGeneration_;
            if (!staticCache_)
            {
                staticCache_ = own::make_shared<MeshSurfaceStaticCache>(device);
            }
            if (!staticCache_->Acquire(std::move(identity), candidate->staticBuffers_, error,
                                       candidate->clusterResidency_))
            {
                return false;
            }
            nextDetail(ce::marker<"MeshPrepareTransformLookup">());
            if (candidate->staticBuffers_ && bufferPool_)
            {
                for (const auto& previous : std::span(transformedCache_))
                {
                    const auto& prior = *previous->input_;
                    // One geometry/pose has one immutable output. Object-space
                    // instances keep separate world matrices and share its producer.
                    if (previous->staticBuffers_.operator->() != candidate->staticBuffers_.operator->() || previous->pipeline_.operator->() != pipeline_.operator->() ||
                        previous->objectSpace_ != objectSpace ||
                        (!objectSpace && std::memcmp(&prior.World(), &source.World(), sizeof(math::matrix4x4)) != 0) ||
                        prior.Bones().size() != source.Bones().size() ||
                        (!source.Bones().empty() &&
                         std::memcmp(prior.Bones().data(), source.Bones().data(), source.Bones().size_bytes()) != 0))
                    {
                        continue;
                    }
                    const bool sameRecording = previous->recordingId_ == candidate->recordingId_ &&
                                               previous->descriptorVersion_ == candidate->descriptorVersion_;
                    // A fresh producer in this recording may be shared before it
                    // executes. Older recordings require a completed immutable output.
                    // Preparation has no graph identity. Once declared, a fresh
                    // producer cannot be selected for an unknown subsequent graph.
                    if (sameRecording && previous->HasGraphDeclaration())
                    {
                        continue;
                    }
                    if (!sameRecording &&
                        (!previous->staticBuffers_->usable || !previous->IsReadyForEvaluation() ||
                         !previous->completion_.IsValid() ||
                         previous->completion_.value > bufferPool_->completed.load(std::memory_order_acquire)))
                    {
                        continue;
                    }
                    candidate->transformedOwner_ = previous;
                    candidate->buffer_ = previous->buffer_;
                    candidate->bufferBytes_ = previous->BufferBytes();
                    candidate->self_ = candidate;
                    if (sameRecording)
                    {
                        candidate->declarationOwner_ = previous;
                    }
                    else if (const auto declaration = previous->reuseDeclaration_.lock();
                             declaration && declaration->recordingId_ == candidate->recordingId_ &&
                             declaration->descriptorVersion_ == candidate->descriptorVersion_ &&
                             !declaration->HasGraphDeclaration())
                    {
                        candidate->declarationOwner_ = declaration;
                    }
                    else
                    {
                        previous->reuseDeclaration_ = candidate;
                    }
                    previous->lastUsedGeneration_ = cacheGeneration_;
                    ++transformHits_;
                    result = std::move(candidate);
                    error.clear();
                    return true;
                }
            }
        }
        nextDetail(ce::marker<"MeshPrepareOutput">());
        RHIBufferDesc description;
        description.bytes = candidate->Count() * sizeof(SurfacePoint);
        if (candidate->directClusters_)
        {
            const auto bytes = 32 + source.Bones().size_bytes();
            description.bytes = ((bytes + sizeof(SurfacePoint) - 1) / sizeof(SurfacePoint)) * sizeof(SurfacePoint);
            candidate->bufferBytes_ = description.bytes;
            candidate->clusterPoseUpload_ = device.AllocateUpload({description.bytes, RHIUploadUsage::BufferCopy, 16});
            if (!candidate->clusterPoseUpload_.IsWritable())
            {
                return Fail(error, "Cluster pose upload failed");
            }
            std::memset(candidate->clusterPoseUpload_.cpuAddress, 0, description.bytes);
            const uint32_t boneCount = static_cast<uint32_t>(source.Bones().size());
            std::memcpy(candidate->clusterPoseUpload_.cpuAddress, &boneCount, 4);
            if (boneCount)
            {
                std::memcpy(static_cast<std::byte*>(candidate->clusterPoseUpload_.cpuAddress) + 32,
                            source.Bones().data(), source.Bones().size_bytes());
            }
        }
        description.allowUnorderedAccess = !candidate->directClusters_;
        description.debugName = L"METEOR.Material.MeshWorldPoints";
        candidate->bufferPool_ = bufferPool_;
        if ((!candidate->bufferPool_ ||
             !candidate->bufferPool_->Acquire(description.bytes, candidate->buffer_, candidate->bufferState_)) &&
            !device.CreateBuffer(description, candidate->buffer_, error))
        {
            return false;
        }
        candidate->self_ = candidate;
        if (cacheStatic && candidate->staticBuffers_)
        {
            transformedCache_.push_back(candidate);
            transformedBytes_ += description.bytes;
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
        return graph_ == graph.DeclarationGraph() && graphEpoch_ == graph.DeclarationEpoch() &&
                       graph.ResolveBufferHandle(graphOutput_) == buffer_
                   ? graphOutput_
                   : RGHandle{};
    }

    struct MeshTransformDispatch
    {
        static constexpr uint32_t MaxJobs = 4096, GroupWidth = 65535;
        struct Job
        {
            math::matrix4x4 world;
            std::array<uint32_t, 4> countStrideMaskBones;
            std::array<uint32_t, 8> offsets;
            std::array<uint32_t, 4> inputOffsets;
        };
        struct Group
        {
            uint32_t job, firstVertex;
        };
        static_assert(sizeof(Job) == 128 && sizeof(Group) == 8);
        RHIBufferSlice uniform, inputs, jobs, groups;
        RHIBindingTable outputs;
        uint32_t groupCount{}, dispatchX{}, dispatchY{};
        uint64_t pointCount{}, inputBytes{};
    };

    bool MeshSurfaceBatch::PrepareDispatch(std::span<const own::shared_owner<const MeshSurfaceBatch>> batches,
                                           own::shared_owner<MeshTransformDispatch>& result, std::string& error)
    {
        if (batches.empty() || batches.size() > MeshTransformDispatch::MaxJobs)
        {
            return Fail(error, "METEOR transform work list requires 1..4096 jobs; no alternate dispatch route.");
        }
        auto dispatch = own::make_shared<MeshTransformDispatch>();
        auto& device = *batches.front()->device_;
        ce::profile_scope prepareScope{ce::marker<"MaterialMeshDispatchPrepare">()};
        std::vector<std::byte> inputs;
        std::vector<MeshTransformDispatch::Job> jobs;
        std::vector<MeshTransformDispatch::Group> groups;
        std::vector<RHIBindingDesc> outputs;
        std::map<std::pair<uintptr_t, size_t>, uint32_t> immutableInputs;
        const auto append = [&](const void* data, size_t bytes, bool deduplicate) -> uint32_t
        {
            const auto key = std::pair{reinterpret_cast<uintptr_t>(data), bytes};
            if (deduplicate)
            {
                const auto found = immutableInputs.find(key);
                if (found != immutableInputs.end())
                {
                    return found->second;
                }
            }
            const auto offset = (inputs.size() + 15u) & ~size_t(15u);
            if (offset + bytes > (256ull << 20))
            {
                throw std::runtime_error("METEOR transform packed input exceeds 256 MiB; no fallback.");
            }
            inputs.resize(offset + bytes);
            if (bytes)
            {
                std::memcpy(inputs.data() + offset, data, bytes);
            }
            if (deduplicate)
            {
                immutableInputs.emplace(key, static_cast<uint32_t>(offset));
            }
            return static_cast<uint32_t>(offset);
        };
        for (const auto& batch : batches)
        {
            if (!batch->IsPreparedForGraph() || batch->device_ != &device ||
                batch->pipeline_.operator->() != batches.front()->pipeline_.operator->() || batch->transformedOwner_)
            {
                return Fail(error, "METEOR transform jobs require one fresh pipeline/device/recording.");
            }
            const auto& source = *batch->input_;
            MeshTransformDispatch::Job job{batch->objectSpace_ ? math::matrix4x4::identity() : source.World(),
                                           {batch->Count(), source.Geometry().vertexStride,
                                            source.Geometry().vertexAttributeMask,
                                            static_cast<uint32_t>(source.Bones().size())},
                                           {},
                                           {}};
            for (const auto& attribute : assets::kVertexAttributeTable)
            {
                job.offsets[static_cast<size_t>(attribute.attribute)] =
                    assets::OffsetOf(source.Geometry().vertexAttributeMask, attribute.attribute);
            }
            job.inputOffsets[0] = append(source.PackedVertices().data(), source.PackedVertices().size(), true);
            job.inputOffsets[1] = append(source.Bones().data(), source.Bones().size_bytes(), true);
            job.inputOffsets[2] = append(source.Lods().data(), source.Lods().size_bytes(), true);
            dispatch->pointCount += batch->Count();
            const auto jobIndex = static_cast<uint32_t>(jobs.size());
            jobs.push_back(job);
            for (uint32_t first = 0; first < batch->Count(); first += 32)
            {
                groups.push_back({jobIndex, first});
            }
            outputs.push_back(RHIBindingDesc::UavBuffer(batch->buffer_, batch->Count(), sizeof(SurfacePoint)));
        }
        if (groups.empty() || groups.size() > uint64_t(MeshTransformDispatch::GroupWidth) * 65535)
        {
            return Fail(error, "METEOR transform group list exceeds one 2D dispatch.");
        }
        dispatch->groupCount = static_cast<uint32_t>(groups.size());
        dispatch->dispatchY =
            (dispatch->groupCount + MeshTransformDispatch::GroupWidth - 1) / MeshTransformDispatch::GroupWidth;
        dispatch->dispatchX = (dispatch->groupCount + dispatch->dispatchY - 1) / dispatch->dispatchY;
        const std::array<uint32_t, 4> constants{dispatch->groupCount, dispatch->dispatchX,
                                                static_cast<uint32_t>(jobs.size()), 0};
        dispatch->uniform = device.UploadConstants(constants.data(), sizeof(constants));
        const auto upload = [&](const void* data, size_t bytes)
        {
            auto slice = device.AllocateUpload({bytes, RHIUploadUsage::Raw, 16});
            if (slice.IsWritable())
            {
                std::memcpy(slice.cpuAddress, data, bytes);
            }
            return slice;
        };
        dispatch->inputBytes =
            inputs.size() + jobs.size() * sizeof(jobs.front()) + groups.size() * sizeof(groups.front());
        dispatch->inputs = upload(inputs.data(), inputs.size());
        dispatch->jobs = upload(jobs.data(), jobs.size() * sizeof(jobs.front()));
        dispatch->groups = upload(groups.data(), groups.size() * sizeof(groups.front()));
        // Only live descriptors are allocated; all group job IDs are CPU sealed below jobCount.
        dispatch->outputs = device.CreateBindings(outputs);
        if (!dispatch->uniform.IsValid() || !dispatch->inputs.IsWritable() || !dispatch->jobs.IsWritable() ||
            !dispatch->groups.IsWritable() || !dispatch->outputs.IsValid() ||
            !std::ranges::all_of(batches, [](const auto& batch) { return batch->IsPreparedForGraph(); }))
        {
            return Fail(error, "METEOR transform work-list allocation changed recording/descriptors.");
        }
        result = std::move(dispatch);
        error.clear();
        return true;
    }

    bool MeshSurfaceBatch::PrepareDeclarationDispatch(std::span<const own::shared_owner<const MeshSurfaceBatch>> batches,
                                                      own::shared_owner<MeshTransformDispatch>& result,
                                                      std::string& error)
    {
        ce::profile_scope prepareScope{ce::marker<"MaterialMeshDispatchPrepare">()};
        if (batches.empty() || batches.size() > MeshTransformDeclarationPlan::kMaxJobs || !batches.front() ||
            !batches.front()->bufferPool_)
        {
            return Fail(error, "METEOR transform declaration requires 1..4096 retained jobs.");
        }
        auto& pool = *batches.front()->bufferPool_;
        render_graph_declaration_lock declarationLock(pool.m_declarationMutex);
        if (!declarationLock.owns_lock())
        {
            return Fail(error, "Concurrent transform declaration cannot reuse one scratch owner.");
        }
        if (!pool.m_declarationScratch)
        {
            pool.m_declarationScratch = own::make_unique<MeshTransformDeclarationScratch>();
        }
        auto& scratch = *pool.m_declarationScratch;
        auto& plan = scratch.m_plan;
        plan.Reset();
        auto dispatch = own::make_shared<MeshTransformDispatch>();
        auto& device = *batches.front()->device_;
        {
            ce::profile_scope planScope{ce::marker<"RenderGraphDeclareMeshPlan">()};
            for (std::size_t index = 0; index < batches.size(); ++index)
            {
                const auto& batch = batches[index];
                if (!batch || !batch->IsPreparedForGraph() || batch->device_ != &device ||
                    batch->pipeline_.operator->() != batches.front()->pipeline_.operator->() || batch->transformedOwner_)
                {
                    return Fail(error, "METEOR transform jobs require one fresh pipeline/device/recording.");
                }
                const auto& source = *batch->input_;
                if (!plan.Append(batch->Count(), source.PackedVertices(), std::as_bytes(source.Bones()),
                                 std::as_bytes(source.Lods()), error))
                {
                    return false;
                }
                dispatch->pointCount += batch->Count();
                scratch.m_outputs[index] =
                    RHIBindingDesc::UavBuffer(batch->buffer_, batch->Count(), sizeof(SurfacePoint));
            }
        }
        if (!plan.GroupCount())
        {
            return Fail(error, "METEOR transform group list exceeds one 2D dispatch.");
        }
        dispatch->groupCount = plan.GroupCount();
        dispatch->dispatchY =
            (dispatch->groupCount + MeshTransformDispatch::GroupWidth - 1) / MeshTransformDispatch::GroupWidth;
        dispatch->dispatchX = (dispatch->groupCount + dispatch->dispatchY - 1) / dispatch->dispatchY;
        const std::array<std::uint32_t, 4> constants{dispatch->groupCount, dispatch->dispatchX,
                                                     static_cast<std::uint32_t>(plan.JobCount()), 0};
        const RHIUploadRequest requests[]{
            {sizeof(constants), RHIUploadUsage::ConstantBuffer, 256},
            {plan.InputBytes(), RHIUploadUsage::Raw, 16},
            {plan.JobCount() * sizeof(MeshTransformDispatch::Job), RHIUploadUsage::Raw,
             (std::max)(std::size_t{16}, alignof(MeshTransformDispatch::Job))},
            {std::uint64_t(plan.GroupCount()) * sizeof(MeshTransformDispatch::Group), RHIUploadUsage::Raw, 16}};
        std::array<RHIBufferSlice, 4> uploads;
        if (!device.ReserveUploadBatch(requests, uploads, error) ||
            std::ranges::any_of(uploads,
                                [](const auto& upload) { return !upload.IsValid() || !upload.IsWritable(); }) ||
            !std::ranges::all_of(batches, [](const auto& batch) { return batch->IsPreparedForGraph(); }))
        {
            return Fail(error, "METEOR transform declaration upload reservation changed recording/descriptors.");
        }
        dispatch->uniform = uploads[0];
        dispatch->inputs = uploads[1];
        dispatch->jobs = uploads[2];
        dispatch->groups = uploads[3];
        dispatch->inputBytes = requests[1].bytes + requests[2].bytes + requests[3].bytes;
        std::memcpy(dispatch->uniform.cpuAddress, constants.data(), sizeof(constants));
        const std::span inputBytes(static_cast<std::byte*>(dispatch->inputs.cpuAddress), plan.InputBytes());
        const std::span jobs(static_cast<MeshTransformDispatch::Job*>(dispatch->jobs.cpuAddress), plan.JobCount());
        const std::span groups(static_cast<MeshTransformDispatch::Group*>(dispatch->groups.cpuAddress),
                               std::size_t(plan.GroupCount()));
        // 실제 스케줄러에서 작은 복사는 제출/합류 비용보다 짧았다. 8 MiB부터
        // 고정 구간을 한 번만 제출하며, 그 아래는 한 구간으로 직접 완료한다.
        // 바이트와 그룹 범위는 따로 나눠 큰 메시 하나에도 워커를 배정한다.
        constexpr std::uint64_t kParallelUploadBytes = 8u << 20;
        constexpr std::size_t kParallelSlices = 8;
        constexpr std::size_t kMinimumParallelWorkers = 4;
        const bool parallel = dispatch->inputBytes >= kParallelUploadBytes && !thread_pool::is_worker_thread() &&
                              ce::get_thread_pool().size() >= kMinimumParallelWorkers;
        const auto slices = parallel ? kParallelSlices : std::size_t{1};
        const auto fill = [&](std::size_t slice)
        {
            ce::profile_scope copyScope{ce::marker<"RenderGraphDeclareMeshFill">()};
            plan.CopyInputRange(inputBytes, inputBytes.size() * slice / slices,
                                inputBytes.size() * (slice + 1) / slices);
            plan.FillGroupRange(groups, groups.size() * slice / slices, groups.size() * (slice + 1) / slices);
            const auto first = jobs.size() * slice / slices;
            const auto last = jobs.size() * (slice + 1) / slices;
            for (std::size_t index = first; index < last; ++index)
            {
                const auto& batch = batches[index];
                const auto& source = *batch->input_;
                MeshTransformDispatch::Job job{batch->objectSpace_ ? math::matrix4x4::identity() : source.World(),
                                               {batch->Count(), source.Geometry().vertexStride,
                                                source.Geometry().vertexAttributeMask,
                                                static_cast<std::uint32_t>(source.Bones().size())},
                                               {},
                                               {}};
                for (const auto& attribute : assets::kVertexAttributeTable)
                {
                    job.offsets[static_cast<std::size_t>(attribute.attribute)] =
                        assets::OffsetOf(source.Geometry().vertexAttributeMask, attribute.attribute);
                }
                const auto& planned = plan.GetJob(index);
                std::copy(planned.m_inputOffsets.begin(), planned.m_inputOffsets.end(), job.inputOffsets.begin());
                std::construct_at(jobs.data() + index, job);
            }
        };
        render_graph_declaration_task preparation;
        preparation.start(slices, slices > 1, fill);
        dispatch->outputs = device.CreateBindings(std::span{scratch.m_outputs}.first(plan.JobCount()));
        preparation.join();
        if (!dispatch->outputs.IsValid() ||
            !std::ranges::all_of(batches, [](const auto& batch) { return batch->IsPreparedForGraph(); }))
        {
            return Fail(error, "METEOR transform work-list allocation changed recording/descriptors.");
        }
        result = std::move(dispatch);
        error.clear();
        return true;
    }

    bool MeshSurfaceBatch::RecordDispatch(RHIEncoder& encoder,
                                          std::span<const own::shared_owner<const MeshSurfaceBatch>> batches,
                                          const MeshTransformDispatch& dispatch, std::string& error)
    {
        ce::profile_scope recordScope{ce::marker<"MaterialMeshDispatchRecord">()};
        for (const auto& batch : batches)
        {
            if (!batch->IsPreparedForGraph() || (batch->recordedStages_.fetch_or(1) & 1) != 0)
            {
                batch->recordedStages_.fetch_or(4);
                return Fail(error, "METEOR transform job has stale uploads/descriptors or was already recorded.");
            }
        }
        encoder.SetPipeline(RHIBindPoint::Compute, batches.front()->pipeline_->GetHandle());
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, dispatch.uniform);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 1, dispatch.inputs);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 2, dispatch.jobs);
        encoder.SetRootBuffer(RHIBindPoint::Compute, 3, dispatch.groups);
        encoder.SetBindings(RHIBindPoint::Compute, 4, dispatch.outputs);
        const auto before = encoder.GetComputeDispatchCount();
        encoder.Dispatch(dispatch.dispatchX, dispatch.dispatchY, 1);
        const auto after = encoder.GetComputeDispatchCount();
        if (before != UINT64_MAX && after != before + 1)
        {
            return Fail(error, "METEOR transform failed to record exactly one native dispatch.");
        }
        static const bool audit = []
        {
            char* value{};
            size_t length{};
            const bool enabled =
                _dupenv_s(&value, &length, "CREATOR_METEOR_TRANSFORM_AUDIT") == 0 && value && value[0] == '1';
            std::free(value);
            return enabled;
        }();
        if (audit)
        {
            std::fprintf(
                stderr,
                "METEOR_TRANSFORM_DISPATCH recording=%llu jobs=%zu points=%llu groups=%u x=%u y=%u native=%llu "
                "inputBytes=%llu frame=%llu view=%llu qpc=%llu\n",
                static_cast<unsigned long long>(batches.front()->recordingId_), batches.size(),
                static_cast<unsigned long long>(dispatch.pointCount), dispatch.groupCount, dispatch.dispatchX,
                dispatch.dispatchY, static_cast<unsigned long long>(before == UINT64_MAX ? UINT64_MAX : after - before),
                static_cast<unsigned long long>(dispatch.inputBytes),
                static_cast<unsigned long long>(batches.front()->auditRealFrame_),
                static_cast<unsigned long long>(batches.front()->auditView_),
                static_cast<unsigned long long>(ce::profiler_service::now()));
        }
        error.clear();
        return true;
    }

    bool MeshSurfaceBatch::Declare(EnhancedRenderGraph& graph, std::string& error) const
    {
        const MeshSurfaceBatch* const batches[]{this};
        return DeclareBatch(graph, batches, error);
    }

    bool MeshSurfaceBatch::DeclareBatch(EnhancedRenderGraph& graph, std::span<const MeshSurfaceBatch* const> batches,
                                        std::string& error)
    {
        struct DeclarationScratch
        {
            std::vector<own::shared_owner<const MeshSurfaceBatch>> transformed, reused, direct, aliases, producers;
            std::vector<EnhancedRenderGraph::RGPassUsage> transformUses, readyUses, reuseUses, clusterUses,
                clusterReadyUses;
            ce::render_pointer_set<MeshSurfaceBatch> selected, producerSet;
            void Clear()
            {
                transformed.clear();
                reused.clear();
                direct.clear();
                aliases.clear();
                producers.clear();
                transformUses.clear();
                readyUses.clear();
                reuseUses.clear();
                clusterUses.clear();
                clusterReadyUses.clear();
                selected.clear();
                producerSet.clear();
            }
        };
        struct ScratchStack
        {
            ce::render_vector<ce::render_unique_ptr<DeclarationScratch>> entries;
            size_t depth{};
        };
        static thread_local ScratchStack stack;
        if (stack.depth == stack.entries.size())
        {
            stack.entries.push_back(ce::make_render_unique<DeclarationScratch>());
        }
        // Upload joins can assist another declaration on this thread. Each depth
        // owns a separate stable object, including when the pointer array grows.
        auto& scratch = *stack.entries[stack.depth++];
        struct ClearScratch
        {
            DeclarationScratch& value;
            size_t& depth;
            ~ClearScratch()
            {
                value.Clear();
                --depth;
            }
        } clearScratch{scratch, stack.depth};
        auto& [transformed, reused, direct, aliases, producers, transformUses, readyUses, reuseUses, clusterUses,
               clusterReadyUses, selected, producerSet] = scratch;
        selected.reset(batches.size());
        producerSet.reset(batches.size());
        for (const auto* batch : batches)
        {
            const auto instance = batch ? batch->self_.lock() : nullptr;
            if (!instance || &graph.DeviceServices() != instance->device_ || !instance->IsPreparedForGraph() ||
                instance->declared_.load() || !selected.insert(instance.operator->()))
            {
                return Fail(error, "Mesh declaration requires distinct fresh instances on their owning recording.");
            }
            auto producer = instance;
            while (producer->declarationOwner_)
            {
                producer = producer->declarationOwner_;
            }
            if (producerSet.insert(producer.operator->()))
            {
                producers.push_back(producer);
            }
            if (producer.operator->() != instance.operator->())
            {
                aliases.push_back(instance);
            }
        }
        for (const auto& owner : std::span{producers})
        {
            if (owner->HasGraphDeclaration())
            {
                if (!owner->GraphOutput(graph).IsValid())
                {
                    return Fail(error, "Shared mesh producer belongs to another graph or resource epoch.");
                }
                continue;
            }
            bool isReuse = false;
            auto& uses = owner->transformedOwner_ ? reuseUses : owner->directClusters_ ? clusterUses : transformUses;
            if (!owner->DeclareResources(graph, uses, isReuse, error))
            {
                return false;
            }
            if (isReuse)
            {
                reused.push_back(owner);
                continue;
            }
            const auto readAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder
                                        ? RGAccessMode::Read
                                        : RGAccessMode::LegacyState;
            auto& ready = owner->directClusters_ ? clusterReadyUses : readyUses;
            ready.push_back({owner->graphOutput_, RHIResourceState::ShaderResource, readAccess});
            if (owner->directClusters_)
            {
                direct.push_back(owner);
            }
            else
            {
                transformed.push_back(owner);
            }
        }
        // Aliases publish the producer's exact version, never a second import
        // or write of the same physical buffer. No readiness/join pass is added.
        for (const auto& instance : std::span{aliases})
        {
            auto producer = instance->declarationOwner_;
            while (producer->declarationOwner_)
            {
                producer = producer->declarationOwner_;
            }
            const auto output = producer->GraphOutput(graph);
            if (!output.IsValid())
            {
                return Fail(error, "Shared mesh producer has no graph output.");
            }
            instance->graph_ = graph.DeclarationGraph();
            instance->graphEpoch_ = graph.DeclarationEpoch();
            instance->graphOutput_ = output;
            instance->declared_.store(true);
        }
        // Instances of one mesh share static inputs; a pass lists each version once.
        NormalizeSceneReads(graph, transformUses);
        NormalizeSceneReads(graph, reuseUses);
        const auto reusedOwners =
            graph.CopyDeclaration(std::span<const own::shared_owner<const MeshSurfaceBatch>>{reused});
        const auto directOwners =
            graph.CopyDeclaration(std::span<const own::shared_owner<const MeshSurfaceBatch>>{direct});
        const auto transformedOwners =
            graph.CopyDeclaration(std::span<const own::shared_owner<const MeshSurfaceBatch>>{transformed});
        if (!reused.empty())
        {
            graph.AddPass(
                "METEOR.MeshWorldReuse", reuseUses,
                [reused = reusedOwners](const auto&)
                {
                    for (const auto& owner : reused)
                    {
                        if (!owner->IsPreparedForGraph())
                        {
                            owner->recordedStages_.fetch_or(4);
                            throw std::runtime_error("Stale reused mesh recording.");
                        }
                        owner->recordedStages_.store(3);
                    }
                },
                true);
        }
        if (!direct.empty())
        {
            NormalizeSceneReads(graph, clusterUses);
            graph.AddPass("METEOR.ClusterPoseUpload", clusterUses,
                          [direct = directOwners](const auto& context)
                          {
                              for (const auto& owner : direct)
                              {
                                  auto destination = RHIBufferSlice::Whole(owner->buffer_);
                                  destination.size = owner->BufferBytes();
                                  if (!context.encoder->CopyBuffer(destination, owner->clusterPoseUpload_))
                                  {
                                      throw std::runtime_error("Cluster pose copy failed");
                                  }
                                  owner->recordedStages_.store(3);
                              }
                          });
            graph.AddPass(
                "METEOR.ClusterGeometryReady", clusterReadyUses, [direct = directOwners](const auto&) {}, true);
        }
        if (transformed.empty())
        {
            error.clear();
            return true;
        }
        own::shared_owner<MeshTransformDispatch> dispatch;
        if (!PrepareDeclarationDispatch(transformed, dispatch, error))
        {
            return false;
        }
        graph.AddPass("METEOR.TransformMeshWorld", transformUses,
                      [transformed = transformedOwners, dispatch](const auto& context)
                      {
                          for (const auto& owner : transformed)
                          {
                              if (!context.graph || !owner->GraphOutput(*context.graph).IsValid())
                              {
                                  throw std::runtime_error("METEOR transform output belongs to another graph.");
                              }
                          }
                          std::string failure;
                          if (!context.encoder || !RecordDispatch(*context.encoder, transformed, *dispatch, failure))
                          {
                              throw std::runtime_error("METEOR batched transform recording failed: " + failure);
                          }
                      });
        graph.AddPass(
            "METEOR.MeshWorldReady", readyUses,
            [transformed = transformedOwners](const auto&)
            {
                for (const auto& owner : transformed)
                {
                    if (!owner->IsPreparedForGraph())
                    {
                        owner->recordedStages_.fetch_or(4);
                        throw std::runtime_error("Stale mesh readiness recording.");
                    }
                    owner->recordedStages_.fetch_or(2);
                }
            },
            true);
        error.clear();
        return true;
    }

    bool MeshSurfaceBatch::DeclareResources(EnhancedRenderGraph& graph,
                                            std::vector<EnhancedRenderGraph::RGPassUsage>& inputs, bool& reused,
                                            std::string& error) const
    {
        reused = false;
        if (&graph.DeviceServices() != device_ || !IsPreparedForGraph() || recordedStages_.load() != 0 ||
            declared_.exchange(true))
        {
            return Fail(error, "Mesh declaration needs a fresh prepared world transform on its owning recording.");
        }
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        graph_ = graph.DeclarationGraph();
        graphEpoch_ = graph.DeclarationEpoch();
        if (staticBuffers_)
        {
            auto& buffers = *staticBuffers_;
            if (buffers.graph != graph.DeclarationGraph() || buffers.graphEpoch != graphEpoch_)
            {
                buffers.graph = graph.DeclarationGraph();
                buffers.graphEpoch = graphEpoch_;
                const bool needsCopy = !buffers.usable;
                const RHIBufferHandle handles[]{buffers.vertices.buffer, buffers.indices.buffer, buffers.lods.buffer,
                                                buffers.meshlets.buffer};
                for (unsigned i = 0; i < 4; ++i)
                {
                    buffers.graphBuffers[i] = graph.ImportBuffer(
                        handles[i],
                        needsCopy ? RHIResourceState::Common
                                  : (i == 1 ? RHIResourceState::IndexBuffer : RHIResourceState::ShaderResource),
                        "METEOR.Mesh.StaticInput");
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
                    graph.AddPass("METEOR.MeshInputUpload",
                                  {{buffers.graphBuffers[0], RHIResourceState::CopyDest, writeAccess},
                                   {buffers.graphBuffers[1], RHIResourceState::CopyDest, writeAccess},
                                   {buffers.graphBuffers[2], RHIResourceState::CopyDest, writeAccess},
                                   {buffers.graphBuffers[3], RHIResourceState::CopyDest, writeAccess}},
                                  [retained = staticBuffers_](const auto& context)
                                  {
                                      if (retained->sparse)
                                      {
                                          RHIBarrierBatch activation{};
                                          activation.aliasBuffers = {&retained->meshlets.buffer, 1};
                                          context.encoder->ResourceBarriers(activation);
                                      }
                                      const RHIBufferSlice destinations[]{retained->vertices, retained->indices,
                                                                          retained->lods, retained->meshlets};
                                      for (unsigned i = 0; i < 4; ++i)
                                      {
                                          auto destination = destinations[i];
                                          if (i == 3 && retained->sparse)
                                          {
                                              continue;
                                          }
                                          if (!context.encoder->CopyBuffer(destination, retained->staging[i]))
                                          {
                                              throw std::runtime_error("Static mesh buffer copy was rejected.");
                                          }
                                      }
                                      for (const auto& copy : retained->sparseCopies)
                                      {
                                          if (!context.encoder->CopyBuffer(copy.first, copy.second))
                                          {
                                              throw std::runtime_error("Sparse cluster packet copy was rejected.");
                                          }
                                      }
                                      retained->usable = true;
                                  });
                }
            }
            // Index reads are represented as well: downstream raster passes depend
            // on this posed output, so their index buffer is ready on every backend.
            for (unsigned i = 0; i < 4; ++i)
            {
                inputs.push_back({buffers.graphBuffers[i],
                                  i == 1 ? RHIResourceState::IndexBuffer : RHIResourceState::ShaderResource,
                                  readAccess});
            }
        }
        auto& state = transformedOwner_ ? transformedOwner_->bufferState_ : bufferState_;
        graphOutput_ = graph.ImportBuffer(buffer_, state, "METEOR.Mesh.WorldPoints", &state);
        // Import the accepted tracked state, including a preceding readback.
        // The graph derives the next read transition; no fixed buffer end state.
        if (transformedOwner_)
        {
            inputs.push_back({graphOutput_, RHIResourceState::ShaderResource, readAccess});
            reused = true;
            error.clear();
            return true;
        }
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            graphOutput_ = graph.Write(graphOutput_);
        }
        inputs.push_back({graphOutput_,
                          directClusters_ ? RHIResourceState::CopyDest : RHIResourceState::UnorderedAccess,
                          writeAccess});
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
                                                  RHILayout::Srv(6), RHILayout::UavBufferTable(1, 4096)};
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
                                             own::shared_owner<const MeshSurfaceBatch> vertices,
                                             std::span<const MeshSurfaceSample> samples,
                                             own::shared_owner<const MeshSurfaceBatch>& result, std::string& error)
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
        auto candidate = own::make_shared<MeshSurfaceBatch>(MeshSurfaceBatch::ConstructionKey{});
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
        description.debugName = L"METEOR.Material.TriangleSamples";
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
