#include "../../Engine/RenderEngine/RenderArena.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Arena.h"
#else
import Creator.Core.Arena;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../Engine/RenderEngine/RHI/RHIDescriptorVersionPolicy.h"
#include "../../Engine/RenderEngine/RHI/RHISoftwareEncoder.h"
#include "../../Engine/RenderEngine/RHI/RHISoftwareRecorder.h"
#include <iostream>

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    class ProbeValidator final : public IRHISoftwareCommandValidator
    {
      public:
        bool allow{};
        bool Validate(RHISoftwareCommandType, const void*, size_t, const RHISoftwareEncoderState&) const override
        {
            return allow;
        }
    };

    class ProbeEncoder final : public RHIEncoder
    {
      public:
        uint64_t draws{};
        uint64_t dispatches{};
        uint32_t textures{};
        uint32_t roots{};
        bool failIndirect{};
        bool throwDispatch{};
        std::vector<uint32_t> order;

        void SetViewportAndScissor(uint32_t, uint32_t) override {}
        void SetPipeline(RHIBindPoint, RHIPipelineHandle) override {}
        void SetPrimitiveTopology(RHIPrimitiveTopology) override {}
        void SetBindings(RHIBindPoint, uint32_t, const RHIBindingTable&) override {}
        void SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&) override {}
        void SetConstantBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice& slice) override
        {
            Require(slice.cpuAddress == nullptr, "CPU upload pointer leaked into replay.");
        }
        void SetRootBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override {}
        void SetVertexBuffer(const RHIBufferSlice&, uint32_t) override {}
        void SetIndexBuffer(const RHIBufferSlice&, RHIFormat) override {}
        void Draw(uint32_t count, uint32_t, uint32_t, uint32_t) override
        {
            ++draws;
            order.push_back(count);
        }
        void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
        bool DispatchIndirect(RHIBufferHandle, uint64_t) override { return !failIndirect; }
        bool DispatchMeshIndirectBatch(RHIBufferHandle, uint64_t, uint32_t, std::span<const uint32_t> values) override
        {
            Require(values.size() == 3 && values[0] == 7 && values[2] == 9, "Borrowed root slots changed.");
            roots += static_cast<uint32_t>(values.size());
            return true;
        }
        void Dispatch(uint32_t, uint32_t, uint32_t) override
        {
            if (throwDispatch)
            {
                throw std::runtime_error("probe native exception");
            }
            ++dispatches;
        }
        uint64_t GetComputeDispatchCount() const override { return dispatches; }
        void BindRenderTargets(const RHIRenderTargetBinding&) override {}
        void ClearRenderTargets(const RHIRenderTargetBinding&, const float* rgba) override
        {
            Require(rgba[0] == 0.25f && rgba[3] == 1.0f, "Borrowed clear color changed.");
        }
        void ClearDepthTarget(const RHIRenderTargetBinding&, float) override {}
        void ResourceBarriers(const RHIBarrierBatch& value) override
        {
            Require(value.textureTransitions.size() == 1 && value.textureTransitions[0].texture.id == 11,
                    "Borrowed texture transitions changed.");
            Require(value.bufferTransitions.size() == 1 && value.bufferTransitions[0].buffer.id == 12,
                    "Borrowed buffer transitions changed.");
            Require(value.uavTextures.size() == 1 && value.uavTextures[0].id == 13, "UAV texture payload changed.");
            Require(value.uavBuffers.size() == 1 && value.uavBuffers[0].id == 14, "UAV buffer payload changed.");
            Require(value.aliasTextures.size() == 1 && value.aliasTextures[0].id == 15,
                    "Alias texture payload changed.");
            Require(value.aliasBuffers.size() == 1 && value.aliasBuffers[0].id == 16, "Alias buffer payload changed.");
            ++textures;
        }
        void UavBarrier(std::span<const RHITextureHandle>) override {}
        void UavBarrierBuffers(std::span<const RHIBufferHandle>) override {}
        void CopyResource(RHITextureHandle, RHITextureHandle) override {}
        void ClearUnorderedAccess(const RHIBindingDesc&, const float*) override {}
        void CopyToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t) override {}
        void CopyVolumeToReadback(const RHIReadback&, RHITextureHandle, uint32_t) override {}
        void CopyPartialToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t) override {}
        void CopyBufferToReadback(const RHIReadback&, RHIBufferHandle, uint64_t, uint64_t) override {}
        void CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t, uint32_t) override {}
        void ClearRenderTargetRect(const RHIRenderTargetBinding&, const float*, const RHIRect&) override {}
    };
} // namespace

