// Compiles the production recorder, translator, scheduler and submission FIFO.
// Only the native device/encoder/fence boundary is a deterministic CPU fixture.
#include "../../Engine/RenderEngine/RHI/IRenderDeviceServices.h"
#include "../../Engine/RenderEngine/RHI/IRHIDeviceResources.h"
#include "../../Engine/RenderEngine/RHI/IRHICommandTranslator.h"
#include "../../Engine/RenderEngine/RHI/RHIParallelCommandPool.h"
#include "../../Engine/RenderEngine/RHI/RHISoftwareRecorder.h"
#include "../../Engine/RenderEngine/RHI/RHISubmissionThread.h"
#if __has_include("../../Engine/RenderEngine/RHI/RHIDisplaySubmission.h")
#include "../../Engine/RenderEngine/RHI/RHIDisplaySubmission.h"
#define CE_RHI_DISPLAY_SUBMISSION_PROOF 1
#else
#define CE_RHI_DISPLAY_SUBMISSION_PROOF 0
#endif
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <new>

namespace allocation_probe
{
    std::atomic<bool> enabled{};
    std::atomic<uint64_t> standard{};
    std::atomic<uint64_t> mimalloc{};
    void begin()
    {
        standard.store(0);
        mimalloc.store(0);
        enabled.store(true);
    }
    std::array<uint64_t, 2> end()
    {
        enabled.store(false);
        return {standard.load(), mimalloc.load()};
    }
} // namespace allocation_probe

void* operator new(std::size_t bytes)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed))
    {
        allocation_probe::standard.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void operator delete(void* memory) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed))
    {
        allocation_probe::standard.fetch_add(1, std::memory_order_relaxed);
    }
    void* memory{};
    if (posix_memalign(&memory, static_cast<size_t>(alignment), bytes ? bytes : 1) == 0)
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return ::operator new(bytes, alignment);
}
void operator delete(void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}

// GNU --wrap observes the actual vendored mimalloc entry points; allocation
// still executes the unmodified allocator. This is not a malloc replacement.
extern "C" void* __real_mi_malloc(size_t);
extern "C" void* __real_mi_malloc_aligned(size_t, size_t);
extern "C" void* __wrap_mi_malloc(size_t bytes)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed))
    {
        allocation_probe::mimalloc.fetch_add(1, std::memory_order_relaxed);
    }
    return __real_mi_malloc(bytes);
}
extern "C" void* __wrap_mi_malloc_aligned(size_t bytes, size_t alignment)
{
    if (allocation_probe::enabled.load(std::memory_order_relaxed))
    {
        allocation_probe::mimalloc.fetch_add(1, std::memory_order_relaxed);
    }
    return __real_mi_malloc_aligned(bytes, alignment);
}

namespace
{
    uint64_t g_checks{};
    uint64_t g_scenarios{};
    thread_local uint32_t g_worker = 8;
    void require(bool value, const char* message)
    {
        ++g_checks;
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
    void worker_start(unsigned int worker)
    {
        // The engine hook preserves physical worker IDs 1..N. Keep the
        // fixture's 0..7 worker slots separate from its owner slot 8.
        g_worker = worker - 1;
    }

    void verify_allocation_observer()
    {
        allocation_probe::begin();
        void* standard = ::operator new(17);
        void* aligned = ::operator new(128, std::align_val_t{64});
        void* first = mi_malloc(41);
        void* second = mi_malloc_aligned(128, 64);
        const auto counts = allocation_probe::end();
        require(standard && aligned && first && second && counts[0] == 2 && counts[1] == 2,
                "Allocation observation did not count the real standard/mimalloc API calls.");
        require(mi_check_owned(first) && mi_check_owned(second), "Mimalloc observation replaced the real allocator.");
        ::operator delete(standard);
        ::operator delete(aligned, std::align_val_t{64});
        mi_free(first);
        mi_free(second);
    }

    class ProbeValidator final : public IRHISoftwareCommandValidator
    {
      public:
        bool Validate(RHISoftwareCommandType, const void*, size_t, const RHISoftwareEncoderState&) const override
        {
            return true;
        }
    };

    class ProbeDeviceServices final : public IRenderDeviceServices
    {
      public:
        uint64_t m_uploadRecordingId{41};
        uint64_t m_descriptorVersion{17};
        bool ReserveUploadBatch(std::span<const RHIUploadRequest> requests, std::span<RHIBufferSlice> outSlices,
                                std::string& outError) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHIBufferSlice AllocateUpload(const RHIUploadRequest& request) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        uint64_t GetCurrentUploadRecordingId() const override { return m_uploadRecordingId; }

        void RegisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        void UnregisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHIBufferSlice UploadConstants(const void* data, size_t bytes) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc> descs) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHIEncoder& GetImmediateEncoder() override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descs) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        uint64_t GetDescriptorVersionToken() const override { return m_descriptorVersion; }

