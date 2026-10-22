#pragma once
#include "../Utility_Framework/Ownership.h"

#include "MaterialGraphTransientBufferPool.h"
#include "Render/Graph/EnhancedRenderGraph.h"

namespace material_graph
{
class ResidentBufferCache;

// The version owns device-local bytes. This frame wrapper owns graph identity and
// its upload plan; a reused version never carries a previous graph's handles.
class ResidentBufferFrame final
{
  public:
    RHIBufferHandle Buffer() const { return version_->buffer; }
    uint64_t Size() const { return version_->bytes.size(); }
    uint32_t Stride() const { return version_->stride; }
    uint64_t UploadBytes() const { return uploadBytes_; }
    uint64_t CopyBytes() const { return copyBytes_; }
    bool Reused() const { return copies_.empty(); }

    RHIBufferSlice Slice(uint64_t offset = 0, uint64_t bytes = 0) const
    {
        if (offset > Size() || (bytes && bytes > Size() - offset))
            return {};
        RHIBufferSlice result;
        result.buffer = Buffer();
        result.offset = offset;
        result.size = bytes ? bytes : Size() - offset;
        return result.size ? result : RHIBufferSlice{};
    }

    void Declare(EnhancedRenderGraph& graph, own::shared_owner<const ResidentBufferFrame> owner) const
    {
        if (graphOwner_ == graph.DeclarationGraph() && graphEpoch_ == graph.DeclarationEpoch())
        {
            return;
        }
        if (graphOwner_ || !owner || owner.operator->() != this ||
            graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned ||
            version_->device->GetCurrentUploadRecordingId() != recording_)
            throw std::runtime_error("METEOR resident table requires its current owned versioned graph.");

        graphOwner_ = graph.DeclarationGraph();
        graphEpoch_ = graph.DeclarationEpoch();
        const auto import = [&](RHIBufferHandle buffer, const std::string& name) {
            auto handle = graph.FindImportedBuffer(buffer);
            if (!handle.IsValid())
                handle = graph.ImportBuffer(buffer, RHIResourceState::Common, name);
            graph.RequireImportedFinalState(handle, RHIResourceState::Common);
            return handle;
        };
        graphBuffer_ = import(Buffer(), name_);
        if (copies_.empty())
            return;

        graphBuffer_ = graph.Write(graphBuffer_);
        auto uses = graph.CreateDeclarationUsages({
            {graphBuffer_, RHIResourceState::CopyDest, RGAccessMode::Write}});
        if (copyBytes_)
            uses.push_back({import(base_->buffer, name_ + ".Base"), RHIResourceState::CopySource, RGAccessMode::Read});
        const auto pass = graph.AddPass(name_ + ".Upload", std::move(uses), [owner](const auto& execution) {
            for (const auto& copy : owner->copies_)
            {
                auto destination = owner->Slice(copy.offset, copy.bytes);
                RHIBufferSlice source;
                if (copy.upload)
                    source = owner->staging_.SubRange(copy.stagingOffset, copy.bytes);
                else
                {
                    source.buffer = owner->base_->buffer;
                    source.offset = copy.offset;
                    source.size = copy.bytes;
                }
                if (!source.IsValid() || !execution.encoder->CopyBuffer(destination, source))
                    throw std::runtime_error("METEOR resident table copy failed.");
            }
            // A prepared but unused/culled copy is not a completed GPU baseline.
            owner->version_->copyRecorded.store(true, std::memory_order_release);
        });
        graph.DeclareRecordingIndependent(pass);
    }

    RGHandle GraphBuffer(const EnhancedRenderGraph& graph) const
    {
        if (graphOwner_ != graph.DeclarationGraph() || graphEpoch_ != graph.DeclarationEpoch() ||
            !graphBuffer_.IsValid())
        {
            throw std::runtime_error("METEOR resident table requires its published graph version.");
        }
        return graphBuffer_;
    }

    void AddRead(const EnhancedRenderGraph& graph, auto& uses) const
    {
        const auto handle = graph.CanonicalDeclarationHandle(GraphBuffer(graph));
        for (const auto& use : uses)
        {
            const auto current = graph.CanonicalDeclarationHandle(use.handle);
            if (current.index != handle.index || current.kind != handle.kind || current.epoch != handle.epoch)
            {
                continue;
            }
            if (current.version != handle.version || use.state != RHIResourceState::ShaderResource ||
                use.access != RGAccessMode::Read)
            {
                throw std::runtime_error("METEOR resident table has conflicting graph uses.");
            }
            return;
        }
        uses.push_back({handle, RHIResourceState::ShaderResource, RGAccessMode::Read});
    }