int main()
{
    try
    {
        {
            ce::render_arena_objects objects;
            ce::fixed_array<int, 4> destruction{};
            size_t destroyed{};
            struct Capture
            {
                own::shared_owner<int> owner;
                int id;
                ce::fixed_array<int, 4>* order;
                size_t* count;
                ~Capture() { (*order)[(*count)++] = id; }
            };
            auto owner = own::make_shared<int>(17);
            own::weak_owner<int> weak = owner;
            auto* first = objects.emplace<Capture>(owner, 1, &destruction, &destroyed);
            const auto address = reinterpret_cast<uintptr_t>(first);
            Require(address % alignof(Capture) == 0, "Declaration capture alignment failed.");
            objects.emplace<Capture>(owner, 2, &destruction, &destroyed);
            ce::fixed_array owners{owner, owner};
            const auto copied = objects.copy(std::span<const own::shared_owner<int>>{owners});
            Require(copied.size() == 2 && *copied[1] == 17, "Declaration owner array copy failed.");
            owners.fill({});
            owner.reset();
            Require(!weak.expired() && first->id == 1, "Declaration growth lost live captures.");
            struct alignas(4096) AlignedCapture
            {
                std::byte bytes[4096]{};
            };
            auto* alignedCapture = objects.emplace<AlignedCapture>();
            Require(reinterpret_cast<uintptr_t>(alignedCapture) % 4096 == 0, "Overaligned declaration capture failed.");
            const auto retained = objects.stats().retained_bytes;
            objects.reset();
            Require(weak.expired() && destroyed == 2 && destruction[0] == 2 && destruction[1] == 1,
                    "Declaration captures were not destroyed in reverse order.");
            Require(objects.stats().retained_bytes == retained && objects.stats().used_bytes == 0,
                    "Declaration reset failed to retain raw blocks.");
            auto* recycled = objects.emplace<Capture>(own::shared_owner<int>{}, 3, &destruction, &destroyed);
            Require(reinterpret_cast<uintptr_t>(recycled) == address,
                    "Declaration reset failed to reuse an aligned object address.");
            objects.reset();
        }
        {
            ce::fixed_array<int, 4096> identities{};
            ce::render_pointer_set<int> set;
            for (size_t repeat = 0; repeat < 32; ++repeat)
            {
                set.reset(identities.size());
                for (const auto& identity : identities)
                {
                    Require(set.insert(&identity) && !set.insert(&identity),
                            "Flat declaration identity set lost uniqueness.");
                }
                set.clear();
            }
        }
        {
            ce::render_arena_objects objects;
            int copies{};
            int live{};
            struct ThrowingCopy
            {
                int* copies;
                int* live;
                ThrowingCopy(int& count, int& owners) : copies(&count), live(&owners) { ++*live; }
                ThrowingCopy(const ThrowingCopy& other) : copies(other.copies), live(other.live)
                {
                    if (++*copies == 2)
                    {
                        throw std::runtime_error("injected declaration copy failure");
                    }
                    ++*live;
                }
                ~ThrowingCopy() { --*live; }
            };
            const ce::fixed_array values{ThrowingCopy(copies, live), ThrowingCopy(copies, live)};
            bool rejected{};
            try
            {
                objects.copy(std::span<const ThrowingCopy>{values});
            }
            catch (const std::runtime_error&)
            {
                rejected = true;
            }
            Require(rejected && live == 2, "Partial declaration copy retained constructed owners.");
            objects.reset();
            Require(live == 2, "Failed declaration copy destroyed source objects.");
        }
        // The render allocator owns aligned storage, STL proxy/control blocks,
        // and objects released by a consumer on another thread.
        struct alignas(256) AlignedStorage
        {
            uint64_t values[32]{};
        };
        struct Left
        {
            virtual ~Left() = default;
        };
        struct Right
        {
            virtual ~Right() = default;
        };
        struct Multiple final : Left, Right
        {
            uint32_t value{123};
        };
        {
            auto root = ce::make_render_shared<Multiple>();
            ce::pooled_owner<Left> left = root;
            const auto right = ce::dynamic_pooled_cast<Right>(left);
            const auto restored = ce::dynamic_pooled_cast<Multiple>(right);
            Require(right && restored && restored->value == 123 && root.use_count() == 4,
                    "Polymorphic RHI lease lost its concrete ownership or adjusted pointer.");
            ce::weak_pooled_owner<Left> observed = left;
            root.reset();
            Require(!observed.expired(), "RHI weak lease expired before outstanding aliases.");
        }
        auto aligned = ce::make_render_unique<AlignedStorage>();
        Require(reinterpret_cast<uintptr_t>(aligned.operator->()) % alignof(AlignedStorage) == 0,
                "Render allocator alignment failed.");
        Require(mi_check_owned(aligned.operator->()), "Render object bypassed mimalloc.");
        aligned->values[31] = 87;
        ce::render_vector<uint64_t> allocatedValues(4096, 123);
        Require(mi_check_owned(allocatedValues.data()), "Render vector bypassed mimalloc.");
        auto sharedValue = ce::make_render_shared<uint64_t>(456);
        std::thread consumer(
            [value = std::move(aligned), values = std::move(allocatedValues), shared = std::move(sharedValue)]
            {
                Require(value->values[31] == 87 && values[4095] == 123 && *shared == 456,
                        "Cross-thread render allocation contents changed.");
            });
        consumer.join();
        ce::render_vector<ce::render_unique_ptr<AlignedStorage>> orphanedAllocations;
        std::thread producer(
            [&]
            {
                for (size_t index = 0; index < 1024; ++index)
                {
                    auto value = ce::make_render_unique<AlignedStorage>();
                    value->values[0] = index;
                    orphanedAllocations.push_back(std::move(value));
                }
            });
        producer.join();
        for (size_t index = 0; index < orphanedAllocations.size(); ++index)
        {
            Require(orphanedAllocations[index]->values[0] == index,
                    "Allocation owner thread exit invalidated renderer storage.");
        }
        orphanedAllocations.clear();
        bool overflowRejected = false;
        try
        {
            auto* impossible = ce::render_allocator<uint64_t>{}.allocate(SIZE_MAX);
            ce::render_allocator<uint64_t>{}.deallocate(impossible, SIZE_MAX);
        }
        catch (const std::bad_array_new_length&)
        {
            overflowRejected = true;
        }
        Require(overflowRejected, "Render allocator multiplication overflow was accepted.");
        RHICommandFailure resetFailure;
        resetFailure.stage = RHICommandFailureStage::Translation;
        resetFailure.recordingId = 9;
        resetFailure.targetIndex = 3;
        resetFailure.commandIndex = 17;
        resetFailure.commandType = RHISoftwareCommandType::Draw;
        resetFailure.backendGeneration = 2;
        resetFailure.executionUncertain = true;
        resetFailure.message = std::string(256, 'x');
        const auto diagnosticCapacity = resetFailure.message.capacity();
        resetFailure.Reset();
        Require(resetFailure.stage == RHICommandFailureStage::Recording && !resetFailure.recordingId &&
                    resetFailure.targetIndex == UINT32_MAX && resetFailure.commandIndex == UINT64_MAX &&
                    resetFailure.commandType == RHISoftwareCommandType::Count && !resetFailure.backendGeneration &&
                    !resetFailure.executionUncertain && resetFailure.message.empty() &&
                    resetFailure.message.capacity() == diagnosticCapacity,
                "Failure reset changed defaults or discarded diagnostic storage.");
        // Nested containers and allocator-aware pairs must keep the writer
        // domain through construction, copy and cross-domain move assignment.
        {
            ce::render_arena_resource first;
            ce::render_arena_resource second;
            ce::render_arena_vector<ce::render_arena_vector<uint64_t>> nested(3, &first);
            nested[1].assign(64, 123);
            Require(nested[1].get_allocator().resource() == &first, "Nested allocator lost its domain.");
            ce::render_arena_map<int, ce::render_arena_vector<uint64_t>> groups{&first};
            groups[7].assign(32, 456);
            Require(groups[7].get_allocator().resource() == &first, "Map value lost its arena domain.");
            ce::render_arena_vector<uint64_t> moved{&second};
            moved = std::move(nested[1]);
            Require(moved.get_allocator().resource() == &second && moved.front() == 123,
                    "Cross-domain move stole storage from an earlier lifetime.");
            const auto independent = moved;
            Require(!independent.get_allocator().resource() && independent.front() == 123,
                    "Plain container copy borrowed an arena without retaining its frame owner.");
        }
        {
            ce::RenderArenaPool concurrentPool;
            std::atomic<bool> intact{true};
            ce::fixed_array<std::thread, 8> clients;
            for (size_t client = 0; client < clients.size(); ++client)
            {
                clients[client] = std::thread(
                    [&, client]
                    {
                        for (size_t iteration = 0; iteration < 2000; ++iteration)
                        {
                            auto lease = concurrentPool.Acquire();
                            auto* words = static_cast<uint64_t*>(lease->Resource(0).allocate(4096, alignof(uint64_t)));
                            std::fill_n(words, 512, client * 2000 + iteration);
                            auto owner = lease;
                            lease.reset();
                            std::this_thread::yield();
                            if (words[0] != client * 2000 + iteration || words[511] != words[0])
                            {
                                intact.store(false, std::memory_order_relaxed);
                            }
                        }
                    });
            }
            for (auto& client : clients)
            {
                client.join();
            }
            Require(intact.load(), "Concurrent pool reuse overlapped live arena ownership.");
        }
        {
            RHISoftwareCommandArena chunks;
            RHICommandRecordingStats stats;
            auto* first = static_cast<std::byte*>(chunks.Allocate(40, 8, stats));
            auto* second = static_cast<std::byte*>(chunks.Allocate(40, 8, stats));
            Require(second - first == 40 && stats.arenaBlocks == 1,
                    "Compact commands gained alignment gaps or activated a block per command.");
            RHISoftwareCommandArena moved(std::move(chunks));
            auto* third = static_cast<std::byte*>(moved.Allocate(40, 8, stats));
            Require(third - first == 80, "Command arena move lost the live chunk cursor.");
            auto* independent = static_cast<std::byte*>(chunks.Allocate(40, 8, stats));
            Require(independent != first && independent != second && independent != third,
                    "Moved-from command arena retained another writer's chunk.");
        }
        ce::RenderArenaPool arenaPool;
        auto arenaLease = arenaPool.Acquire();
        auto* originalSet = arenaLease.operator->();
        auto& arenaResource = arenaLease->Resource(0);
        void* firstBlock{};
        {
            ce::render_arena_vector<uint64_t> values{&arenaResource};
            values.resize(20000, 0xabc);
            firstBlock = values.data();
            auto* alignedData = static_cast<std::byte*>(arenaResource.allocate(70000, 4096));
            Require(reinterpret_cast<uintptr_t>(alignedData) % 4096 == 0, "Arena over-alignment failed.");
            alignedData[69999] = std::byte{0x5a};
            Require(values.front() == 0xabc && values.back() == 0xabc, "Arena growth moved live payloads.");
        }
        const auto retained = arenaResource.stats();
        auto rhiOwner = arenaLease;
        auto gpuOwner = arenaLease;
        arenaLease.reset();
        auto overlap = arenaPool.Acquire();
        Require(overlap.operator->() != originalSet && arenaResource.stats().used_bytes != 0,
                "Live frame arena was reset while asynchronous owners remained.");
        rhiOwner.reset();
        Require(arenaResource.stats().used_bytes != 0, "CPU completion reset a GPU-owned arena.");
        gpuOwner.reset();
        auto recycled = arenaPool.Acquire();
        Require(recycled.operator->() == originalSet && recycled->Resource(0).stats().resets == 1 &&
                    recycled->Resource(0).stats().used_bytes == 0 &&
                    recycled->Resource(0).stats().retained_bytes == retained.retained_bytes,
                "Last owner did not reset and retain arena blocks.");
        {
            ce::render_arena_vector<uint64_t> values{&recycled->Resource(0)};
            values.resize(20000, 0xdef);
            Require(values.data() == firstBlock && recycled->Resource(0).stats().block_count == retained.block_count,
                    "Next frame did not reuse retained arena storage.");
        }
        ce::fixed_array<std::thread, ce::RenderArenaSet::DomainCount> writers;
        for (size_t domain = 0; domain < writers.size(); ++domain)
        {
            writers[domain] = std::thread(
                [recycled, domain]
                {
                    ce::render_arena_vector<size_t> values{&recycled->Resource(domain)};
                    for (size_t index = 0; index < 10000; ++index)
                    {
                        values.push_back(domain * 10000 + index);
                    }
                    Require(values.back() == domain * 10000 + 9999, "Independent arena writer domains collided.");
                });
        }
        for (auto& writer : writers)
        {
            writer.join();
        }
        ce::RenderArenaLease escaped;
        {
            ce::RenderArenaPool shortLivedPool;
            escaped = shortLivedPool.Acquire();
            auto* value = static_cast<uint32_t*>(escaped->Resource(0).allocate(sizeof(uint32_t), alignof(uint32_t)));
            *value = 77;
        }
        std::thread lastConsumer([escaped = std::move(escaped)]() mutable { escaped.reset(); });
        lastConsumer.join();
        {
            ce::RenderArenaPool boundedPool;
            {
                auto large = boundedPool.Acquire();
                auto* oversized = static_cast<std::byte*>(large->Resource(0).allocate(17 * 1024 * 1024, 16));
                oversized[0] = std::byte{0x5a};
            }
            auto fresh = boundedPool.Acquire();
            Require(fresh->RetainedBytes() == 0, "Arena pool retained an oversized free set.");
        }
        {
            ce::RenderArenaPool exceptionPool;
            ce::RenderArenaSet* failedSet{};
            try
            {
                auto lease = exceptionPool.Acquire();
                failedSet = lease.operator->();
                ce::render_arena_vector<own::shared_owner<uint32_t>> values{&lease->Resource(0)};
                values.push_back(own::make_shared<uint32_t>(19));
                throw std::runtime_error("expected arena early exit");
            }
            catch (const std::runtime_error&)
            {
            }
            auto next = exceptionPool.Acquire();
            Require(next.operator->() == failedSet && next->Resource(0).stats().used_bytes == 0,
                    "Early exit did not destroy containers before resetting arena storage.");
        }
        ProbeValidator validator;
        RHISoftwareCommandList list(4 * 1024 * 1024, 1024 * 1024);
        RHISoftwareEncoder writer(list, validator);
        Require(!writer.DispatchIndirect({1}, 0), "Unsupported operation accepted.");
        Require(list.GetCommandCount() == 0 && list.GetPayloadBytes() == 0, "Rejected operation mutated list.");
        validator.allow = true;
        {
            ce::fixed_array<uint32_t, 3> roots{7, 8, 9};
            Require(writer.DispatchMeshIndirectBatch({1}, 0, 1, roots), "Valid root slots rejected.");
            roots.fill(0);
            ce::fixed_array<RHITransition, 1> transitions{{{{11}, RHIResourceState::Common, RHIResourceState::CopySource}}};
            ce::fixed_array<RHIBufferTransition, 1> buffers{
                {{{12}, RHIResourceState::Common, RHIResourceState::CopySource}}};
            ce::fixed_array<RHITextureHandle, 1> uavTextures{{{13}}}, aliasTextures{{{15}}};
            ce::fixed_array<RHIBufferHandle, 1> uavBuffers{{{14}}}, aliasBuffers{{{16}}};
            writer.ResourceBarriers({transitions, buffers, uavTextures, uavBuffers, aliasTextures, aliasBuffers});
            transitions[0].texture.id = 0;
            buffers[0].buffer.id = 0;
            uavTextures[0].id = aliasTextures[0].id = 0;
            uavBuffers[0].id = aliasBuffers[0].id = 0;
            float color[4]{0.25f, 0.5f, 0.75f, 1.0f};
            writer.ClearRenderTargets({}, color);
            color[0] = 0;
            RHIBufferSlice slice{};
            slice.cpuAddress = &slice;
            writer.SetConstantBuffer(RHIBindPoint::Compute, 0, slice);
        }
        for (uint32_t index = 0; index < 4096; ++index)
        {
            writer.Draw(index, 1, 0, 0);
        }
        writer.Dispatch(1, 1, 1);
        Require(writer.GetComputeDispatchCount() == UINT64_MAX, "Software count pretends to be native count.");
        Require(list.GetRecordingStats().computeDispatchCommands == 1, "Logical dispatch count incorrect.");
        std::string error;
        Require(list.Seal(error), "Seal failed.");
        Require(!list.Seal(error), "Double seal accepted.");
        RHISoftwareCommandList moved(std::move(list));
        ProbeEncoder native;
        RHICommandTranslationStats stats;
        RHICommandFailure failure;
        Require(!list.Replay(native, stats, failure), "Moved-from list replayed.");
        Require(moved.Replay(native, stats, failure), "Replay failed.");
        Require(stats.commandCount == 4101 && stats.nativeComputeDispatches == 1, "Translation counts incorrect.");
        Require(native.draws == 4096 && native.textures == 1 && native.roots == 3, "Replay coverage incorrect.");
        for (uint32_t index = 0; index < 4096; ++index)
        {
            Require(native.order[index] == index, "Command order changed.");
        }
        Require(!moved.Replay(native, stats, failure), "Consumed list replayed twice.");

        RHISoftwareCommandList tiny(1, 1);
        RHISoftwareEncoder tinyWriter(tiny, validator);
        tinyWriter.Draw(1, 1, 0, 0);
        Require(!tiny.Seal(error), "Command budget overflow sealed successfully.");
        RHISoftwareCommandList tinyPayload(1024, 1);
        RHISoftwareEncoder tinyPayloadWriter(tinyPayload, validator);
        ce::fixed_array<uint32_t, 3> slots{7, 8, 9};
        Require(!tinyPayloadWriter.DispatchMeshIndirectBatch({1}, 0, 1, slots), "Payload budget overflow accepted.");
        Require(tinyPayload.GetCommandCount() == 0 && tinyPayload.GetPayloadBytes() == 0 &&
                    tinyPayload.GetRecordingStats().allocatedBytes == 0 && tinyPayload.GetError().empty(),
                "Rejected bool payload changed command state, budget or arena storage.");
        tinyPayloadWriter.Draw(71, 1, 0, 0);
        Require(tinyPayload.Seal(error) && tinyPayload.Replay(native, stats, failure) && stats.commandCount == 1 &&
                    native.order.back() == 71,
                "Bool payload rejection prevented the direct draw fallback.");
        {
            // Both indirect-batch bool APIs must preflight the opcode before
            // copying roots. A smaller direct Draw still fits the same target.
            for (uint32_t mode = 0; mode < 2; ++mode)
            {
                auto budget = ce::make_render_shared<RHISoftwareRecordingBudget>();
                budget->maxCommandBytes = 48;
                budget->maxPayloadBytes = 12;
                RHISoftwareCommandList bounded(1024, 1024, budget);
                RHISoftwareEncoder boundedWriter(bounded, validator);
                const bool accepted = mode == 0 ? boundedWriter.DispatchMeshIndirectBatch({1}, 0, 1, slots)
                                                : boundedWriter.DrawIndexedIndirectBatch({1}, 0, 1, slots);
                Require(!accepted && bounded.GetError().empty() && bounded.GetCommandCount() == 0 &&
                            budget->commandBytes == 0 && budget->payloadBytes == 0 &&
                            bounded.GetRecordingStats().allocatedBytes == 0,
                        "Bool command rejection consumed a shared target's payload budget.");
                boundedWriter.Draw(72 + mode, 1, 0, 0);
                Require(bounded.Seal(error) && bounded.Replay(native, stats, failure) && stats.commandCount == 1 &&
                            native.order.back() == 72 + mode && budget->commandBytes == 0 && budget->payloadBytes == 0,
                        "Shared target budget did not recover for bool fallback and replay.");
            }
            RHISoftwareCommandList fixedBudget(48, 1024);
            RHISoftwareEncoder fixedWriter(fixedBudget, validator);
            Require(!fixedWriter.CopyBuffer({}, {}) && fixedBudget.GetError().empty() &&
                        fixedBudget.GetCommandBytes() == 0,
                    "Fixed bool command budget failure became sticky.");
            fixedWriter.Draw(74, 1, 0, 0);
            Require(fixedBudget.Seal(error) && fixedBudget.Replay(native, stats, failure) && stats.commandCount == 1 &&
                        native.order.back() == 74,
                    "Fixed bool command rejection blocked its fallback.");
        }

        RHISoftwareCommandList rejected(1024, 1024);
        RHISoftwareEncoder rejectedWriter(rejected, validator);
        Require(rejectedWriter.DispatchIndirect({1}, 0), "Validated indirect command rejected.");
        rejectedWriter.Draw(1, 1, 0, 0);
        Require(rejected.Seal(error), "Rejected-native case seal failed.");
        native.failIndirect = true;
        const auto drawsBefore = native.draws;
        Require(!rejected.Replay(native, stats, failure), "Native rejection hidden.");
        Require(failure.commandIndex == 0 && failure.commandType == RHISoftwareCommandType::DispatchIndirect,
                "Native failure provenance missing.");
        Require(native.draws == drawsBefore, "Replay continued after native rejection.");
        Require(!rejected.Replay(native, stats, failure), "Failed list replayed twice.");

        RHISoftwareCommandList throwing(1024, 1024);
        RHISoftwareEncoder throwingWriter(throwing, validator);
        throwingWriter.Dispatch(1, 1, 1);
        Require(throwing.Seal(error), "Exception case seal failed.");
        native.throwDispatch = true;
        Require(!throwing.Replay(native, stats, failure) && failure.message == "probe native exception",
                "Native exception lost.");
        RHISoftwareRecorder recorder(validator);
        RHISoftwareRecordingDesc recordingDesc{{1, 1, RHIQueueKind::Graphics}, 2, 16384, 16384};
        Require(recorder.GetRecordingId() == 0, "Inactive recorder published a recording identity.");
        Require(recorder.BeginRecording(recordingDesc, error), "Recording session failed.");
        const uint64_t firstRecordingId = recorder.GetRecordingId();
        Require(firstRecordingId != 0, "Successful BeginRecording did not publish a recording identity.");
        Require(!recorder.BeginRecording(recordingDesc, error), "Active session overwritten.");
        Require(recorder.GetRecordingId() == firstRecordingId, "Rejected BeginRecording replaced the active identity.");
        recorder.AcquireEncoder(0).Draw(1, 1, 0, 0);
        recorder.AcquireEncoder(0).Draw(2, 1, 0, 0);
        recorder.AcquireEncoder(1).Draw(3, 1, 0, 0);
        auto owner = own::make_shared<uint32_t>(42);
        own::weak_owner<uint32_t> weakOwner = owner;
        RHISoftwareBatchDesc batchDesc;
        batchDesc.queue = recordingDesc.queue;
        batchDesc.backendGeneration = 1;
        batchDesc.lifetimeToken = owner;
        RHISoftwareBatch batch;
        const ce::fixed_array<uint32_t, 2> duplicate{0, 0};
        Require(!recorder.FinishRecording(duplicate, batchDesc, batch, error), "Duplicate target accepted.");
        const ce::fixed_array<uint32_t, 1> omitted{0};
        Require(!recorder.FinishRecording(omitted, batchDesc, batch, error), "Nonempty target omitted.");
        Require(recorder.GetRecordingId() == firstRecordingId, "Rejected seal replaced the active recording identity.");
        const ce::fixed_array<uint32_t, 2> order{0, 1};
        Require(recorder.FinishRecording(order, batchDesc, batch, error), "Software batch seal failed.");
        Require(batch.GetRecordingId() == firstRecordingId && recorder.GetRecordingId() == 0,
                "Sealing did not transfer the recording identity to the batch.");
        Require(batch.IsValid() && batch.GetRecordingStats().commandCount == 3 && batch.GetTargetCount() == 2,
                "Software batch metadata incorrect.");
        RHISoftwareBatch movedBatch(std::move(batch));
        Require(!batch.IsValid() && movedBatch.IsValid(), "Software batch move duplicated submission rights.");
        Require(batch.GetRecordingId() == 0 && movedBatch.GetRecordingId() == firstRecordingId,
                "Moving a sealed batch lost or duplicated its recording identity.");
        batchDesc.lifetimeToken.reset();
        owner.reset();
        Require(!weakOwner.expired(), "Software batch lost its lifetime owner.");
        movedBatch = RHISoftwareBatch{};
        Require(weakOwner.expired(), "Cancelled software batch retained its lifetime owner.");
        Require(recorder.BeginRecording(recordingDesc, error), "Recorder could not restart.");
        const uint64_t cancelledRecordingId = recorder.GetRecordingId();
        Require(cancelledRecordingId != 0 && cancelledRecordingId != firstRecordingId,
                "Restarted recorder reused a sealed recording identity.");
        recorder.AcquireEncoder(0).Draw(1, 1, 0, 0);
        recorder.CancelRecording();
        Require(recorder.GetRecordingId() == 0, "Cancelled recorder retained its recording identity.");
        Require(recorder.GetRecordingStats().commandCount == 0, "Cancelled commands survived.");
        Require(recorder.BeginRecording(recordingDesc, error), "Cancelled recorder could not restart.");
        const uint64_t restartedRecordingId = recorder.GetRecordingId();
        Require(restartedRecordingId != 0 && restartedRecordingId != cancelledRecordingId &&
                    restartedRecordingId != firstRecordingId,
                "A new session reused a cancelled recording identity.");
        recorder.AcquireEncoder(0).ClearRenderTargets({}, nullptr);
        batchDesc.lifetimeToken = own::make_shared<uint32_t>(1);
        Require(!recorder.FinishRecording(omitted, batchDesc, batch, error), "Null color accepted.");
        bool sealRejected = false;
        try
        {
            recorder.AcquireEncoder(0);
        }
        catch (const std::runtime_error&)
        {
            sealRejected = true;
        }
        Require(sealRejected, "Invalid previous pass was accepted by the reused diagnostic buffer.");
        recorder.CancelRecording();
        Require(recorder.BeginRecording(recordingDesc, error), "Recorder failed after a seal error.");
        recorder.AcquireEncoder(0).Draw(1, 1, 0, 0);
        recorder.AcquireEncoder(0).Draw(2, 1, 0, 0);
        Require(recorder.GetRecordingStats().commandCount == 2, "Previous seal error contaminated the next session.");
        recorder.CancelRecording();

        recordingDesc.maxCommandBytes = 65536;
        Require(recorder.BeginRecording(recordingDesc, error), "Concurrent recording session failed.");
        const uint64_t concurrentRecordingId = recorder.GetRecordingId();
        ce::fixed_array<std::exception_ptr, 2> workerErrors;
        ce::fixed_array<std::thread, 2> threads;
        for (uint32_t target = 0; target < 2; ++target)
        {
            threads[target] = std::thread(
                [&, target]
                {
                    try
                    {
                        Require(concurrentRecordingId != 0 && recorder.GetRecordingId() == concurrentRecordingId,
                                "Recording worker did not observe the owner's recording identity.");
                        auto& encoder = recorder.AcquireEncoder(target);
                        for (uint32_t index = 0; index < 200; ++index)
                        {
                            encoder.Draw(index, 1, 0, 0);
                        }
                    }
                    catch (...)
                    {
                        workerErrors[target] = std::current_exception();
                    }
                });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }
        Require(!workerErrors[0] && !workerErrors[1], "Independent recording targets failed.");
        Require(recorder.FinishRecording(order, batchDesc, batch, error), "Concurrent targets did not seal.");
        Require(batch.GetRecordingId() == concurrentRecordingId && recorder.GetRecordingId() == 0,
                "Concurrent recording workers and the sealed batch used different identities.");
        Require(batch.GetRecordingStats().commandCount == 400, "Concurrent recording lost commands.");
        batch = RHISoftwareBatch{};
        recordingDesc.targetCount = 1;
        recordingDesc.maxCommandBytes = 65536;
        recordingDesc.maxPayloadBytes = 65536;
        Require(recorder.BeginRecording(recordingDesc, error), "Shared arena recording failed.");
        RHIEncoder* firstPass = &recorder.AcquireEncoder(0);
        firstPass->Draw(1, 1, 0, 0);
        RHIEncoder* secondPass = &recorder.AcquireEncoder(0);
        secondPass->Draw(2, 1, 0, 0);
        Require(firstPass == secondPass, "Pass encoder was allocated again instead of reset.");
        Require(recorder.GetRecordingStats().arenaBlocks == 1,
                "Small pass segments did not share their target arena block.");
        Require(recorder.FinishRecording(omitted, batchDesc, batch, error), "Shared arena did not seal.");
        // A second in-flight batch must not borrow the first batch's live block.
        Require(recorder.BeginRecording(recordingDesc, error), "Overlapping arena recording failed.");
        recorder.AcquireEncoder(0).Draw(3, 1, 0, 0);
        Require(batch.GetRecordingStats().commandCount == 2, "Overlapping recording changed the sealed batch.");
        recorder.CancelRecording();
        batch = RHISoftwareBatch{};
        Require(recorder.BeginRecording(recordingDesc, error), "Recycled arena recording failed.");
        recorder.AcquireEncoder(0).Draw(4, 1, 0, 0);
        Require(recorder.GetRecordingStats().arenaBlocks == 1, "New recording did not acquire its arena.");
        Require(recorder.GetRecordingStats().arenaCacheHits == 1, "Recycled command block was allocated again.");
        Require(recorder.GetRecordingStats().commandBytes == 40, "Compact draw command layout regressed.");
        recorder.CancelRecording();
        {
            RHISoftwareCommandList firstList(65536, 65536);
            RHISoftwareEncoder firstEncoder(firstList, validator);
            firstEncoder.Draw(17, 1, 0, 0);
            Require(firstList.Seal(error), "Arena move source did not seal.");
            RHISoftwareCommandList secondList(std::move(firstList));
            RHICommandTranslationStats movedStats;
            RHICommandFailure movedFailure;
            ProbeEncoder movedNative;
            Require(secondList.Replay(movedNative, movedStats, movedFailure) && movedStats.commandCount == 1,
                    "Arena move lost a command or returned its block early.");
            Require(!firstList.Replay(movedNative, movedStats, movedFailure), "Moved arena retained replay ownership.");
        }
        {
            Require(recorder.BeginRecording(recordingDesc, error), "Reusable storage warmup failed.");
            auto* retainedEncoder = &recorder.AcquireEncoder(0);
            retainedEncoder->Draw(1, 1, 0, 0);
            recorder.AcquireEncoder(0).Draw(2, 1, 0, 0);
            recorder.AcquireEncoder(0).Draw(3, 1, 0, 0);
            recorder.CancelRecording();
            Require(recorder.BeginRecording(recordingDesc, error), "Reusable storage restart failed.");
            auto* nextEncoder = &recorder.AcquireEncoder(0);
            Require(nextEncoder == retainedEncoder, "Completed frame allocated another software encoder.");
            nextEncoder->Draw(4, 1, 0, 0);
            Require(recorder.GetRecordingStats().commandCount == 1,
                    "Inactive retained segments leaked commands into the next frame.");
            recorder.CancelRecording();
            ProbeValidator rejecting;
            RHISoftwareRecorder differentValidator(rejecting);
            Require(differentValidator.BeginRecording(recordingDesc, error), "Validator rebinding begin failed.");
            Require(!differentValidator.AcquireEncoder(0).SetScissor(0, 0, 1, 1),
                    "Recycled encoder retained its previous recorder validator.");
            differentValidator.CancelRecording();
        }
        {
            ce::fixed_array<RHIEncoder*, 8> retainedTargets{};
            ce::fixed_array<uint32_t, 8> wideOrder{};
            recordingDesc.targetCount = static_cast<uint32_t>(retainedTargets.size());
            recordingDesc.maxCommandBytes = 65536;
            recordingDesc.maxPayloadBytes = 65536;
            Require(recorder.BeginRecording(recordingDesc, error), "Wide target warmup failed.");
            for (uint32_t target = 0; target < retainedTargets.size(); ++target)
            {
                wideOrder[target] = target;
                retainedTargets[target] = &recorder.AcquireEncoder(target);
                retainedTargets[target]->Draw(target + 1, 1, 0, 0);
            }
            recorder.CancelRecording();
            recordingDesc.targetCount = 1;
            Require(recorder.BeginRecording(recordingDesc, error), "Narrow target session failed.");
            recorder.AcquireEncoder(0).Draw(29, 1, 0, 0);
            Require(!recorder.HasRecorded(7), "Inactive retained target became visible.");
            bool inactiveRejected = false;
            try
            {
                recorder.AcquireEncoder(7);
            }
            catch (const std::out_of_range&)
            {
                inactiveRejected = true;
            }
            Require(inactiveRejected, "Inactive retained target accepted a writer.");
            const ce::fixed_array<uint32_t, 2> inactiveOrder{0, 7};
            Require(!recorder.FinishRecording(inactiveOrder, batchDesc, batch, error),
                    "Inactive retained target was accepted in submission order.");
            Require(recorder.FinishRecording(std::span(wideOrder).first(1), batchDesc, batch, error),
                    "Narrow batch did not seal after rejecting an inactive target.");
            Require(batch.GetTargetCount() == 1 && batch.GetRecordingStats().commandCount == 1,
                    "Retained targets contaminated narrow recording statistics.");
            batch = RHISoftwareBatch{};
            recordingDesc.targetCount = static_cast<uint32_t>(retainedTargets.size());
            Require(recorder.BeginRecording(recordingDesc, error), "Wide target reuse failed.");
            for (uint32_t target = 0; target < retainedTargets.size(); ++target)
            {
                auto& encoder = recorder.AcquireEncoder(target);
                Require(&encoder == retainedTargets[target], "Narrow batch discarded a retained target encoder.");
                encoder.Draw(target + 41, 1, 0, 0);
            }
            Require(recorder.FinishRecording(wideOrder, batchDesc, batch, error), "Reused wide targets did not seal.");
            Require(batch.GetTargetCount() == 8 && batch.GetRecordingStats().commandCount == 8,
                    "Wide target reuse replayed inactive or previous commands.");
            batch = RHISoftwareBatch{};
        }
        recordingDesc.maxCommandBytes = 1;
        recordingDesc.targetCount = 2;
        Require(!recorder.BeginRecording(recordingDesc, error), "Budget smaller than target count accepted.");
        Require(recorder.GetRecordingId() == 0, "Rejected inactive BeginRecording published an identity.");
        RHIDescriptorVersionPolicy versions;
        versions.Reset(1);
        const auto first = versions.BeginRecording(101).handle;
        auto lease = versions.PinRecording(first);
        Require(lease != nullptr, "Descriptor recording lease was not acquired.");
        Require(versions.OnSubmitted(101, RHICompletionPoint{7}), "Pinned descriptor submission failed.");
        Require(versions.Collect(RHICompletionPoint{7}) == 1, "Descriptor fence collection failed.");
        const auto second = versions.BeginRecording(102).handle;
        Require(second.slot != first.slot, "Descriptor lease allowed reuse after fence completion.");
        Require(versions.AbortRecording(102), "Descriptor abort failed.");
        lease.reset();
        const auto reused = versions.BeginRecording(103).handle;
        Require(reused.slot == first.slot && reused.generation != first.generation,
                "Released descriptor lease did not allow generation-safe reuse.");
        Require(!versions.PinRecording(first), "Stale descriptor generation was pinned.");
        auto cancelledLease = versions.PinRecording(reused);
        Require(versions.AbortRecording(103), "Pinned recording cancellation failed.");
        const auto afterCancel = versions.BeginRecording(104).handle;
        Require(afterCancel.slot != reused.slot, "Cancelled recording reused a leased descriptor page.");
        cancelledLease.reset();
        std::cout << "RHI_SOFTWARE_COMMANDS_OK encoderMethods=39 query=1 arenaGrowth=true ownedPayload=true "
                     "singleUse=true nativeFailure=true concurrentTargets=true descriptorLease=true "
                     "sharedArena=true mimallocArena=true crossThreadFree=true ownerThreadExit=true "
                     "arenaResetReuse=true arenaAsyncOwners=true arenaParallelDomains=true arenaBoundedCache=true "
                     "arenaEarlyExit=true alignedAllocation=true arenaNestedAllocator=true arenaConcurrentPool=true "
                     "resetEncoder=true boolBudgetFallback=true recordingIdentity=true nativeExecution=false\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