        RHIRenderTargetBinding CreateRenderTargets(std::span<const RHITextureHandle> colors,
                                                   const RHIDepthTargetDesc* depth = nullptr) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHIRenderTargetBinding CreateRenderTargets(std::span<const RHIColorTargetDesc> colors,
                                                   const RHIDepthTargetDesc* depth = nullptr) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        RHITextureInfo DescribeTexture(RHITextureHandle handle) const override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        void ReleaseTexture(RHITextureHandle handle) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        void ReleaseBuffer(RHIBufferHandle handle) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        void TransitionResources(std::span<const RHITransition> transitions) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        void TransitionBuffers(std::span<const RHIBufferTransition> transitions) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        bool CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outHandle, std::string& outError) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        bool CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outHandle, std::string& outError) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        bool CreateReadback(uint32_t width, uint32_t height, RHIFormat format, uint32_t sliceCount,
                            RHIReadback& outReadback, std::string& outError) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        bool MapReadback(const RHIReadback& readback, RHIReadbackImage& outImage, std::string& outError) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        void ReleaseReadback(RHIReadback& readback) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }

        bool CreateBufferReadback(uint64_t bytes, RHIReadback& outReadback, std::string& outError) override
        {
            throw std::logic_error("Unexpected device service call at CPU translation boundary.");
        }
    };

    class ProbeDevice final : public IRHIDeviceResources
    {
      public:
        std::atomic<uint64_t> m_completed{};
        bool IsInitialized() const override { return true; }
        void Shutdown() override {}
        bool Resize(uint32_t, uint32_t, std::string&) override { return false; }
        uint32_t GetWidth() const override { return 1; }
        uint32_t GetHeight() const override { return 1; }
        bool BeginFrame(std::string&) override { return false; }
        bool EndFrame(std::string&) override { return false; }
        void AbortFrame() override {}
        bool FlushCommandList(std::string&) override { return false; }
        void WaitForGpu() override { throw std::logic_error("Synchronous GPU wait reached CPU fixture."); }
        uint64_t GetLastSignaledFenceValue() const override { return m_completed.load(); }
        uint64_t GetCompletedFenceValue() const override { return m_completed.load(); }
        bool AttachSwapChain(void*, uint32_t, uint32_t, std::string&) override { return false; }
        bool ResizeSwapChain(uint32_t, uint32_t, std::string&) override { return false; }
        bool Present(std::string&) override { return false; }
        bool HasSwapChain() const override { return false; }
        uint32_t GetBackBufferIndex() const override { return 0; }
        uint32_t DrainDebugMessages(std::string&) override { return 0; }
        RHIVideoMemoryInfo QueryVideoMemory() const override { return {}; }
        RHIGpuObjectCensus CaptureLiveObjectCensus(bool) override { return {}; }
        void ReportLiveObjectsToDebugOutput() override {}
    };

    struct EncoderDigest
    {
        uint64_t hash{0xcbf29ce484222325ull};
        uint64_t commands{};
        uint64_t dispatches{};
        bool operator==(const EncoderDigest&) const = default;
    };

    class ProbeEncoder final : public RHIEncoder
    {
      public:
        EncoderDigest m_digest;
        uint64_t m_failCommand{UINT64_MAX};
        uint64_t m_throwCommand{UINT64_MAX};
        uint32_t m_spin{};
        uint32_t m_worker{UINT32_MAX};
        bool m_trackLifetime{};
        ce::weak_lifetime_token m_lifetime;
        std::atomic<uint32_t>* m_active{};

        void Feed(uint64_t kind, uint64_t first = 0, uint64_t second = 0, uint64_t third = 0)
        {
            if (m_active)
            {
                m_active->fetch_add(1);
            }
            struct Guard
            {
                std::atomic<uint32_t>* count;
                ~Guard()
                {
                    if (count)
                    {
                        count->fetch_sub(1);
                    }
                }
            } guard{m_active};
            if (m_trackLifetime && m_lifetime.expired())
            {
                throw std::logic_error("Frame owner died during native replay.");
            }
            if (m_digest.commands == m_throwCommand)
            {
                throw std::runtime_error("Injected native replay exception.");
            }
            if (m_worker == UINT32_MAX)
            {
                m_worker = g_worker;
            }
            else if (m_worker != g_worker)
            {
                throw std::logic_error("Segments of one target changed physical worker.");
            }
            m_digest.hash = (m_digest.hash ^ kind) * 0x100000001b3ull;
            m_digest.hash = (m_digest.hash ^ first) * 0x100000001b3ull;
            m_digest.hash = (m_digest.hash ^ second) * 0x100000001b3ull;
            m_digest.hash = (m_digest.hash ^ third) * 0x100000001b3ull;
            ++m_digest.commands;
            // Explicit deterministic fake driver work, never a native GPU cost.
            for (uint32_t index = 0; index < m_spin; ++index)
            {
                m_digest.hash = (m_digest.hash ^ index) * 0x100000001b3ull;
            }
        }
        void SetViewportAndScissor(uint32_t x, uint32_t y) override { Feed(1, x, y); }
        void SetPipeline(RHIBindPoint point, RHIPipelineHandle pipeline) override
        {
            Feed(2, static_cast<uint64_t>(point), pipeline.id);
        }
        void SetPrimitiveTopology(RHIPrimitiveTopology topology) override { Feed(3, static_cast<uint64_t>(topology)); }
        void SetBindings(RHIBindPoint, uint32_t, const RHIBindingTable&) override { Feed(4); }
        void SetSamplers(RHIBindPoint, uint32_t, const RHISamplerTable&) override { Feed(5); }
        void SetConstantBuffer(RHIBindPoint point, uint32_t slot, const RHIBufferSlice& slice) override
        {
            if (slice.cpuAddress)
            {
                throw std::logic_error("CPU upload pointer escaped into native replay.");
            }
            Feed(6, static_cast<uint64_t>(point), slot, slice.buffer.id ^ slice.offset ^ slice.size);
        }
        void SetRootBuffer(RHIBindPoint, uint32_t, const RHIBufferSlice&) override { Feed(7); }
        void SetVertexBuffer(const RHIBufferSlice&, uint32_t) override { Feed(8); }
        void SetIndexBuffer(const RHIBufferSlice&, RHIFormat) override { Feed(9); }
        void Draw(uint32_t a, uint32_t b, uint32_t c, uint32_t d) override { Feed(10, a, b, (uint64_t(c) << 32) | d); }
        void DrawIndexed(uint32_t a, uint32_t b, uint32_t c, int32_t d, uint32_t e) override
        {
            Feed(11, a, b, (uint64_t(c) << 32) ^ uint32_t(d) ^ e);
        }
        bool DispatchIndirect(RHIBufferHandle buffer, uint64_t offset) override
        {
            if (m_digest.commands == m_failCommand)
            {
                return false;
            }
            Feed(12, buffer.id, offset);
            ++m_digest.dispatches;
            return true;
        }
        bool DispatchMeshIndirectBatch(RHIBufferHandle buffer, uint64_t offset, uint32_t count,
                                       std::span<const uint32_t> roots) override
        {
            if (roots.size() != 3)
            {
                throw std::logic_error("Owned root payload changed its extent.");
            }
            Feed(13, buffer.id ^ offset, count, (uint64_t(roots[0]) << 32) ^ roots[1] ^ roots[2]);
            return true;
        }
        void Dispatch(uint32_t x, uint32_t y, uint32_t z) override
        {
            Feed(14, x, y, z);
            ++m_digest.dispatches;
        }
        uint64_t GetComputeDispatchCount() const override { return m_digest.dispatches; }
        void BindRenderTargets(const RHIRenderTargetBinding&) override { Feed(15); }
        void ClearRenderTargets(const RHIRenderTargetBinding&, const float* rgba) override
        {
            Feed(16, std::bit_cast<uint32_t>(rgba[0]), std::bit_cast<uint32_t>(rgba[1]),
                 (uint64_t(std::bit_cast<uint32_t>(rgba[2])) << 32) | std::bit_cast<uint32_t>(rgba[3]));
        }
        void ClearDepthTarget(const RHIRenderTargetBinding&, float value) override
        {
            Feed(17, std::bit_cast<uint32_t>(value));
        }
        void ResourceBarriers(const RHIBarrierBatch& barriers) override
        {
            if (barriers.textureTransitions.size() != 1 || barriers.bufferTransitions.size() != 1 ||
                barriers.uavTextures.size() != 1 || barriers.uavBuffers.size() != 1 ||
                barriers.aliasTextures.size() != 1 || barriers.aliasBuffers.size() != 1)
            {
                throw std::logic_error("Owned barrier spans changed.");
            }
            Feed(18, barriers.textureTransitions[0].texture.id ^ barriers.bufferTransitions[0].buffer.id,
                 barriers.uavTextures[0].id ^ barriers.uavBuffers[0].id,
                 barriers.aliasTextures[0].id ^ barriers.aliasBuffers[0].id);
        }
        void UavBarrier(std::span<const RHITextureHandle>) override { Feed(19); }
        void UavBarrierBuffers(std::span<const RHIBufferHandle>) override { Feed(20); }
        void CopyResource(RHITextureHandle a, RHITextureHandle b) override { Feed(21, a.id, b.id); }
        void ClearUnorderedAccess(const RHIBindingDesc&, const float*) override { Feed(22); }
        void CopyToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t) override { Feed(23); }
        void CopyVolumeToReadback(const RHIReadback&, RHITextureHandle, uint32_t) override { Feed(24); }
        void CopyPartialToReadback(const RHIReadback&, RHITextureHandle, uint32_t, uint32_t) override { Feed(25); }
        void CopyBufferToReadback(const RHIReadback&, RHIBufferHandle, uint64_t, uint64_t) override { Feed(26); }
        void CopyTexture(RHITextureHandle, RHITextureHandle, uint32_t, uint32_t) override { Feed(27); }
        void ClearRenderTargetRect(const RHIRenderTargetBinding&, const float*, const RHIRect&) override { Feed(28); }
    };

    class ProbePool final : public IRHIParallelCommandPool
    {
      public:
        explicit ProbePool(job_scheduler& scheduler) : IRHIParallelCommandPool(scheduler) { Reset(); }
        std::array<ProbeEncoder, 8> m_encoders;
        std::array<bool, 8> m_opened{};
        std::array<bool, 8> m_recorded{};
        std::array<uint32_t, 8> m_submittedOrder{};
        uint32_t m_submittedCount{};
        uint32_t m_capacity{8};
        uint32_t m_openFailure{UINT32_MAX};
        uint32_t m_acquireFailure{UINT32_MAX};
        uint32_t m_dropCount{};
        uint32_t m_openCalls{};
        uint32_t m_closeCalls{};
        uint64_t m_prepared{};
        uint64_t m_rejected{};
        uint64_t m_accepted{};
        uint64_t m_submitCalls{};
        uint64_t m_completion{};
        std::atomic<uint32_t> m_active{};
        bool m_initialized{true};
        bool m_parallel{true};
        bool m_frozen{true};
        bool m_closeFailure{};
        bool m_submitFailure{};
        bool m_executeBeforeFailure{};
        std::thread::id m_coordinator;
        void Reset()
        {
            if (m_active.load())
            {
                throw std::logic_error("Reset raced accepted native replay.");
            }
            for (auto& encoder : m_encoders)
            {
                encoder = ProbeEncoder{};
                encoder.m_active = &m_active;
            }
            m_opened.fill(false);
            m_recorded.fill(false);
            m_submittedOrder.fill(UINT32_MAX);
            m_submittedCount = m_openCalls = m_closeCalls = 0;
            m_openFailure = m_acquireFailure = UINT32_MAX;
            m_dropCount = 0;
            m_initialized = m_parallel = m_frozen = true;
            m_closeFailure = m_submitFailure = false;
            m_executeBeforeFailure = false;
            m_capacity = 8;
            m_coordinator = {};
        }
        bool IsInitialized() const override { return m_initialized; }
        uint32_t GetWorkerCount() const override { return m_capacity; }
        bool GetTranslationRecording(uint64_t& upload, uint64_t& descriptor) const override
        {
            upload = 41;
            descriptor = 17;
            return m_frozen;
        }
        void BeginFrame(uint32_t) override {}
        bool Prepare(std::string&) override { return true; }
        bool SupportsParallelTranslation() const override { return m_parallel; }
        bool OpenWorker(uint32_t worker, std::string& error) override
        {
            if (!m_openCalls)
            {
                m_coordinator = std::this_thread::get_id();
            }
            if (m_coordinator != std::this_thread::get_id() || worker >= m_capacity || m_opened[worker])
            {
                throw std::logic_error("Native target open ownership changed.");
            }
            ++m_openCalls;
            if (worker == m_openFailure)
            {
                error = "Injected target-open failure.";
                return false;
            }
            m_opened[worker] = true;
            return true;
        }
        RHIEncoder& AcquireEncoder(uint32_t worker) override
        {
            if (worker >= m_capacity || !m_opened[worker])
            {
                throw std::logic_error("Native replay acquired an unopened target.");
            }
            if (worker == m_acquireFailure)
            {
                throw std::runtime_error("Injected encoder acquisition failure.");
            }
            m_recorded[worker] = true;
            return m_encoders[worker];
        }
        bool CloseAll(std::string& error) override
        {
            if (m_active.load() || m_coordinator != std::this_thread::get_id())
            {
                throw std::logic_error("Native close ran before all jobs joined or on another owner.");
            }
            ++m_closeCalls;
            if (m_closeFailure)
            {
                error = "Injected native close failure.";
                return false;
            }
            return true;
        }
        uint32_t DrainEncoderDrops(std::string& error) override
        {
            if (m_dropCount)
            {
                error = "Injected encoder drop.";
            }
            return m_dropCount;
        }
        bool HasRecorded(uint32_t worker) const override { return worker < 8 && m_recorded[worker]; }

      protected:
        uint32_t GetCurrentFrameSlot() const override { return 3; }
        uint64_t GetCurrentRecordingId() const override { return 41; }
        void RejectPreparedCommands(uint64_t, RHICompletionPoint) override { ++m_rejected; }
        void AcceptPreparedCommands(RHICompletionPoint, const RHISubmissionTicket&) override { ++m_accepted; }
        bool PrepareRecordedCommands(uint32_t slot, RHICompletionPoint& completion, std::string&) override
        {
            if (slot != 3)
            {
                throw std::logic_error("Captured frame slot changed before admission.");
            }
            completion.value = ++m_completion;
            ++m_prepared;
            return true;
        }
        bool SubmitRecordedCommands(uint32_t slot, std::span<const uint32_t> order, RHICompletionPoint completion,
                                    std::string& error) override
        {
            if (slot != 3 || !completion.IsValid() || order.size() > 8)
            {
                throw std::logic_error("Native submit lost recorded identity.");
            }
            m_submittedCount = static_cast<uint32_t>(order.size());
            std::copy(order.begin(), order.end(), m_submittedOrder.begin());
            ++m_submitCalls;
#if CE_RHI_STRUCTURED_FAILURE
            if (m_executeBeforeFailure)
            {
                RHICommandFailureScope::SetExecutionUncertain(true);
            }
#endif
            if (m_submitFailure)
            {
                error = "Injected native submission failure.";
                return false;
            }
#if CE_RHI_STRUCTURED_FAILURE
            RHICommandFailureScope::SetExecutionUncertain(false);
#endif
            return true;
        }
    };

    struct BatchSpec
    {
        uint32_t targets{8};
        uint32_t segments{3};
        uint32_t draws{7};
        uint32_t spin{};
        uint32_t skew{};
        bool reverse{};
        uint64_t generation{1};
        uint64_t frame{71};
        uint64_t upload{41};
        uint64_t descriptor{17};
        RHIQueueIdentity queue{1, 1, RHIQueueKind::Graphics};
    };

    void emit_segment(RHIEncoder& encoder, uint32_t target, uint32_t segment, uint32_t draws, bool software)
    {
        RHIBufferSlice constants{{100 + target}, 32 + segment, 256, nullptr};
        if (software)
        {
            constants.cpuAddress = &constants;
        }
        encoder.SetConstantBuffer(RHIBindPoint::Compute, segment, constants);
        std::array<uint32_t, 3> roots{target + 7, segment + 8, draws + 9};
        if (!encoder.DispatchMeshIndirectBatch({1}, 16, 3, roots))
        {
            throw std::logic_error("Validated root payload was rejected.");
        }
        roots.fill(0);
        std::array<RHITransition, 1> textures{
            {{{11 + target}, RHIResourceState::Common, RHIResourceState::CopySource}}};
        std::array<RHIBufferTransition, 1> buffers{
            {{{12 + segment}, RHIResourceState::Common, RHIResourceState::CopySource}}};
        std::array<RHITextureHandle, 1> uavTextures{{{13}}}, aliasTextures{{{15}}};
        std::array<RHIBufferHandle, 1> uavBuffers{{{14}}}, aliasBuffers{{{16}}};
        encoder.ResourceBarriers({textures, buffers, uavTextures, uavBuffers, aliasTextures, aliasBuffers});
        textures[0].texture.id = 0;
        buffers[0].buffer.id = 0;
        uavTextures[0].id = aliasTextures[0].id = 0;
        uavBuffers[0].id = aliasBuffers[0].id = 0;
        std::array<float, 4> color{0.25f, 0.5f, static_cast<float>(target), 1.f};
        encoder.ClearRenderTargets({}, color.data());
        color.fill(0.f);
        for (uint32_t index = 0; index < draws; ++index)
        {
            encoder.Draw(index + segment * 131, target + 1, segment, index ^ target);
        }
        encoder.Dispatch(target + 1, segment + 1, 3);
        if (!encoder.DispatchIndirect({55}, 16))
        {
            throw std::logic_error("Validated indirect command was rejected while recording.");
        }
    }

    RHISoftwareBatch record_batch(RHISoftwareRecorder& recorder, const BatchSpec& spec, ce::lifetime_token owner = {})
    {
        if (!owner)
        {
            static const auto defaultOwner = own::make_shared<uint32_t>(1);
            owner = defaultOwner;
        }
        std::string error;
        RHISoftwareRecordingDesc recording{spec.queue, spec.targets, 64ull << 20, 32ull << 20};
        if (!recorder.BeginRecording(recording, error))
        {
            throw std::runtime_error(error);
        }
        for (uint32_t target = 0; target < spec.targets; ++target)
        {
            const uint32_t draws = spec.draws + ((target == spec.targets - 1) ? spec.skew : 0);
            for (uint32_t segment = 0; segment < spec.segments; ++segment)
            {
                emit_segment(recorder.AcquireEncoder(target), target, segment, draws, true);
            }
        }
        std::array<uint32_t, 8> order{};
        for (uint32_t index = 0; index < spec.targets; ++index)
        {
            order[index] = spec.reverse ? spec.targets - 1 - index : index;
        }
        RHISoftwareBatchDesc desc;
        desc.frameId = spec.frame;
        desc.backendGeneration = spec.generation;
        desc.displayToken = 91;
        desc.uploadRecordingId = spec.upload;
        desc.descriptorVersion = spec.descriptor;
        desc.queue = spec.queue;
        desc.lifetimeToken = std::move(owner);
        RHISoftwareBatch batch;
        if (!recorder.FinishRecording(std::span(order.data(), spec.targets), desc, batch, error))
        {
            throw std::runtime_error(error);
        }
        return batch;
    }

    std::array<EncoderDigest, 8> expected_digests(const BatchSpec& spec)
    {
        std::array<EncoderDigest, 8> result{};
        for (uint32_t target = 0; target < spec.targets; ++target)
        {
            ProbeEncoder direct;
            direct.m_spin = spec.spin;
            const uint32_t draws = spec.draws + ((target == spec.targets - 1) ? spec.skew : 0);
            for (uint32_t segment = 0; segment < spec.segments; ++segment)
            {
                emit_segment(direct, target, segment, draws, false);
            }
            result[target] = direct.m_digest;
        }
        return result;
    }

    void verify_digests(const ProbePool& pool, const BatchSpec& spec, const std::array<EncoderDigest, 8>& expected)
    {
        for (uint32_t target = 0; target < spec.targets; ++target)
        {
            require(pool.m_encoders[target].m_digest == expected[target],
                    "Software replay changed command bytes, order or count.");
        }
    }

    void verify_submission(RHISubmissionThread& submission, ProbeDevice& device, const void* owner, ProbePool& pool,
                           const BatchSpec& spec, RHITranslatedBatch&& translated)
    {
        std::string error;
        RHISubmissionTicket ticket;
        require(submission.EnqueueTranslatedBatch(owner, device, std::move(translated), ticket, error), error.c_str());
        require(!translated.IsValid() && ticket.IsValid(), "Submission duplicated a translated batch.");
        require(submission.Wait(ticket, error), error.c_str());
        const auto* native = ticket.GetRecordedBatch();
        require(native && native->IsSubmitted() && native->GetFrameId() == spec.frame &&
                    native->GetDisplayToken() == 91 && native->GetFrameSlot() == 3,
                "Actual submission FIFO lost native batch metadata.");
        require(pool.m_submittedCount == spec.targets, "Native target count changed at submission.");
        for (uint32_t index = 0; index < spec.targets; ++index)
        {
            require(pool.m_submittedOrder[index] == (spec.reverse ? spec.targets - 1 - index : index),
                    "Cost-order replay changed original graph submission order.");
        }
        device.m_completed.store(native->GetCompletionPoint().value);
        require(submission.Drain(owner, error), error.c_str());
        submission.CollectCompletedLifetimes(owner);
    }

    void correctness_matrix(uint32_t physicalWorkers)
    {
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(physicalWorkers);
        ProbePool pool(scheduler);
        ProbeDeviceServices services;
        ProbeValidator validator;
        RHISoftwareRecorder recorder(validator);
        RHICommandTranslator translator(pool, services, {1, 1, RHIQueueKind::Graphics});
        RHISubmissionThread submission;
        ProbeDevice device;
        std::string error;
        require(submission.AcquireClient(&device, error), error.c_str());
        for (const uint32_t targets : {1u, 2u, 4u, 8u})
        {
            for (const uint32_t segments : {1u, 3u, 17u})
            {
                for (const uint32_t jobs : {1u, 2u, 4u, 8u})
                {
                    for (const uint64_t minimum : {0ull, 256ull, 1000000ull})
                    {
                        for (uint32_t pattern = 0; pattern < 3; ++pattern)
                        {
                            ++g_scenarios;
                            BatchSpec spec;
                            spec.targets = targets;
                            spec.segments = segments;
                            spec.skew = pattern == 2 ? 73 : 0;
                            spec.reverse = pattern != 0;
                            const auto expected = expected_digests(spec);
                            auto owner = own::make_shared<uint64_t>(71);
                            ce::weak_lifetime_token lifetime(owner);
                            pool.Reset();
                            for (auto& encoder : pool.m_encoders)
                            {
                                encoder.m_trackLifetime = true;
                                encoder.m_lifetime = owner;
                            }
                            auto batch = record_batch(recorder, spec, owner);
                            owner.reset();
                            const auto recorded = batch.GetRecordingStats();
                            RHICommandFailure failure;
                            RHITranslatedBatch translated;
                            const RHICommandTranslationOptions options{jobs == 1 ? RHICommandTranslationMode::Serial
                                                                                 : RHICommandTranslationMode::Parallel,
                                                                       jobs, minimum};
                            require(translator.Translate(std::move(batch), options, translated, failure),
                                    failure.message.c_str());
                            require(!batch.IsValid() && translated.IsValid() &&
                                        translated.GetKind() == RHITranslatedBatchKind::ParallelPool,
                                    "Translation violated one-shot state.");
                            require(!lifetime.expired(), "Successful translation released its resource lease.");
                            const uint32_t expectedJobs =
                                jobs == 1
                                    ? 1
                                    : static_cast<uint32_t>((std::max)(
                                          uint64_t{1},
                                          (std::min)({uint64_t(jobs), uint64_t(targets),
                                                      minimum ? recorded.commandCount / minimum : uint64_t(targets)})));
                            require(translated.GetTranslationStats().translationJobCount == expectedJobs,
                                    "Translation admission ignored target or command grain.");
                            require(translated.GetTranslationStats().commandCount == recorded.commandCount &&
                                        translated.GetTranslationStats().nativeComputeDispatches ==
                                            targets * segments * 2,
                                    "Translation aggregate command/dispatch count changed.");
                            verify_digests(pool, spec, expected);
                            verify_submission(submission, device, &device, pool, spec, std::move(translated));
                            require(lifetime.expired(),
                                    "Completed CPU submission and fence retirement retained frame resources.");
                        }
                    }
                }
            }
        }
        submission.ReleaseClient(&device);
        scheduler.shutdown();
    }

    void failure_contracts()
    {
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(4);
        ProbePool pool(scheduler);
        ProbeDeviceServices services;
        ProbeValidator validator;
        RHISoftwareRecorder recorder(validator);
        RHICommandTranslator translator(pool, services, {1, 1, RHIQueueKind::Graphics});
        BatchSpec spec;
        spec.reverse = true;
        const RHICommandTranslationOptions parallel{RHICommandTranslationMode::Parallel, 4, 0};
        for (uint32_t fault = 0; fault < 16; ++fault)
        {
            ++g_scenarios;
            pool.Reset();
            services.m_uploadRecordingId = 41;
            services.m_descriptorVersion = 17;
            auto owner = own::make_shared<uint32_t>(7);
            ce::weak_lifetime_token lifetime(owner);
            for (auto& encoder : pool.m_encoders)
            {
                encoder.m_trackLifetime = true;
                encoder.m_lifetime = owner;
            }
            auto batch = record_batch(recorder, spec, owner);
            owner.reset();
            RHICommandTranslationOptions options = parallel;
            switch (fault)
            {
            case 0:
                pool.m_initialized = false;
                break;
            case 1:
                pool.m_parallel = false;
                break;
            case 2:
                options.maxTranslationJobs = 0;
                break;
            case 3:
                options.maxTranslationJobs = 9;
                break;
            case 4:
                options.mode = RHICommandTranslationMode::Serial;
                break;
            case 5:
                options.mode = static_cast<RHICommandTranslationMode>(99);
                break;
            case 6:
                pool.m_capacity = 7;
                break;
            case 7:
                pool.m_frozen = false;
                services.m_uploadRecordingId = 99;
                break;
            case 8:
                pool.m_frozen = false;
                services.m_descriptorVersion = 99;
                break;
            case 9:
                pool.m_openFailure = 4;
                break;
            case 10:
                pool.m_acquireFailure = 5;
                break;
            case 11:
                pool.m_closeFailure = true;
                break;
            case 12:
                pool.m_dropCount = 1;
                break;
            case 13:
                pool.m_encoders[1].m_failCommand = 12;
                pool.m_encoders[6].m_failCommand = 25;
                break;
            case 14:
                pool.m_encoders[2].m_throwCommand = 27;
                pool.m_encoders[7].m_throwCommand = 5;
                break;
            case 15:
                pool.m_acquireFailure = 0;
                break;
            }
            RHITranslatedBatch translated;
            RHICommandFailure failure;
            require(!translator.Translate(std::move(batch), options, translated, failure),
                    "Injected translation fault succeeded.");
            require(!batch.IsValid() && !translated.IsValid() && !failure.message.empty(),
                    "Failed translation retained reusable input or empty diagnostics.");
            require(lifetime.expired() && pool.m_active.load() == 0,
                    "Failure returned before resource callbacks drained.");
            if (fault == 13)
            {
                require(failure.targetIndex == 6 && failure.commandIndex == 25 &&
                            failure.commandType == RHISoftwareCommandType::DispatchIndirect,
                        "First failure did not follow original target order and cumulative segment command index.");
            }
            if (fault == 14)
            {
                require(failure.targetIndex == 7 && failure.commandIndex == 5, "Native exception provenance changed.");
            }
            if (fault >= 10 && fault != 11)
            {
                require(pool.m_closeCalls >= 1, "Started native recording was not closed after failure.");
                for (uint32_t target = 0; target < 8; ++target)
                {
                    if ((fault == 10 && target == 5) || (fault == 13 && (target == 1 || target == 6)) ||
                        (fault == 14 && (target == 2 || target == 7)) || (fault == 15 && target == 0))
                    {
                        continue;
                    }
                    require(pool.m_encoders[target].m_digest.commands == 39,
                            "Independent target did not drain after a peer failed.");
                }
            }
        }
        // A frozen pool carries its captured upload/descriptor identity even
        // after the producer has opened a new mutable recording.
        pool.Reset();
        services.m_uploadRecordingId = 900;
        services.m_descriptorVersion = 901;
        auto batch = record_batch(recorder, spec);
        RHITranslatedBatch translated;
        RHICommandFailure failure;
        require(translator.Translate(std::move(batch), parallel, translated, failure),
                "Frozen pool used advanced mutable producer identity.");
        translated = {};
        // Native recording remains owner-only: an engine worker cannot nest a
        // synchronous translation join into its own scheduler.
        pool.Reset();
        batch = record_batch(recorder, spec);
        bool rejected{};
        auto nested = scheduler.submit(
            [&] { rejected = !translator.Translate(std::move(batch), parallel, translated, failure); });
        nested.wait();
        require(rejected && !translated.IsValid() && !batch.IsValid(),
                "Nested worker translation bypassed join ownership.");
        // A nonempty output owns its previous result. A rejected second input
        // is still consumed, and cannot replace the already-translated batch.
        pool.Reset();
        batch = record_batch(recorder, spec);
        require(translator.Translate(std::move(batch), parallel, translated, failure), failure.message.c_str());
        const auto retainedCount = translated.GetTranslationStats().commandCount;
        auto rejectedOwner = own::make_shared<uint32_t>(1);
        ce::weak_lifetime_token rejectedLifetime(rejectedOwner);
        batch = record_batch(recorder, spec, rejectedOwner);
        rejectedOwner.reset();
        require(!translator.Translate(std::move(batch), parallel, translated, failure) && translated.IsValid() &&
                    translated.GetTranslationStats().commandCount == retainedCount && !batch.IsValid() &&
                    rejectedLifetime.expired(),
                "Nonempty output admission replaced a result or retained rejected input.");
        translated = {};
        for (uint32_t invalidIdentity = 0; invalidIdentity < 4; ++invalidIdentity)
        {
            ++g_scenarios;
            pool.Reset();
            auto invalid = spec;
            if (invalidIdentity == 0)
            {
                invalid.queue.deviceGeneration = 2;
            }
            else if (invalidIdentity == 1)
            {
                invalid.queue.kind = RHIQueueKind::Compute;
            }
            else if (invalidIdentity == 2)
            {
                invalid.upload = 0;
            }
            else
            {
                invalid.descriptor = 0;
            }
            batch = record_batch(recorder, invalid);
            require(!translator.Translate(std::move(batch), parallel, translated, failure) && !batch.IsValid() &&
                        !translated.IsValid() && pool.m_openCalls == 0,
                    "Wrong queue or unavailable resource identity reached native recording.");
        }
        // The same recorder's disjoint target slots are written by real engine
        // jobs, then replayed and compared with the direct native-boundary oracle.
        pool.Reset();
        RHISoftwareRecordingDesc recording{spec.queue, 8, 64ull << 20, 32ull << 20};
        std::string error;
        require(recorder.BeginRecording(recording, error), error.c_str());
        auto recordingJobs =
            scheduler.submit_indexed(8,
                                     [&](size_t target)
                                     {
                                         for (uint32_t segment = 0; segment < spec.segments; ++segment)
                                         {
                                             emit_segment(recorder.AcquireEncoder(static_cast<uint32_t>(target)),
                                                          static_cast<uint32_t>(target), segment, spec.draws, true);
                                         }
                                     });
        recordingJobs.wait();
        RHISoftwareBatchDesc desc;
        desc.frameId = spec.frame;
        desc.backendGeneration = 1;
        desc.queue = spec.queue;
        desc.uploadRecordingId = 41;
        desc.descriptorVersion = 17;
        desc.lifetimeToken = own::make_shared<uint32_t>(1);
        const std::array<uint32_t, 8> reverse{7, 6, 5, 4, 3, 2, 1, 0};
        require(recorder.FinishRecording(reverse, desc, batch, error), error.c_str());
        require(translator.Translate(std::move(batch), parallel, translated, failure), failure.message.c_str());
        verify_digests(pool, spec, expected_digests(spec));
        scheduler.shutdown();
    }