  private:
    friend class ResidentBufferCache;
    struct Version final
    {
        IRenderDeviceServices* device{};
        own::shared_owner<TransientBufferPool> pool;
        RHIBufferHandle buffer;
        std::vector<std::byte> bytes;
        uint32_t stride{};
        uint64_t serial{};
        mutable std::atomic<bool> copyRecorded{false};
        ~Version()
        {
            if (device)
                ReleaseSceneBuffer(*device, pool, buffer);
        }
    };
    struct Copy
    {
        uint64_t offset{}, bytes{}, stagingOffset{};
        bool upload{};
    };
    own::shared_owner<const Version> version_, base_;
    RHIBufferSlice staging_;
    std::vector<Copy> copies_;
    std::string name_;
    uint64_t recording_{}, uploadBytes_{}, copyBytes_{};
    mutable const EnhancedRenderGraph* graphOwner_{};
    mutable uint64_t graphEpoch_{};
    mutable RGHandle graphBuffer_;
};

// Owner-thread preparation, submission-listener publication. Versions are never
// overwritten: GPU readers and delta-copy sources retain their exact owners.
// Every user of a version belongs to the device's ordered primary frame stream.
// Owned compute queues wait for its graphics prologue and join its epilogue;
// the next frame prefix therefore follows every earlier reader's final Common
// transition. Immutable bytes alone would not justify cross-queue state changes
// for a caller outside that submission/join contract.
// ShutdownAfterIdle follows the renderer's existing submission/GPU idle boundary.
class ResidentBufferCache final : private IRHIUploadTransactionListener
{
  public:
    using Key = std::tuple<uint64_t, uint64_t, uint32_t>; // scene epoch, view, table
    ResidentBufferCache() = default;
    ~ResidentBufferCache() { ShutdownAfterIdle(); }
    ResidentBufferCache(const ResidentBufferCache&) = delete;
    ResidentBufferCache& operator=(const ResidentBufferCache&) = delete;

    bool Initialize(IRenderDeviceServices& device, own::shared_owner<TransientBufferPool> pool, std::string& error)
    {
        error.clear();
        if (device_)
        {
            if (device_ == &device && pool_.operator->() == pool.operator->())
                return true;
            error = "METEOR resident table cache must retire before changing its device or pool.";
            return false;
        }
        device_ = &device;
        pool_ = std::move(pool);
        device_->RegisterUploadTransactionListener(this);
        return true;
    }

