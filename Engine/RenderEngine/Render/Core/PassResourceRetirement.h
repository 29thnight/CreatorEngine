#pragma once

#include "../../RHI/IRenderDeviceServices.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <vector>

// Pass-owned resources are retired against submission notifications. A resource
// replaced during an open recording also waits for that recording's decision.
class PassResourceRetirement final : private IRHIUploadTransactionListener
{
public:
    PassResourceRetirement() = default;
    PassResourceRetirement(const PassResourceRetirement&) = delete;
    PassResourceRetirement& operator=(const PassResourceRetirement&) = delete;
    ~PassResourceRetirement()
    {
        if (device_)
        {
            device_->UnregisterUploadTransactionListener(this);
        }
    }

    bool Attach(IRenderDeviceServices& device)
    {
        if (device_ && device_ != &device)
        {
            return false;
        }
        if (!device_)
        {
            device_ = &device;
            device_->RegisterUploadTransactionListener(this);
        }
        return true;
    }

    IRenderDeviceServices* Device() const { return device_; }
    std::size_t PendingCount() const { return retired_.size(); }

    // Reserve before recording/replacing resources, never after queue admission.
    void ReserveRetirements(std::size_t additional)
    {
        retired_.reserve(retired_.size() + additional);
    }

    // The caller has already observed acceptance, possibly after this listener's
    // notification. Do not associate these handles with the new/current recording.
    // ReserveRetirements must cover every handle transferred by this operation.
    void RetireAccepted(RHITextureHandle texture, RHICompletionPoint completion) noexcept
    {
        if (texture.IsValid())
        {
            assert(retired_.size() < retired_.capacity());
            retired_.push_back({texture, {}, 0, completion.value,
                submittedWithoutCompletion_ || !completion.IsValid()});
        }
    }

    void WatchRecording(std::function<void()> rejected, std::function<void()> accepted = {})
    {
        watches_.push_back({device_->GetCurrentUploadRecordingId(), std::move(rejected), std::move(accepted)});
    }

    void Retire(RHITextureHandle texture)
    {
        if (texture.IsValid())
        {
            const auto recording = device_->GetCurrentUploadRecordingId();
            retired_.push_back({texture, {}, PendingRecording(recording), submitted_, submittedWithoutCompletion_});
        }
        Collect();
    }

    void Retire(RHIBufferHandle buffer)
    {
        if (buffer.IsValid())
        {
            const auto recording = device_->GetCurrentUploadRecordingId();
            retired_.push_back({{}, buffer, PendingRecording(recording), submitted_, submittedWithoutCompletion_});
        }
        Collect();
    }

    // The owning pipeline must establish GPU idle before pass shutdown.
    void ClearAfterIdle()
    {
        for (const auto& item : retired_)
        {
            Release(item);
        }
        retired_.clear();
        watches_.clear();
        if (device_)
        {
            device_->UnregisterUploadTransactionListener(this);
        }
        device_ = nullptr;
        submitted_ = completed_ = 0;
        abortedRecording_ = acceptedRecording_ = 0;
        submittedWithoutCompletion_ = false;
    }

private:
    struct Retired
    {
        RHITextureHandle texture;
        RHIBufferHandle buffer;
        std::uint64_t recording{}, completion{};
        bool waitForIdle{};
    };

    std::uint64_t PendingRecording(std::uint64_t recording) const
    {
        return recording == abortedRecording_ || recording == acceptedRecording_ ? 0 : recording;
    }

    void Release(const Retired& item)
    {
        if (item.texture.IsValid())
        {
            device_->ReleaseTexture(item.texture);
        }
        if (item.buffer.IsValid())
        {
            device_->ReleaseBuffer(item.buffer);
        }
    }

    void Collect() noexcept
    {
        std::erase_if(retired_, [&](const Retired& item)
        {
            if (item.recording || item.waitForIdle || item.completion > completed_)
            {
                return false;
            }
            try
            {
                Release(item);
                return true;
            }
            catch (...)
            {
                // Backend free-list growth can throw after releasing a native
                // slot. Retain the generational handle for an idempotent retry;
                // never interrupt a rejection/completion broadcast mid-compaction.
                return false;
            }
        });
    }

    void OnUploadSubmitted(std::uint64_t, RHICompletionPoint) override {}

    void OnUploadAccepted(std::uint64_t recording, RHICompletionPoint completion) noexcept override
    {
        acceptedRecording_ = recording;
        submitted_ = (std::max)(submitted_, completion.value);
        submittedWithoutCompletion_ |= !completion.IsValid();
        for (auto& item : retired_)
        {
            if (item.recording == recording)
            {
                item.recording = 0;
                item.completion = (std::max)(item.completion, completion.value);
                item.waitForIdle |= !completion.IsValid();
            }
        }
        ResolveWatches(recording, true);
    }

    void OnUploadCompleted(std::uint64_t completed) noexcept override
    {
        completed_ = (std::max)(completed_, completed);
        Collect();
    }

    void OnUploadAborted(std::uint64_t recording) noexcept override
    {
        // An accepted submission (including completion 0) is never rollback work.
        if (!recording || recording == acceptedRecording_)
        {
            return;
        }
        abortedRecording_ = recording;
        for (auto& item : retired_)
        {
            if (item.recording == recording)
            {
                item.recording = 0;
            }
        }
        ResolveWatches(recording, false);
        Collect();
    }

    void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint) noexcept override
    {
        OnUploadAborted(recording);
    }

    IRenderDeviceServices* device_{};
    std::vector<Retired> retired_;
    std::uint64_t submitted_{}, completed_{};
    std::uint64_t abortedRecording_{}, acceptedRecording_{};
    bool submittedWithoutCompletion_{};
    struct Watch
    {
        std::uint64_t recording;
        std::function<void()> rejected, accepted;
    };
    std::vector<Watch> watches_;

    void ResolveWatches(std::uint64_t recording, bool accepted) noexcept
    {
        for (;;)
        {
            const auto found = std::find_if(watches_.begin(), watches_.end(),
                [recording](const Watch& watch) { return watch.recording == recording; });
            if (found == watches_.end())
            {
                return;
            }
            // Consume both decisions before user code runs. A callback may append
            // watches or throw; neither may invalidate iteration or stop the device
            // from delivering the already-final submission decision to other listeners.
            auto watch = std::move(*found);
            watches_.erase(found);
            auto& callback = accepted ? watch.accepted : watch.rejected;
            if (callback)
            {
                try { callback(); }
                catch (...) {}
            }
        }
    }
};