#if CE_RHI_BYTE_ADMISSION
#if CE_RHI_STRUCTURED_FAILURE
    bool equal_failure(const RHICommandFailure& first, const RHICommandFailure& next)
    {
        return first.stage == next.stage && first.recordingId == next.recordingId &&
               first.targetIndex == next.targetIndex && first.commandIndex == next.commandIndex &&
               first.commandType == next.commandType && first.backendGeneration == next.backendGeneration &&
               first.executionUncertain == next.executionUncertain && first.message == next.message;
    }

    RHICommandFailure stale_failure()
    {
        RHICommandFailure failure;
        failure.stage = RHICommandFailureStage::Retirement;
        failure.recordingId = 19;
        failure.targetIndex = 3;
        failure.commandIndex = 7;
        failure.commandType = RHISoftwareCommandType::Dispatch;
        failure.backendGeneration = 4;
        failure.executionUncertain = true;
        failure.message = "stale output must be reset";
        return failure;
    }
#endif
    class ReplayGate
    {
      public:
        void Wait()
        {
            std::unique_lock lock(m_mutex);
            m_entered = true;
            m_wake.notify_all();
            m_wake.wait(lock, [&] { return m_open; });
        }
        bool WaitEntered()
        {
            std::unique_lock lock(m_mutex);
            return m_wake.wait_for(lock, std::chrono::seconds(10), [&] { return m_entered; });
        }
        void Open()
        {
            std::lock_guard lock(m_mutex);
            m_open = true;
            m_wake.notify_all();
        }

      private:
        std::mutex m_mutex;
        std::condition_variable m_wake;
        bool m_entered{};
        bool m_open{};
    };

    class GatedTranslator final : public IRHICommandTranslator
    {
      public:
        GatedTranslator(ProbePool& pool, ProbeDeviceServices& services, ReplayGate& gate)
            : m_translator(pool, services, {1, 1, RHIQueueKind::Graphics}), m_gate(gate)
        {
        }
        bool Translate(RHISoftwareBatch&& batch, const RHICommandTranslationOptions& options,
                       RHITranslatedBatch& translated, RHICommandFailure& failure) override
        {
            m_gate.Wait();
            return m_translator.Translate(std::move(batch), options, translated, failure);
        }

      private:
        RHICommandTranslator m_translator;
        ReplayGate& m_gate;
    };

    class ThrowBeforeConsumeTranslator final : public IRHICommandTranslator
    {
      public:
        bool Translate(RHISoftwareBatch&&, const RHICommandTranslationOptions&, RHITranslatedBatch&,
                       RHICommandFailure&) override
        {
            throw std::runtime_error("Injected pre-consumption translation exception.");
        }
    };

    class GatedDestruction final
    {
      public:
        GatedDestruction(ReplayGate& gate, std::atomic<bool>& completed) : m_gate(gate), m_completed(completed) {}
        ~GatedDestruction()
        {
            m_gate.Wait();
            m_completed.store(true);
        }

      private:
        ReplayGate& m_gate;
        std::atomic<bool>& m_completed;
    };

    template<class Predicate>
    bool wait_observed(Predicate&& predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    void byte_admission_contracts()
    {
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(4);
        ProbeValidator validator;
        ProbeDeviceServices services;
        RHISoftwareRecorder recorder(validator);
        BatchSpec spec;
        spec.targets = 1;
        spec.segments = 1;
        auto reference = record_batch(recorder, spec);
        const auto bytes = reference.GetRecordingStats().allocatedBytes;
        reference = {};
        require(bytes > 1, "Fixture did not allocate a measurable software arena.");
        // Oversize must reject before reserving completion or consuming input.
        {
            ++g_scenarios;
            RHISubmissionThread submission(bytes - 1);
            ProbeDevice device;
            auto pool = own::make_shared<ProbePool>(scheduler);
            auto translator = own::make_shared<RHICommandTranslator>(*pool, services, spec.queue);
            std::string error;
            require(submission.AcquireClient(&device, error), error.c_str());
            auto batch = record_batch(recorder, spec);
            RHISubmissionTicket ticket;
            const bool admitted =
                submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), ticket, error);
            require(!admitted && batch.IsValid() && !ticket.IsValid() && pool->m_prepared == 0,
                    "Oversized arena consumed input or advanced native completion before rejection.");
            require(submission.GetStats().pendingSoftwareBytes == 0 && submission.GetStats().peakSoftwareBytes == 0,
                    "Rejected oversize consumed FIFO byte credits.");
#if CE_RHI_STRUCTURED_FAILURE
            auto failure = stale_failure();
            require(!ticket.GetCommandFailure(failure) && equal_failure(failure, RHICommandFailure{}),
                    "Invalid ticket exposed a failure or retained stale output.");
#endif
            submission.ReleaseClient(&device);
        }
        // A running translator holds its credits. A second producer blocks on
        // byte capacity despite free FIFO entry slots, then resumes after replay.
        {
            ++g_scenarios;
            RHISubmissionThread submission(bytes);
            ProbeDevice device;
            ReplayGate gate;
            auto firstPool = own::make_shared<ProbePool>(scheduler);
            auto nextPool = own::make_shared<ProbePool>(scheduler);
            nextPool->m_completion = 1;
            auto firstTranslator = own::make_shared<GatedTranslator>(*firstPool, services, gate);
            auto nextTranslator = own::make_shared<RHICommandTranslator>(*nextPool, services, spec.queue);
            std::string error;
            require(submission.AcquireClient(&device, error), error.c_str());
            auto firstOwner = own::make_shared<uint32_t>(1);
            auto nextOwner = own::make_shared<uint32_t>(2);
            ce::weak_lifetime_token firstLifetime(firstOwner), nextLifetime(nextOwner);
            auto first = record_batch(recorder, spec, firstOwner);
            auto next = record_batch(recorder, spec, nextOwner);
            firstOwner.reset();
            nextOwner.reset();
            RHISubmissionTicket firstTicket, nextTicket;
            require(submission.EnqueueSoftwareBatch(&device, device, firstPool, firstTranslator, std::move(first),
                                                    firstTicket, error),
                    error.c_str());
            const bool entered = gate.WaitEntered();
#if CE_RHI_STRUCTURED_FAILURE
            auto pendingFailure = stale_failure();
            const bool pendingSafe =
                !firstTicket.GetCommandFailure(pendingFailure) && equal_failure(pendingFailure, RHICommandFailure{});
#endif
            bool nextAdmitted{};
            std::string nextError;
            std::atomic<bool> producerReturned{};
            std::thread producer(
                [&]
                {
                    nextAdmitted = submission.EnqueueSoftwareBatch(&device, device, nextPool, nextTranslator,
                                                                   std::move(next), nextTicket, nextError);
                    producerReturned.store(true);
                });
            const bool waited = wait_observed([&] { return submission.GetStats().softwareSaturationWaits == 1; });
            const auto blocked = submission.GetStats();
            const bool premature = producerReturned.load();
            gate.Open();
            producer.join();
            require(entered && waited && !premature && blocked.pendingSoftwareBytes == bytes &&
                        blocked.peakSoftwareBytes == bytes && blocked.pendingTasks == 1,
                    "Running software work did not hold byte credits independently of FIFO slots.");
            require(nextAdmitted && nextTicket.IsValid(), nextError.c_str());
            require(submission.Wait(firstTicket, error) && submission.Wait(nextTicket, error), error.c_str());
#if CE_RHI_STRUCTURED_FAILURE
            require(pendingSafe, "Pending ticket exposed unacquired diagnostic state.");
            auto completedFailure = stale_failure();
            require(!firstTicket.GetCommandFailure(completedFailure) &&
                        equal_failure(completedFailure, RHICommandFailure{}),
                    "Successful ticket retained a structured failure.");
            completedFailure = stale_failure();
            require(!submission.ConsumeFailure(&device, completedFailure) &&
                        equal_failure(completedFailure, RHICommandFailure{}),
                    "Successful owner retained a structured failure.");
#endif
            const auto replayed = submission.GetStats();
            require(replayed.pendingSoftwareBytes == 0 && replayed.peakSoftwareBytes == bytes &&
                        replayed.softwareSaturationWaits == 1 && replayed.pendingRetirements == 2,
                    "CPU arena credits were not returned independently of GPU retirement.");
            require(!firstLifetime.expired() && !nextLifetime.expired(),
                    "CPU replay completion released native resource leases before fence observation.");
            device.m_completed.store(1);
            const bool firstRetired = wait_observed(
                [&]
                {
                    submission.CollectCompletedLifetimes(&device);
                    return firstLifetime.expired();
                });
            require(firstRetired && !nextLifetime.expired(),
                    "Distinct completion values did not retain the later resource lease.");
            device.m_completed.store(2);
            require(submission.Drain(&device, error), error.c_str());
            submission.CollectCompletedLifetimes(&device);
            require(nextLifetime.expired(), "Completed second fence retained its native lease.");
            submission.ReleaseClient(&device);
        }
        // A translating owner cannot occupy the native submission thread. An
        // unrelated owner progresses, while that owner's tail remains ordered.
        {
            ++g_scenarios;
            RHISubmissionThread submission(bytes);
            ProbeDevice device;
            int unrelatedOwner{};
            ReplayGate gate;
            auto pool = own::make_shared<ProbePool>(scheduler);
            auto translator = own::make_shared<GatedTranslator>(*pool, services, gate);
            std::string error;
            require(submission.AcquireClient(&device, error), error.c_str());
            require(submission.AcquireClient(&unrelatedOwner, error), error.c_str());
            auto batch = record_batch(recorder, spec);
            RHISubmissionTicket translating, tail, unrelated;
            require(submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), translating,
                                                    error),
                    error.c_str());
            const bool entered = gate.WaitEntered();
            bool tailSawSubmission{};
            require(submission.Enqueue(
                        &device, "ordered owner tail",
                        [&](std::string&)
                        {
                            tailSawSubmission = translating.IsComplete();
                            return true;
                        },
                        tail, error),
                    error.c_str());
            require(submission.Enqueue(
                        &unrelatedOwner, "unrelated native submission", [](std::string&) { return true; }, unrelated,
                        error),
                    error.c_str());
            const bool progressed = wait_observed([&] { return unrelated.IsComplete(); });
            const bool ordered = !tail.IsComplete() && !translating.IsComplete();
            gate.Open();
            require(submission.Wait(translating, error) && submission.Wait(tail, error) &&
                        submission.Wait(unrelated, error),
                    error.c_str());
            require(entered && progressed && ordered && tailSawSubmission && submission.GetStats().orderingErrors == 0,
                    "Translation blocked unrelated native work or allowed same-owner overtaking.");
