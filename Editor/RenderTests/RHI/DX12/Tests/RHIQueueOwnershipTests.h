#pragma once
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12QueueBatchAdapter.h"
#include <chrono>
#include <thread>
#include <stdexcept>
#include <cstring>

bool DX12Test::RunQueueOwnershipTest(std::string& outLog)
{
    uint32_t checks = 0;
    const auto require = [&](bool accepted, const std::string& message)
    {
        ++checks;
        if (!accepted)
        {
            throw std::runtime_error(message);
        }
    };
    try
    {
        DX12DeviceResources device;
        std::string error;
        require(device.Initialize(32, 32, error), error);
        DX12QueueService service(device.GetDevice());
        std::shared_ptr<IRHICommandQueue> producer, consumer, unrelated;
        require(service.CreateQueue(RHIQueueKind::Graphics, producer, error) &&
            service.CreateQueue(RHIQueueKind::Copy, consumer, error) &&
            service.CreateQueue(RHIQueueKind::Compute, unrelated, error), error);
        constexpr uint64_t bytes = 16384;
        const auto buffer = [&](D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES state,
            ID3D12Device* nativeDevice = nullptr)
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heapType;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = bytes;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            require(SUCCEEDED((nativeDevice ? nativeDevice : device.GetDevice())->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, state, nullptr, IID_PPV_ARGS(&resource))), "Buffer creation failed.");
            return resource;
        };
        auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto shared = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
        auto readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        void* mapped = nullptr;
        D3D12_RANGE noRead{0, 0};
        require(SUCCEEDED(upload->Map(0, &noRead, &mapped)), "Upload map failed.");
        for (uint32_t index = 0; index < bytes / sizeof(uint32_t); ++index)
        {
            static_cast<uint32_t*>(mapped)[index] = (index * 1664525u) ^ 0xA73B59D1u;
        }
        upload->Unmap(0, nullptr);
        Microsoft::WRL::ComPtr<ID3D12Fence> gate;
        require(SUCCEEDED(device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(&gate))), "Gate fence creation failed.");
        struct GateRelease
        {
            ID3D12Fence* fence;
            ~GateRelease() { fence->Signal(1); }
        } release{gate.Get()}; // Opens on exceptions before service teardown.
        require(service.EnqueueTestGate(producer, gate.Get(), 1, error), error);
        auto producerOwner = std::make_shared<int>(1);
        auto consumerOwner = std::make_shared<int>(2);
        std::weak_ptr<int> producerWeak = producerOwner, consumerWeak = consumerOwner;
        std::shared_ptr<IRHIQueueCommandBatch> produced, consumed;
        require(service.RecordBufferCopy(producer, upload.Get(), shared.Get(), bytes,
            produced, error, producerOwner) &&
            service.RecordBufferCopy(consumer, shared.Get(), readback.Get(), bytes,
            consumed, error, consumerOwner), error);
        require(produced->GetQueueIdentity() == producer->GetIdentity() &&
            consumed->GetQueueIdentity() == consumer->GetIdentity() &&
            produced->GetRecordingId() != consumed->GetRecordingId(), "Recording identity collision.");
        RHITimelinePoint ready, done, rejected, otherDone;
        std::shared_ptr<IRHICommandQueue> foreignQueue;
        std::shared_ptr<IRHIQueueCommandBatch> foreignBatch;
        require(device.CreateQueue(RHIQueueKind::Copy, foreignQueue, error) &&
            DX12QueueService::RecordBufferCopy(foreignQueue, upload.Get(), readback.Get(), bytes,
                foreignBatch, error) && !consumer->Submit(foreignBatch, 1, rejected, error) &&
            !rejected.fence, "Device factory adapter or foreign recording rejection failed.");
        struct PinnedRecording
        {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
        };
        auto pin = std::make_shared<PinnedRecording>();
        std::weak_ptr<PinnedRecording> pinWeak = pin;
        require(SUCCEEDED(device.GetDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
            IID_PPV_ARGS(&pin->allocator))) &&
            SUCCEEDED(device.GetDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY,
                pin->allocator.Get(), nullptr, IID_PPV_ARGS(&pin->list))) &&
            SUCCEEDED(pin->list->Close()), "Existing encoder recording fixture failed.");
        ID3D12CommandList* nativeLists[]{pin->list.Get()};
        std::shared_ptr<IRHIQueueCommandBatch> sealed;
        require(!DX12SealQueueRecording(foreignQueue, nativeLists, {}, sealed, error) && !sealed &&
            !DX12SealQueueRecording(unrelated, nativeLists, pin, sealed, error) && !sealed,
            "Missing storage pin or incompatible native queue kind was accepted.");
        RHITimelinePoint adapterDone;
        require(DX12SealQueueRecording(foreignQueue, nativeLists, pin, sealed, error) &&
            foreignQueue->Submit(sealed, 1, adapterDone, error), error);
        pin.reset();
        sealed.reset();
        const auto adapterDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!adapterDone.IsComplete() && std::chrono::steady_clock::now() < adapterDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(adapterDone.IsComplete() && foreignQueue->CollectCompleted() == 1 && pinWeak.expired(),
            "Device factory's existing recording storage was not retired after completion.");
        require(!consumer->Submit(produced, 1, rejected, error) && !rejected.fence,
            "Wrong queue admitted the producer recording.");
        service.RejectNextTestSubmission(false);
        require(!producer->Submit(produced, 1, rejected, error) && !rejected.fence &&
            producer->GetPendingBatchCount() == 0, "Pre-execution rejection retained or executed a batch.");
        require(producer->Submit(produced, 1, ready, error), error);
        require(!producer->Submit(produced, 2, rejected, error) && !rejected.fence,
            "Duplicate recording was submitted twice.");
        const RHIQueueHandoff handoff{producer->GetIdentity(), consumer->GetIdentity(), ready};
        require(ValidateQueueHandoff(handoff, error) && consumer->Wait(handoff.readyAfter, error) &&
            consumer->Submit(consumed, 1, done, error), error);
        require(unrelated->Signal(1000, otherDone, error), error);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!otherDone.IsComplete() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        producerOwner.reset();
        consumerOwner.reset();
        produced.reset();
        consumed.reset();
        upload.Reset();
        shared.Reset();
        require(otherDone.IsComplete() && !ready.IsComplete() && !done.IsComplete() &&
            producer->CollectCompleted() == 0 && consumer->CollectCompleted() == 0 &&
            !producerWeak.expired() && !consumerWeak.expired(),
            "Unrelated fence or pending signal caused early GPU resource retirement.");
        require(SUCCEEDED(gate->Signal(1)), "Gate release failed.");
        const auto completionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done.IsComplete() && std::chrono::steady_clock::now() < completionDeadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(done.IsComplete() && ready.IsComplete(), "Delayed producer/consumer did not complete.");
        D3D12_RANGE readRange{0, bytes};
        require(SUCCEEDED(readback->Map(0, &readRange, &mapped)), "Readback map failed.");
        bool matches = true;
        for (uint32_t index = 0; index < bytes / sizeof(uint32_t); ++index)
        {
            matches &= static_cast<const uint32_t*>(mapped)[index] == ((index * 1664525u) ^ 0xA73B59D1u);
        }
        readback->Unmap(0, &noRead);
        require(matches, "Cross-queue readback payload mismatch.");
        require(producer->CollectCompleted() == 1 && consumer->CollectCompleted() == 1 &&
            producerWeak.expired() && consumerWeak.expired(), "Completed queue-specific owners were not retired.");

        auto failedUpload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        auto failedReadback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        auto failedOwner = std::make_shared<int>(3);
        std::weak_ptr<int> failedWeak = failedOwner;
        std::shared_ptr<IRHIQueueCommandBatch> failed;
        std::shared_ptr<IRHIQueueCommandBatch> retired;
        require(service.RecordBufferCopy(consumer, failedUpload.Get(), failedReadback.Get(), bytes,
            retired, error), error);
        require(service.RecordBufferCopy(producer, failedUpload.Get(), failedReadback.Get(), bytes,
            failed, error, failedOwner), error);
        service.RejectNextTestSubmission(true);
        require(!producer->Submit(failed, 2, rejected, error) && !rejected.fence &&
            ready.fence->GetLastIssuedValue() == 1, "Unfenced execution published a point.");
        failedOwner.reset();
        failed.reset();
        failedUpload.Reset();
        failedReadback.Reset();
        require(producer->CollectCompleted() == 0 && producer->GetPendingBatchCount() == 1 &&
            !failedWeak.expired() && !producer->Signal(2, rejected, error),
            "Unfenced failure was collected or faulted queue admitted more work.");
        require(service.Shutdown(error) && failedWeak.expired() && producer->GetPendingBatchCount() == 0,
            "Verified teardown did not release quarantined ownership.");
        require(!consumer->Submit(retired, 2, rejected, error) && !rejected.fence,
            "Retired queue admitted submission.");
        std::string validation;
        require(device.HasDebugMessageQueue() && device.DrainDebugMessages(validation) == 0, validation);
        device.Shutdown();

        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        Microsoft::WRL::ComPtr<ID3D12Device> warpDevice;
        Microsoft::WRL::ComPtr<ID3D12Device5> removable;
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
            SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&warpDevice))) &&
            SUCCEEDED(warpDevice.As(&removable)), "WARP ownership fixture creation failed.");
        DX12QueueService lost(warpDevice.Get());
        std::shared_ptr<IRHICommandQueue> lostQueue;
        auto lostUpload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, warpDevice.Get());
        auto lostReadback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, warpDevice.Get());
        Microsoft::WRL::ComPtr<ID3D12Fence> lostGate;
        auto lostOwner = std::make_shared<int>(4);
        std::weak_ptr<int> lostWeak = lostOwner;
        std::shared_ptr<IRHIQueueCommandBatch> lostBatch;
        RHITimelinePoint lostPoint;
        require(SUCCEEDED(warpDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&lostGate))),
            "WARP gate creation failed.");
        GateRelease lostRelease{lostGate.Get()};
        require(lost.CreateQueue(RHIQueueKind::Copy, lostQueue, error) &&
            lost.EnqueueTestGate(lostQueue, lostGate.Get(), 1, error) &&
            lost.RecordBufferCopy(lostQueue, lostUpload.Get(), lostReadback.Get(), bytes,
                lostBatch, error, lostOwner) && lostQueue->Submit(lostBatch, 1, lostPoint, error), error);
        lostOwner.reset();
        lostBatch.reset();
        lostUpload.Reset();
        lostReadback.Reset();
        removable->RemoveDevice();
        require(lostPoint.fence->QueryCompletion().status == RHITimelineStatus::DeviceLost &&
            !lostPoint.IsComplete() && lostQueue->CollectCompleted() == 0 && !lostWeak.expired(),
            "Removed device was treated as completed resource ownership.");
        require(lost.Shutdown(error) && lostWeak.expired() && lostQueue->GetPendingBatchCount() == 0,
            "Device-loss teardown did not release pending ownership.");
        outLog += "Q0_QUEUE_OWNERSHIP_OK checks=" + std::to_string(checks) +
            " bytes=16384 payloadError=0 validationErrors=0 delayedProducer=true quarantineReleased=true\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog += "Q0_QUEUE_OWNERSHIP_FAILED " + std::string(exception.what()) + "\n";
        return false;
    }
}