    bool Prepare(const Key& key, std::span<const std::byte> bytes, uint32_t stride, std::string_view name,
                 own::shared_owner<const ResidentBufferFrame>& result, std::string& error)
    {
        result.reset();
        error.clear();
        const auto recording = device_ ? device_->GetCurrentUploadRecordingId() : 0;
        if (!recording || bytes.empty() || !stride || stride % 4 || bytes.size() % stride || name.empty())
        {
            error = "METEOR resident table requires current recording and nonempty word-aligned records.";
            return false;
        }
        own::shared_owner<const Version> base;
        decltype(baselines_) retiredBaselines;
        uint64_t serial{};
        {
            std::lock_guard lock(mutex_);
            if (serial_ == UINT64_MAX)
            {
                error = "METEOR resident table version serial exhausted.";
                return false;
            }
            serial = ++serial_;
            auto found = baselines_.find(key);
            if (found == baselines_.end())
            {
                if (baselines_.size() >= kMaxBaselines)
                    EraseOldestBaseline(retiredBaselines);
                found = baselines_.try_emplace(key).first;
            }
            found->second.lastUse = serial;
            base = found->second.version;
        }
        auto frame = own::make_shared<ResidentBufferFrame>();
        frame->recording_ = recording;
        frame->name_ = std::string(name);
        if (base && base->stride == stride && base->bytes.size() == bytes.size() &&
            std::memcmp(base->bytes.data(), bytes.data(), bytes.size()) == 0)
        {
            frame->version_ = std::move(base);
            result = std::move(frame);
            return true;
        }

        auto version = own::make_shared<Version>();
        version->device = device_;
        version->pool = pool_;
        version->stride = stride;
        version->serial = serial;
        version->bytes.assign(bytes.begin(), bytes.end());
        frame->version_ = version;
        frame->base_ = base;

        // Compare whole records, coalescing small records into at least 256-byte
        // upload pages. Bound copy command count when changes are fragmented.
        const uint64_t recordsPerPage = (std::max)(uint64_t{1}, (uint64_t{256} + stride - 1) / stride);
        const uint64_t pageBytes = recordsPerPage * stride;
        for (uint64_t offset = 0; offset < bytes.size();)
        {
            const auto count = (std::min)(pageBytes, uint64_t(bytes.size()) - offset);
            const bool upload = !base || base->stride != stride || offset > base->bytes.size() ||
                                count > base->bytes.size() - offset ||
                                std::memcmp(base->bytes.data() + offset, bytes.data() + offset,
                                            static_cast<size_t>(count)) != 0;
            if (!frame->copies_.empty() && frame->copies_.back().upload == upload)
                frame->copies_.back().bytes += count;
            else
                frame->copies_.push_back({offset, count, 0, upload});
            offset += count;
            if (frame->copies_.size() > kMaxCopyRanges)
            {
                frame->copies_ = {{0, uint64_t(bytes.size()), 0, true}};
                break;
            }
        }
        for (auto& copy : frame->copies_)
        {
            if (copy.upload)
            {
                copy.stagingOffset = frame->uploadBytes_;
                frame->uploadBytes_ += copy.bytes;
            }
            else
                frame->copyBytes_ += copy.bytes;
        }
        if (frame->uploadBytes_)
        {
            frame->staging_ = device_->AllocateUpload({frame->uploadBytes_, RHIUploadUsage::BufferCopy, 16});
            if (!frame->staging_.IsWritable())
            {
                error = "METEOR resident table delta staging allocation failed.";
                return false;
            }
            for (const auto& copy : frame->copies_)
                if (copy.upload)
                    std::memcpy(static_cast<std::byte*>(frame->staging_.cpuAddress) + copy.stagingOffset,
                                bytes.data() + copy.offset, static_cast<size_t>(copy.bytes));
        }
        if (!frame->copyBytes_)
            frame->base_.reset();
        const std::wstring debugName(name.begin(), name.end());
        RHIBufferDesc desc;
        desc.bytes = bytes.size();
        desc.debugName = debugName.c_str();
        if (!CreateSceneBuffer(*device_, pool_.operator->(), desc, version->buffer, error))
            return false;

        // All ownership allocation precedes graph recording/native admission.
        {
            std::lock_guard lock(mutex_);
            recordings_[recording].versions.push_back({key, std::move(version)});
        }
        result = std::move(frame);
        return true;
    }

    // The host calls this only after the exact frame's CPU submission ticket
    // succeeded (and an owned-queue graph joined its complete graphics epilogue).
    // A later fence value alone cannot certify a partially rejected queue plan.
    void ConfirmSubmitted(uint64_t recording, RHICompletionPoint completion)
    {
        uint64_t completed{};
        {
            std::lock_guard lock(mutex_);
            if (const auto found = recordings_.find(recording); found != recordings_.end() &&
                found->second.accepted && completion.IsValid() && found->second.completion == completion.value)
                found->second.confirmed = true;
            completed = completed_;
        }
        // Completion may have arrived before the host consumed its CPU ticket.
        OnUploadCompleted(completed);
    }

    void ShutdownAfterIdle()
    {
        if (!device_)
            return;
        device_->UnregisterUploadTransactionListener(this);
        decltype(recordings_) recordings;
        decltype(baselines_) baselines;
        {
            std::lock_guard lock(mutex_);
            recordings.swap(recordings_);
            baselines.swap(baselines_);
            cachedBytes_ = completed_ = serial_ = 0;
        }
        // Destroy owners outside the cache lock while the device/pool still exist.
        recordings.clear();
        baselines.clear();
        pool_.reset();
        device_ = nullptr;
    }

  private:
    using Version = ResidentBufferFrame::Version;
    struct Baseline
    {
        own::shared_owner<const Version> version;
        uint64_t lastUse{};
    };
    using BaselineMap = std::map<Key, Baseline>;
    struct Prepared
    {
        Key key;
        own::shared_owner<const Version> version;
    };
    struct Recording
    {
        std::vector<Prepared> versions;
        uint64_t completion{};
        bool submitted{}, accepted{}, confirmed{};
    };
    static constexpr size_t kMaxBaselines = 64;
    static constexpr size_t kMaxCopyRanges = 64;
    static constexpr uint64_t kMaxCachedBytes = 64ull << 20;