#if CE_RHI_DISPLAY_SUBMISSION_PROOF
            require(PollRHIDisplaySubmission(translating, tail) == RHIDisplaySubmissionStatus::Succeeded &&
                        PollRHIDisplaySubmission(tail, tail) == RHIDisplaySubmissionStatus::Succeeded,
                    "Successful graph and tail tickets did not establish CPU publication proof.");
            require(PollRHIDisplaySubmission({}, tail) == RHIDisplaySubmissionStatus::Failed &&
                        PollRHIDisplaySubmission(translating, {}) == RHIDisplaySubmissionStatus::Failed,
                    "A missing submission ticket established CPU publication proof.");
#endif
            device.m_completed.store(1);
            submission.ReleaseClient(&unrelatedOwner);
            submission.ReleaseClient(&device);
        }
        // A blocked translation keeps two software entries queued.
        // Device-loss cancellation must destroy those captures before byte credit
        // wakes an unrelated producer, and must reject a same-owner waiter.
        for (const bool sameOwner : {false, true})
        {
            ++g_scenarios;
            RHISubmissionThread submission(bytes * 2);
            ProbeDevice device;
            int blockingOwner{}, cancelledOwner{}, waitingOwner{};
            ReplayGate gate;
            ReplayGate translationGate;
            ReplayGate destructionGate;
            std::atomic<bool> destructionCompleted{};
            std::string error;
            require(submission.AcquireClient(&blockingOwner, error), error.c_str());
            require(submission.AcquireClient(&cancelledOwner, error), error.c_str());
            require(submission.AcquireClient(&waitingOwner, error), error.c_str());
            RHISubmissionTicket blocker;
            require(submission.Enqueue(
                        &blockingOwner, "hold actual RHI FIFO",
                        [&](std::string&)
                        {
                            gate.Wait();
                            return true;
                        },
                        blocker, error),
                    error.c_str());
            const bool entered = gate.WaitEntered();
            auto firstPool = own::make_shared<ProbePool>(scheduler);
            auto nextPool = own::make_shared<ProbePool>(scheduler);
            auto waitingPool = own::make_shared<ProbePool>(scheduler);
            auto firstTranslator = own::make_shared<GatedTranslator>(*firstPool, services, translationGate);
            auto nextTranslator = own::make_shared<RHICommandTranslator>(*nextPool, services, spec.queue);
            auto waitingTranslator = own::make_shared<RHICommandTranslator>(*waitingPool, services, spec.queue);
            auto firstOwner = own::make_shared<GatedDestruction>(destructionGate, destructionCompleted);
            auto nextOwner = own::make_shared<uint32_t>(2);
            ce::weak_lifetime_token firstLifetime(firstOwner), nextLifetime(nextOwner);
            auto first = record_batch(recorder, spec, firstOwner);
            auto next = record_batch(recorder, spec, nextOwner);
            auto waiting = record_batch(recorder, spec);
#if CE_RHI_STRUCTURED_FAILURE
            const uint64_t firstRecordingId = first.GetRecordingId();
            const uint64_t nextRecordingId = next.GetRecordingId();
#endif
            firstOwner.reset();
            nextOwner.reset();
            RHISubmissionTicket firstTicket, nextTicket, waitingTicket;
            const bool admittedFirst = submission.EnqueueSoftwareBatch(
                &cancelledOwner, device, firstPool, firstTranslator, std::move(first), firstTicket, error);
            const bool translationEntered = translationGate.WaitEntered();
            const bool admittedNext = submission.EnqueueSoftwareBatch(&cancelledOwner, device, nextPool, nextTranslator,
                                                                      std::move(next), nextTicket, error);
            bool waitingAdmitted{};
            bool leasesReleasedOnAdmission{};
            std::atomic<bool> producerReturned{};
            std::string waitingError;
            std::thread producer(
                [&]
                {
                    waitingAdmitted = submission.EnqueueSoftwareBatch(sameOwner ? &cancelledOwner : &waitingOwner,
                                                                      device, waitingPool, waitingTranslator,
                                                                      std::move(waiting), waitingTicket, waitingError);
                    leasesReleasedOnAdmission = firstLifetime.expired() && nextLifetime.expired();
                    if (!sameOwner)
                    {
                        leasesReleasedOnAdmission = leasesReleasedOnAdmission && destructionCompleted.load();
                    }
                    producerReturned.store(true);
                });
            const bool waited = wait_observed([&] { return submission.GetStats().softwareSaturationWaits == 1; });
            const auto before = submission.GetStats();
            submission.MarkUnrecoverableDeviceError(&cancelledOwner, "Injected confirmed device loss.");
            RHILifecycleResult abandoned;
            bool cancelled{};
            std::thread cancellation(
                [&] { cancelled = submission.AbandonForDeviceError(&cancelledOwner, abandoned, error); });
            const bool removedBeforeDestruction =
                wait_observed([&] { return submission.GetOwnerStats(&cancelledOwner).pendingTasks == 0; });
            translationGate.Open();
            const bool destructorEntered = destructionGate.WaitEntered();
            const auto destroying = submission.GetStats();
            const bool prematureAdmission = !sameOwner && producerReturned.load();
            destructionGate.Open();
            cancellation.join();
            producer.join();
            const auto after = submission.GetStats();
            gate.Open();
            const bool blockerCompleted = submission.Wait(blocker, error);
            require(entered && translationEntered && removedBeforeDestruction && admittedFirst && admittedNext &&
                        waited && before.pendingSoftwareBytes == bytes * 2,
                    "Cancellation fixture did not reach a saturated queued software cohort.");
            require(cancelled && abandoned.IsClean() && blockerCompleted, error.c_str());
            require(destructorEntered && !prematureAdmission && destroying.pendingSoftwareBytes == bytes * 2,
                    "Cancelled frame destructors returned software byte credits before completing.");
            require(firstTicket.IsComplete() && nextTicket.IsComplete() && !submission.Wait(firstTicket, error) &&
                        !submission.Wait(nextTicket, error),
                    "Cancelled software entries published a successful ticket.");
#if CE_RHI_STRUCTURED_FAILURE
            RHICommandFailure firstFailure, nextFailure;
            require(firstTicket.GetCommandFailure(firstFailure) && nextTicket.GetCommandFailure(nextFailure) &&
                        firstFailure.stage == RHICommandFailureStage::Admission &&
                        nextFailure.stage == RHICommandFailureStage::Admission &&
                        firstFailure.recordingId == firstRecordingId && nextFailure.recordingId == nextRecordingId &&
                        firstFailure.backendGeneration == spec.generation &&
                        nextFailure.backendGeneration == spec.generation && !firstFailure.executionUncertain &&
                        !nextFailure.executionUncertain && !firstFailure.message.empty() &&
                        !nextFailure.message.empty(),
                    "Cancelled admission lost software recording identity or claimed native execution.");
#endif
            require(firstLifetime.expired() && nextLifetime.expired(),
                    "Cancelled software work retained its frame captures.");
            require(after.peakSoftwareBytes == bytes * 2 && after.pendingSoftwareBytes <= bytes,
                    "Cancellation did not return bounded software byte credits.");
            if (sameOwner)
            {
                require(!waitingAdmitted && !waitingTicket.IsValid() && waitingPool->m_rejected == 1 &&
                            waitingPool->m_accepted == 0 && after.pendingSoftwareBytes == 0,
                        "A cancelled owner's blocked producer was admitted or lost rollback.");
            }
            else
            {
                require(waitingAdmitted && leasesReleasedOnAdmission,
                        "Byte admission woke before cancelled captures had been destroyed.");
                require(submission.Wait(waitingTicket, error), error.c_str());
            }
            device.m_completed.store(UINT64_MAX);
            require(submission.Drain(&blockingOwner, error), error.c_str());
            require(submission.Drain(&waitingOwner, error), error.c_str());
            submission.ReleaseClient(&cancelledOwner);
            submission.ReleaseClient(&waitingOwner);
            submission.ReleaseClient(&blockingOwner);
        }
        // Failed native submissions quarantine GPU resource leases. A failure
        // before native execute remains distinguishable from an uncertain one.
        for (const bool executed : {false, true})
        {
            ++g_scenarios;
            RHISubmissionThread submission(bytes);
            ProbeDevice device;
            auto pool = own::make_shared<ProbePool>(scheduler);
            pool->m_submitFailure = true;
            pool->m_executeBeforeFailure = executed;
            auto translator = own::make_shared<RHICommandTranslator>(*pool, services, spec.queue);
            std::string error;
            require(submission.AcquireClient(&device, error), error.c_str());
            auto owner = own::make_shared<uint32_t>(1);
            ce::weak_lifetime_token lifetime(owner);
            auto batch = record_batch(recorder, spec, owner);
#if CE_RHI_STRUCTURED_FAILURE
            const uint64_t commandRecordingId = batch.GetRecordingId();
#endif
            owner.reset();
            RHISubmissionTicket ticket;
            require(submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), ticket, error),
                    error.c_str());
            require(!submission.Wait(ticket, error) && !lifetime.expired(),
                    "Native failure lost quarantine ownership.");
