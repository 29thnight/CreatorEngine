#pragma once

#include "../../RHI/IRenderDeviceServices.h"
#include <algorithm>
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

    void WatchRecording(std::function<void()> rejected, std::function<void()> accepted = {})
    {
        watches_.push_back({device_->GetCurrentUploadRecordingId(), std::move(rejected), std::move(accepted)});
    }

    void Retire(RHITextureHandle texture)
    {
        if (texture.IsValid())
        {
            const auto recording = device_->GetCurrentUploadRecordingId();
            retired_.push_back({texture, {}, recording == abortedRecording_ ? 0 : recording, submitted_});
        }
        Collect();
    }

    void Retire(RHIBufferHandle buffer)
    {
        if (buffer.IsValid())
        {
            const auto recording = device_->GetCurrentUploadRecordingId();
            retired_.push_back({{}, buffer, recording == abortedRecording_ ? 0 : recording, submitted_});
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
        abortedRecording_ = 0;
    }

private:
    struct Retired
    {
        RHITextureHandle texture;
        RHIBufferHandle buffer;
        std::uint64_t recording{}, completion{};
    };

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

    void Collect()
    {
        std::erase_if(retired_, [&](const Retired& item)
        {
            if (item.recording || item.completion > completed_)
            {
                return false;
            }
            Release(item);
            return true;
        });
    }

    void OnUploadSubmitted(std::uint64_t, RHICompletionPoint) override {}

    void OnUploadAccepted(std::uint64_t recording, RHICompletionPoint completion) override
    {
        submitted_ = (std::max)(submitted_, completion.value);
        for (auto& item : retired_)
        {
            if (item.recording == recording)
            {
                item.recording = 0;
                item.completion = (std::max)(item.completion, completion.value);
            }
        }
        std::erase_if(watches_, [recording](Watch& watch)
        {
            if (watch.recording != recording)
            {
                return false;
            }
            if (watch.accepted)
            {
                watch.accepted();
            }
            return true;
        });
    }

    void OnUploadCompleted(std::uint64_t completed) override
    {
        completed_ = (std::max)(completed_, completed);
        Collect();
    }

    void OnUploadAborted(std::uint64_t recording) override
    {
        abortedRecording_ = recording;
        std::erase_if(watches_, [recording](Watch& watch)
        {
            if (watch.recording != recording)
            {
                return false;
            }
            if (watch.rejected)
            {
                watch.rejected();
            }
            return true;
        });
        for (auto& item : retired_)
        {
            if (item.recording == recording)
            {
                item.recording = 0;
            }
        }
        Collect();
    }

    void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint) override
    {
        OnUploadAborted(recording);
    }

    IRenderDeviceServices* device_{};
    std::vector<Retired> retired_;
    std::uint64_t submitted_{}, completed_{};
    std::uint64_t abortedRecording_{};
    struct Watch
    {
        std::uint64_t recording;
        std::function<void()> rejected, accepted;
    };
    std::vector<Watch> watches_;
};