    void EraseOldestBaseline(BaselineMap& retired)
    {
        const auto oldest = std::ranges::min_element(baselines_, {}, [](const auto& item) { return item.second.lastUse; });
        if (oldest == baselines_.end())
            return;
        if (oldest->second.version)
            cachedBytes_ -= oldest->second.version->bytes.size();
        // Transfer the existing node without allocation. The caller destroys
        // the retained owner after releasing mutex_, including admission LRU.
        retired.insert(baselines_.extract(oldest));
    }
    void OnUploadSubmitted(uint64_t recording, RHICompletionPoint completion) override
    {
        std::lock_guard lock(mutex_);
        if (const auto found = recordings_.find(recording); found != recordings_.end())
        {
            found->second.submitted = true;
            if (!found->second.accepted)
                found->second.completion = completion.value;
        }
    }
    void OnUploadAccepted(uint64_t recording, RHICompletionPoint completion) override
    {
        std::lock_guard lock(mutex_);
        if (const auto found = recordings_.find(recording); found != recordings_.end())
        {
            auto& value = found->second;
            const auto previousCompletion = value.completion;
            value.completion = value.accepted
                ? (value.completion == 0 || completion.value == 0 ? 0 : (std::max)(value.completion, completion.value))
                : completion.value;
            if (!value.accepted || value.completion != previousCompletion)
                value.confirmed = false;
            value.submitted = value.accepted = true;
        }
    }
    void OnUploadCompleted(uint64_t completed) override
    {
        if (!completed || completed == UINT64_MAX)
            return; // Device loss is not proof that the upload initialized its bytes.
        decltype(recordings_) retired;
        BaselineMap retiredBaselines;
        {
            std::lock_guard lock(mutex_);
            completed_ = (std::max)(completed_, completed);
            for (auto it = recordings_.begin(); it != recordings_.end();)
            {
                auto& recording = it->second;
                if (!recording.accepted || !recording.confirmed || !recording.completion || recording.completion > completed_)
                {
                    ++it;
                    continue;
                }
                for (auto& prepared : recording.versions)
                {
                    const auto found = baselines_.find(prepared.key);
                    if (found == baselines_.end() ||
                        !prepared.version->copyRecorded.load(std::memory_order_acquire) ||
                        prepared.version->bytes.size() > kMaxCachedBytes ||
                        (found->second.version && found->second.version->serial >= prepared.version->serial))
                        continue;
                    if (found->second.version)
                        cachedBytes_ -= found->second.version->bytes.size();
                    cachedBytes_ += prepared.version->bytes.size();
                    // The retiring recording's already allocated slot takes
                    // the previous owner. No last release occurs under mutex_.
                    found->second.version.swap(prepared.version);
                }
                retired.insert(recordings_.extract(it++));
            }
            while (cachedBytes_ > kMaxCachedBytes)
                EraseOldestBaseline(retiredBaselines);
        }
    }
    void OnUploadAborted(uint64_t recording) override
    {
        decltype(recordings_)::node_type retired;
        {
            std::lock_guard lock(mutex_);
            const auto found = recordings_.find(recording);
            // Once submission was reserved, only explicit rejection proves that
            // no native execution can still reference this version.
            if (found != recordings_.end() && !found->second.submitted && !found->second.accepted)
                retired = recordings_.extract(found);
        }
    }
    void OnUploadSubmissionRejected(uint64_t recording, RHICompletionPoint completion) override
    {
        decltype(recordings_)::node_type retired;
        {
            std::lock_guard lock(mutex_);
            const auto found = recordings_.find(recording);
            if (found != recordings_.end() && !found->second.accepted &&
                (!found->second.submitted || found->second.completion == completion.value))
                retired = recordings_.extract(found);
        }
    }
    IRenderDeviceServices* device_{};
    own::shared_owner<TransientBufferPool> pool_;
    std::mutex mutex_;
    BaselineMap baselines_;
    std::map<uint64_t, Recording> recordings_;
    uint64_t serial_{}, completed_{}, cachedBytes_{};
};
} // namespace material_graph