#if CE_RHI_STRUCTURED_FAILURE
            RHICommandFailure failure, consumed, repeated;
            require(ticket.GetCommandFailure(failure) && failure.stage == RHICommandFailureStage::Submission &&
                        failure.recordingId == commandRecordingId && failure.backendGeneration == spec.generation &&
                        failure.executionUncertain == executed && failure.targetIndex == UINT32_MAX &&
                        failure.commandIndex == UINT64_MAX && failure.commandType == RHISoftwareCommandType::Count &&
                        failure.message == error && ticket.GetIdentity().recordingId == spec.upload,
                    "Native submission failure lost structured stage, uncertainty or independent recording identity.");
            require(submission.ConsumeFailure(&device, consumed) && equal_failure(failure, consumed) &&
                        submission.ConsumeFailure(&device, repeated) && equal_failure(failure, repeated),
                    "Queued or persistent owner failure changed structured diagnostics.");
            require(ticket.GetCommandFailure(repeated) && equal_failure(failure, repeated),
                    "Failure consumption mutated the completed ticket.");
#endif
            require(submission.GetStats().pendingSoftwareBytes == 0 && submission.GetStats().pendingRetirements == 1,
                    "Failed CPU replay retained arena credits or lost uncertain GPU retirement.");
            device.m_completed.store(UINT64_MAX);
            submission.CollectCompletedLifetimes(&device);
            require(!lifetime.expired(), "A numerically complete fence erased uncertain native failure.");
            submission.MarkUnrecoverableDeviceError(&device, "Injected confirmed device loss.");
            RHILifecycleResult abandoned;
            require(submission.AbandonForDeviceError(&device, abandoned, error) && abandoned.IsClean(), error.c_str());
            require(lifetime.expired(), "Explicit device-loss boundary retained the quarantined owner.");
            submission.ReleaseClient(&device);
        }
#if CE_RHI_STRUCTURED_FAILURE
        // Replay failure never reaches native execute, but retains the exact
        // software command identity through the actual submission FIFO. An
        // already admitted EndFrame tail may still signal a higher GPU fence;
        // that numeric completion must never make the failed image publishable.
        {
            ++g_scenarios;
            RHISubmissionThread submission(bytes);
            ProbeDevice device;
            ReplayGate gate;
            auto pool = own::make_shared<ProbePool>(scheduler);
            pool->m_encoders[0].m_failCommand = 12;
            auto translator = own::make_shared<GatedTranslator>(*pool, services, gate);
            std::string error;
            require(submission.AcquireClient(&device, error), error.c_str());
            auto batch = record_batch(recorder, spec);
            const uint64_t commandRecordingId = batch.GetRecordingId();
            RHISubmissionTicket ticket, tail;
            require(submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), ticket, error),
                    error.c_str());
            const bool entered = gate.WaitEntered();
            const bool tailAdmitted = submission.Enqueue(
                &device, "successful higher-fence frame tail",
                [&](std::string&)
                {
                    device.m_completed.store(17);
                    return true;
                },
                tail, error);
#if CE_RHI_DISPLAY_SUBMISSION_PROOF
            const auto preparing = PollRHIDisplaySubmission(ticket, tail);
#endif
            gate.Open();
            require(entered && tailAdmitted, "Failed replay fixture did not admit a later frame tail.");
            require(!submission.Wait(ticket, error) && pool->m_submitCalls == 0,
                    "Failed replay reached the native submission boundary.");
            RHICommandFailure failure, consumed;
            require(ticket.GetCommandFailure(failure) && failure.stage == RHICommandFailureStage::Translation &&
                        failure.recordingId == commandRecordingId && failure.backendGeneration == spec.generation &&
                        !failure.executionUncertain && failure.targetIndex == 0 && failure.commandIndex == 12 &&
                        failure.commandType == RHISoftwareCommandType::DispatchIndirect && failure.message == error,
                    "FIFO replay failure lost exact target, opcode, index or recording provenance.");
            require(submission.ConsumeFailure(&device, consumed) && equal_failure(failure, consumed),
                    "Owner failure consumption lost replay provenance.");
            require(submission.Wait(tail, error) && device.GetCompletedFenceValue() == 17 &&
                        pool->m_completion < device.GetCompletedFenceValue(),
                    "The admitted frame tail did not advance the higher GPU completion independently.");
#if CE_RHI_DISPLAY_SUBMISSION_PROOF
            const bool pixelsPublishable =
                device.GetCompletedFenceValue() >= pool->m_completion &&
                PollRHIDisplaySubmission(ticket, tail) == RHIDisplaySubmissionStatus::Succeeded;
            require(preparing == RHIDisplaySubmissionStatus::Pending && !pixelsPublishable &&
                        PollRHIDisplaySubmission(ticket, tail) == RHIDisplaySubmissionStatus::Failed &&
                        PollRHIDisplaySubmission(tail, ticket) == RHIDisplaySubmissionStatus::Failed &&
                        PollRHIDisplaySubmission(tail, tail) == RHIDisplaySubmissionStatus::Succeeded,
                    "A successful higher-fence tail concealed a failed scene batch at publication.");
#endif
            submission.MarkUnrecoverableDeviceError(&device, "Injected cleanup after failed CPU replay.");
            RHILifecycleResult abandoned;
            require(submission.AbandonForDeviceError(&device, abandoned, error) && abandoned.IsClean(), error.c_str());
            submission.ReleaseClient(&device);
        }
#endif
        scheduler.shutdown();
    }
#endif

    void ticket_storage_contracts()
    {
        ++g_scenarios;
        RHISubmissionThread submission;
        int owner{}, failedOwner{};
        std::string error;
        require(submission.AcquireClient(&owner, error) && submission.AcquireClient(&failedOwner, error),
                error.c_str());
        RHISubmissionTicket failed;
        require(submission.Enqueue(
                    &failedOwner, "retained failure result",
                    [](std::string& failure)
                    {
                        failure = "Retain this failure unchanged while later tickets complete.";
                        return false;
                    },
                    failed, error),
                error.c_str());
        require(!submission.Wait(failed, error), "Injected retained ticket failure succeeded.");
        const auto failedSequence = failed.GetSequence();
        const std::string failedMessage = error;
        // There are no GPU commands here; explicitly close the failed owner so
        // later ticket reuse also crosses an owner shutdown/restart boundary.
        submission.MarkUnrecoverableDeviceError(&failedOwner, "Retained result fixture cleanup.");
        RHILifecycleResult abandoned;
        require(submission.AbandonForDeviceError(&failedOwner, abandoned, error) && abandoned.IsClean(), error.c_str());
        submission.ReleaseClient(&failedOwner);

        // Keep more completed results than the bounded result cache can retain.
        // Neither cache reuse nor its uncached fallback may mutate a caller's
        // still-live ticket or an independently copied alias of that ticket.
        std::array<RHISubmissionTicket, 96> retained;
        std::array<uint64_t, 96> sequences{};
        for (size_t index = 0; index < retained.size(); ++index)
        {
            require(
                submission.Enqueue(
                    &owner, "retained successful result", [](std::string&) { return true; }, retained[index], error) &&
                    submission.Wait(retained[index], error),
                error.c_str());
            sequences[index] = retained[index].GetSequence();
        }
        const auto alias = retained.front();
        for (size_t index = 0; index < retained.size(); ++index)
        {
            require(retained[index].IsComplete() && retained[index].GetSequence() == sequences[index] &&
                        submission.Wait(retained[index], error),
                    "Later admission overwrote a retained completed ticket.");
            retained[index] = {};
        }
        for (uint32_t index = 0; index < 96; ++index)
        {
            RHISubmissionTicket recycled;
            require(submission.Enqueue(
                        &owner, "recycled successful result", [](std::string&) { return true; }, recycled, error) &&
                        submission.Wait(recycled, error),
                    error.c_str());
        }
        submission.ReleaseClient(&owner);
        require(alias.IsComplete() && alias.GetSequence() == sequences.front() && submission.Wait(alias, error),
                "Ticket alias changed after cache reuse and owner shutdown.");
        require(failed.IsComplete() && failed.GetSequence() == failedSequence && !submission.Wait(failed, error) &&
                    error == failedMessage,
                "Retained failed ticket changed after cache reuse and owner shutdown.");
    }

    void submission_owner_contracts()
    {
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(4);
        ProbeValidator validator;
        ProbeDeviceServices services;
        RHISoftwareRecorder recorder(validator);
        BatchSpec spec;
        spec.targets = spec.segments = 1;
        // CPU ticket storage must not retain the pool, translator or frame owner
        // after finite retirement, even while that ticket and service stay live.
        {
            ++g_scenarios;
            RHISubmissionThread submission;
            ProbeDevice device;
            int otherOwner{};
            auto pool = own::make_shared<ProbePool>(scheduler);
            auto translator = own::make_shared<RHICommandTranslator>(*pool, services, spec.queue);
            auto resources = own::make_shared<uint32_t>(9);
            ce::weak_lifetime_token poolLifetime(pool), translatorLifetime(translator), resourceLifetime(resources);
            auto batch = record_batch(recorder, spec, resources);
            std::string error;
            require(submission.AcquireClient(&device, error) && submission.AcquireClient(&otherOwner, error),
                    error.c_str());
            RHISubmissionTicket ticket;
            require(submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), ticket, error),
                    error.c_str());
            pool.reset();
            translator.reset();
            resources.reset();
            require(submission.Wait(ticket, error), error.c_str());
            const auto sequence = ticket.GetSequence();
            const auto identity = ticket.GetIdentity();
            require(!poolLifetime.expired() && !translatorLifetime.expired() && !resourceLifetime.expired(),
                    "CPU completion released the native submission owners before finite GPU completion.");
            device.m_completed.store(identity.completion.value);
            require(submission.Drain(&device, error), error.c_str());
            require(poolLifetime.expired() && translatorLifetime.expired() && resourceLifetime.expired(),
                    "Completed ticket storage retained native owners after finite retirement.");
            submission.ReleaseClient(&device);
            RHISubmissionTicket other;
            require(submission.Enqueue(
                        &otherOwner, "post-shutdown result", [](std::string&) { return true; }, other, error) &&
                        submission.Wait(other, error),
                    error.c_str());
            require(ticket.IsComplete() && ticket.GetSequence() == sequence &&
                        ticket.GetIdentity().completion.value == identity.completion.value &&
                        submission.Wait(ticket, error) && poolLifetime.expired() && translatorLifetime.expired() &&
                        resourceLifetime.expired(),
                    "Owner shutdown changed a retained ticket or reattached retired native owners.");
            submission.ReleaseClient(&otherOwner);
        }
        // A custom translator is allowed to throw without consuming its input.
        // Reserve the other storage slots so reuse directly proves the CPU
        // recording returned before its admission credits, while GPU resources
        // remain quarantined until the explicit device-loss cleanup boundary.
        {
            ++g_scenarios;
            std::array<RHISoftwareBatch, 8> parked;
            for (auto& batch : parked)
            {
                batch = record_batch(recorder, spec);
            }
            auto warm = record_batch(recorder, spec);
            warm = {};
            allocation_probe::begin();
            auto reference = record_batch(recorder, spec);
            const auto expectedAllocations = allocation_probe::end();
            reference = {};

            RHISubmissionThread submission;
            ProbeDevice device;
            auto pool = own::make_shared<ProbePool>(scheduler);
            auto translator = own::make_shared<ThrowBeforeConsumeTranslator>();
            auto resources = own::make_shared<uint32_t>(11);
            ce::weak_lifetime_token resourceLifetime(resources);
            auto batch = record_batch(recorder, spec, resources);
            resources.reset();
            std::string error;
            require(submission.AcquireClient(&device, error), error.c_str());
            RHISubmissionTicket ticket;
            require(submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), ticket, error),
                    error.c_str());
            const bool failed = !submission.Wait(ticket, error);
            const bool cpuReleased = submission.GetStats().pendingSoftwareBytes == 0;
            const bool gpuRetained = !resourceLifetime.expired();
            allocation_probe::begin();
            auto reused = record_batch(recorder, spec);
            const auto observedAllocations = allocation_probe::end();
            submission.MarkUnrecoverableDeviceError(&device, "Pre-consumption translation fixture cleanup.");
            RHILifecycleResult abandoned;
            require(submission.AbandonForDeviceError(&device, abandoned, error) && abandoned.IsClean(), error.c_str());
            submission.ReleaseClient(&device);
            require(failed && cpuReleased && pool->m_submitCalls == 0 && gpuRetained && resourceLifetime.expired(),
                    "Pre-consumption translation exception violated CPU/GPU owner boundaries.");
            require(observedAllocations == expectedAllocations,
                    "Pre-consumption translation exception returned credits before releasing CPU recording storage.");
        }
        scheduler.shutdown();
    }

    struct AllocationResult
    {
        std::array<uint64_t, 2> stable{};
        std::array<uint64_t, 2> scale{};
        uint64_t allocatedBytes{};
        uint64_t commandCount{};
    };

    AllocationResult allocation_contracts()
    {
        ProbeValidator validator;
        RHISoftwareRecorder recorder(validator);
        BatchSpec spec;
        AllocationResult result;
        for (uint32_t warm = 0; warm < 16; ++warm)
        {
            auto batch = record_batch(recorder, spec);
        }
        constexpr size_t samples = 17;
        std::array<uint64_t, samples> stableStandard{}, stableMimalloc{}, scaleStandard{}, scaleMimalloc{};
        for (size_t sample = 0; sample < samples; ++sample)
        {
            allocation_probe::begin();
            auto batch = record_batch(recorder, spec);
            const auto counts = allocation_probe::end();
            stableStandard[sample] = counts[0];
            stableMimalloc[sample] = counts[1];
            result.allocatedBytes = batch.GetRecordingStats().allocatedBytes;
            result.commandCount = batch.GetRecordingStats().commandCount;
        }
        for (size_t sample = 0; sample < samples; ++sample)
        {
            spec.targets = 1;
            auto smaller = record_batch(recorder, spec);
            smaller = {};
            spec.targets = 8;
            allocation_probe::begin();
            auto batch = record_batch(recorder, spec);
            const auto counts = allocation_probe::end();
            scaleStandard[sample] = counts[0];
            scaleMimalloc[sample] = counts[1];
        }
        std::sort(stableStandard.begin(), stableStandard.end());
        std::sort(stableMimalloc.begin(), stableMimalloc.end());
        std::sort(scaleStandard.begin(), scaleStandard.end());
        std::sort(scaleMimalloc.begin(), scaleMimalloc.end());
        result.stable = {stableStandard[samples / 2], stableMimalloc[samples / 2]};
        result.scale = {scaleStandard[samples / 2], scaleMimalloc[samples / 2]};
        return result;
    }

    struct SubmissionAllocationResult
    {
        uint32_t targets{};
        std::array<uint64_t, 2> minimum{};
        std::array<uint64_t, 2> median{};
        std::array<uint64_t, 2> maximum{};
        std::array<uint64_t, 2> total{};
        uint64_t commands{};
        double minimumMicroseconds{};
        double medianMicroseconds{};
        double p95Microseconds{};
        double maximumMicroseconds{};
    };

    std::array<SubmissionAllocationResult, 2> submission_allocation_contracts()
    {
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(8);
        ProbeValidator validator;
        ProbeDeviceServices services;
        ProbeDevice device;
        RHISoftwareRecorder recorder(validator);
        RHISubmissionThread submission;
        auto pool = own::make_shared<ProbePool>(scheduler);
        auto translator =
            own::make_shared<RHICommandTranslator>(*pool, services, RHIQueueIdentity{1, 1, RHIQueueKind::Graphics});
        auto frameOwner = own::make_shared<uint32_t>(42);
        std::string error;
        require(submission.AcquireClient(&device, error), error.c_str());
        constexpr size_t warmup = 16;
        constexpr size_t samples = 33;
        std::array<SubmissionAllocationResult, 2> result{};
        for (size_t shape = 0; shape < result.size(); ++shape)
        {
            BatchSpec spec;
            spec.targets = shape == 0 ? 1 : 8;
            spec.draws = 512;
            result[shape].targets = spec.targets;
            std::array<uint64_t, samples> standard{}, mimalloc{};
            std::array<double, samples> durations{};
            for (size_t sample = 0; sample < warmup + samples; ++sample)
            {
                pool->Reset();
                // Frozen frame recording and fixture setup are outside this
                // scope. Count actual calls on producer, coordinator and workers
                // from admission through CPU submit and finite GPU retirement.
                auto batch = record_batch(recorder, spec, frameOwner);
                result[shape].commands = batch.GetRecordingStats().commandCount;
                RHISubmissionTicket ticket;
                allocation_probe::begin();
                const auto start = std::chrono::steady_clock::now();
                const bool admitted =
                    submission.EnqueueSoftwareBatch(&device, device, pool, translator, std::move(batch), ticket, error);
                const bool submitted = admitted && submission.Wait(ticket, error);
                device.m_completed.store(pool->m_completion);
                const bool retired = submitted && submission.Drain(&device, error);
                ticket = {};
                const auto elapsed = std::chrono::steady_clock::now() - start;
                const auto counts = allocation_probe::end();
                require(admitted && submitted && retired, error.c_str());
                require(submission.GetStats().pendingSoftwareBytes == 0 &&
                            submission.GetStats().pendingRetirements == 0,
                        "Submission allocation scope ended before CPU/GPU owners retired.");
                if (sample >= warmup)
                {
                    standard[sample - warmup] = counts[0];
                    mimalloc[sample - warmup] = counts[1];
                    durations[sample - warmup] = std::chrono::duration<double, std::micro>(elapsed).count();
                    result[shape].total[0] += counts[0];
                    result[shape].total[1] += counts[1];
                }
            }
            std::sort(standard.begin(), standard.end());
            std::sort(mimalloc.begin(), mimalloc.end());
            std::sort(durations.begin(), durations.end());
            result[shape].minimum = {standard.front(), mimalloc.front()};
            result[shape].median = {standard[samples / 2], mimalloc[samples / 2]};
            result[shape].maximum = {standard.back(), mimalloc.back()};
            result[shape].minimumMicroseconds = durations.front();
            result[shape].medianMicroseconds = durations[samples / 2];
            result[shape].p95Microseconds = durations[static_cast<size_t>(samples * .95)];
            result[shape].maximumMicroseconds = durations.back();
        }
        submission.ReleaseClient(&device);
        scheduler.shutdown();
        return result;
    }

    struct TimingResult
    {
        const char* name{};
        uint32_t workers{};
        uint32_t parallelJobs{};
        uint64_t commandCount{};
        double serialMedian{};
        double serialP95{};
        double parallelMedian{};
        double parallelP95{};
        std::array<uint64_t, 9> observedCommands{};
    };

    TimingResult benchmark(uint32_t physicalWorkers, const char* name, uint32_t draws, uint32_t skew, uint32_t spin)
    {
        thread_pool workers;
        job_scheduler scheduler(workers);
        scheduler.start(physicalWorkers);
        ProbePool pool(scheduler);
        ProbeDeviceServices services;
        ProbeValidator validator;
        RHISoftwareRecorder recorder(validator);
        RHICommandTranslator translator(pool, services, {1, 1, RHIQueueKind::Graphics});
        BatchSpec spec;
        spec.segments = 4;
        spec.draws = draws;
        spec.skew = skew;
        spec.spin = spin;
        TimingResult result;
        result.name = name;
        result.workers = physicalWorkers;
        const auto expected = expected_digests(spec);
        const RHICommandTranslationOptions serial{RHICommandTranslationMode::Serial, 1, 0};
        const RHICommandTranslationOptions parallel{RHICommandTranslationMode::Parallel, 8, 256};
        const auto run = [&](const RHICommandTranslationOptions& options)
        {
            pool.Reset();
            for (auto& encoder : pool.m_encoders)
            {
                encoder.m_spin = spin;
                encoder.m_active = nullptr;
            }
            auto batch = record_batch(recorder, spec);
            result.commandCount = batch.GetRecordingStats().commandCount;
            RHITranslatedBatch translated;
            RHICommandFailure failure;
            const auto start = std::chrono::steady_clock::now();
            const bool succeeded = translator.Translate(std::move(batch), options, translated, failure);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            require(succeeded, failure.message.c_str());
            if (options.mode == RHICommandTranslationMode::Parallel)
            {
                result.parallelJobs = translated.GetTranslationStats().translationJobCount;
            }
            verify_digests(pool, spec, expected);
            return std::chrono::duration<double, std::micro>(elapsed).count();
        };
        for (uint32_t warm = 0; warm < 16; ++warm)
        {
            run(serial);
            run(parallel);
        }
        constexpr size_t samples = 65;
        std::array<double, samples> serialSamples{}, parallelSamples{};
        for (size_t index = 0; index < samples; ++index)
        {
            if (index & 1)
            {
                parallelSamples[index] = run(parallel);
                serialSamples[index] = run(serial);
            }
            else
            {
                serialSamples[index] = run(serial);
                parallelSamples[index] = run(parallel);
            }
        }
        std::sort(serialSamples.begin(), serialSamples.end());
        std::sort(parallelSamples.begin(), parallelSamples.end());
        result.serialMedian = serialSamples[samples / 2];
        result.serialP95 = serialSamples[static_cast<size_t>(samples * .95)];
        result.parallelMedian = parallelSamples[samples / 2];
        result.parallelP95 = parallelSamples[static_cast<size_t>(samples * .95)];
        run(parallel);
        for (const auto& encoder : pool.m_encoders)
        {
            require(encoder.m_worker < result.observedCommands.size(),
                    "Observed worker was outside the fixed fixture slots.");
            result.observedCommands[encoder.m_worker] += encoder.m_digest.commands;
        }
        scheduler.shutdown();
        return result;
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        bool timings{};
        for (int index = 1; index < argc; ++index)
        {
            if (std::string_view(argv[index]) == "--benchmark")
            {
                timings = true;
            }
        }
        thread_pool::set_worker_hooks({worker_start, nullptr, nullptr});
        verify_allocation_observer();
        for (const uint32_t workers : {1u, 2u, 4u, 8u})
        {
            correctness_matrix(workers);
        }
        failure_contracts();
#if CE_RHI_BYTE_ADMISSION
        byte_admission_contracts();
#endif
        ticket_storage_contracts();
        submission_owner_contracts();
        const auto allocations = allocation_contracts();
        const auto submissionAllocations = submission_allocation_contracts();
        std::array<TimingResult, 12> timingResults{};
        size_t timingCount{};
        if (timings)
        {
            for (const uint32_t workers : {1u, 4u, 8u})
            {
                timingResults[timingCount++] = benchmark(workers, "small", 1, 0, 0);
                timingResults[timingCount++] = benchmark(workers, "uniform", 4096, 0, 0);
                timingResults[timingCount++] = benchmark(workers, "skew", 128, 16384, 0);
                timingResults[timingCount++] = benchmark(workers, "uniform_cpu_work", 1024, 0, 32);
            }
        }
        std::cout
            << std::setprecision(12) << "{\"status\":\"PASS\",\"checks\":" << g_checks
            << ",\"scenarios\":" << g_scenarios
            << ",\"actual_production_translation\":true,\"actual_submission_fifo\":true,\"native_execution\":false,"
            << "\"workers\":[1,2,4,8],\"allocation_samples\":17,\"allocation_warmup\":16,"
            << "\"allocations\":{\"scope\":\"BeginRecording+AcquireEncoder+FinishRecording; actual std new and "
               "mimalloc API requests\","
            << "\"stable8\":{\"standard\":" << allocations.stable[0] << ",\"mimalloc\":" << allocations.stable[1]
            << "},\"scale8_1_8\":{\"standard\":" << allocations.scale[0] << ",\"mimalloc\":" << allocations.scale[1]
            << "},\"allocated_arena_bytes\":" << allocations.allocatedBytes
            << ",\"commands\":" << allocations.commandCount << "},\"submission_allocations\":{"
            << "\"scope\":\"EnqueueSoftwareBatch+Translate+Submit+finite completion retirement; recording excluded; "
               "all-thread actual std new and mimalloc API requests\",\"warmup\":16,\"samples\":33,\"cases\":[";
        for (size_t index = 0; index < submissionAllocations.size(); ++index)
        {
            if (index)
            {
                std::cout << ',';
            }
            const auto& result = submissionAllocations[index];
            std::cout << "{\"targets\":" << result.targets << ",\"commands\":" << result.commands
                      << ",\"standard\":{\"min\":" << result.minimum[0] << ",\"median\":" << result.median[0]
                      << ",\"max\":" << result.maximum[0] << ",\"total\":" << result.total[0]
                      << "},\"mimalloc\":{\"min\":" << result.minimum[1] << ",\"median\":" << result.median[1]
                      << ",\"max\":" << result.maximum[1] << ",\"total\":" << result.total[1]
                      << "},\"cpu_microseconds\":{\"min\":" << result.minimumMicroseconds
                      << ",\"p50\":" << result.medianMicroseconds << ",\"p95\":" << result.p95Microseconds
                      << ",\"max\":" << result.maximumMicroseconds << "}}";
        }
        std::cout << "]},\"timings\":[";
        for (size_t index = 0; index < timingCount; ++index)
        {
            const auto& result = timingResults[index];
            if (index)
            {
                std::cout << ',';
            }
            std::cout << "{\"case\":\"" << result.name << "\",\"physical_workers\":" << result.workers
                      << ",\"parallel_jobs\":" << result.parallelJobs << ",\"min_commands_per_job\":256"
                      << ",\"commands\":" << result.commandCount << ",\"samples\":65,\"warmup_pairs\":16,"
                      << "\"alternating_serial_parallel\":true,\"serial_us\":{\"p50\":" << result.serialMedian
                      << ",\"p95\":" << result.serialP95 << "},\"parallel_us\":{\"p50\":" << result.parallelMedian
                      << ",\"p95\":" << result.parallelP95 << "},\"observed_commands_by_worker_then_owner\":[";
            for (size_t worker = 0; worker < result.observedCommands.size(); ++worker)
            {
                if (worker)
                {
                    std::cout << ',';
                }
                std::cout << result.observedCommands[worker];
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        allocation_probe::enabled.store(false);
        std::cerr << "RHI_SOFTWARE_PORTABLE_FAILURE checks=" << g_checks << " " << exception.what() << '\n';
        return 1;
    }
}
