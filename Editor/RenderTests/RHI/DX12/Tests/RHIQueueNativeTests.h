#pragma once
#include "RHIQueueContractTests.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include <chrono>
#include <thread>
#include <stdexcept>

bool DX12Test::RunQueueNativeTest(std::string& outLog)
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
        std::shared_ptr<IRHICommandQueue> graphics, compute, copy;
        require(!device.CreateQueue(RHIQueueKind::Graphics, graphics, error) && !graphics,
            "Uninitialized device admitted queue creation.");
        require(device.Initialize(32, 32, error), error);
        require(device.QueryQueueCapabilities().graphics && device.QueryQueueCapabilities().compute &&
            device.QueryQueueCapabilities().copy && device.QueryQueueCapabilities().crossQueueTimeline,
            "Initialized queue services were unavailable.");
        require(device.CreateQueue(RHIQueueKind::Graphics, graphics, error), error);
        require(device.CreateQueue(RHIQueueKind::Compute, compute, error), error);
        require(device.CreateQueue(RHIQueueKind::Copy, copy, error), error);
        require(graphics->GetIdentity().queueId != compute->GetIdentity().queueId &&
            compute->GetIdentity().queueId != copy->GetIdentity().queueId,
            "Native queue identity collision.");
        std::shared_ptr<IRHICommandQueue> invalid = graphics;
        require(!device.CreateQueue(static_cast<RHIQueueKind>(255), invalid, error) && !invalid,
            "Invalid queue kind was accepted or output was not cleared.");
        RHITimelinePoint source, intermediate, finished, rejected;
        require(graphics->Signal(7, source, error), error);
        rejected = source;
        require(!graphics->Signal(7, rejected, error) && !rejected.fence &&
            source.fence->GetLastIssuedValue() == 7, "Repeated signal was published.");
        require(!graphics->Signal(0, rejected, error) && !graphics->Signal(UINT64_MAX, rejected, error) &&
            !graphics->Signal(6, rejected, error), "Reserved or decreasing signal was accepted.");
        require(compute->Wait(source, error) && compute->Signal(3, intermediate, error), error);
        require(copy->Wait(intermediate, error) && copy->Signal(1, finished, error), error);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!finished.IsComplete() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(source.IsComplete() && intermediate.IsComplete() && finished.IsComplete(),
            "Native graphics-compute-copy timeline chain did not complete.");
        require(!copy->Wait({source.fence, 8}, error), "Unissued native value admitted a wait.");
        auto forged = std::make_shared<QueueContractTestFence>();
        forged->producer = graphics->GetIdentity();
        require(!copy->Wait({forged, 7}, error), "Non-native timeline was admitted by DX12.");
        DX12QueueService foreign(device.GetDevice());
        std::shared_ptr<IRHICommandQueue> foreignQueue;
        require(foreign.CreateQueue(RHIQueueKind::Copy, foreignQueue, error) &&
            foreignQueue->GetIdentity().deviceGeneration != graphics->GetIdentity().deviceGeneration &&
            !foreignQueue->Wait(source, error), "Foreign incarnation admitted a native wait.");
        require(foreign.Shutdown(error), error);
        // A final queued wait has no subsequent public signal: teardown must still cover it.
        require(copy->Wait(source, error), error);
        std::string validation;
        const auto errors = device.DrainDebugMessages(validation);
        require(device.HasDebugMessageQueue() && errors == 0, validation);
        device.Shutdown();
        require(!source.IsComplete() && source.fence->QueryCompletion().status == RHITimelineStatus::Unavailable &&
            !graphics->Signal(8, rejected, error) && !compute->Wait(source, error) &&
            !device.QueryQueueCapabilities().graphics, "Shutdown failed to revoke native queue/timeline admission.");
        require(device.Initialize(32, 32, error), error);
        std::shared_ptr<IRHICommandQueue> restarted;
        require(device.CreateQueue(RHIQueueKind::Graphics, restarted, error) &&
            restarted->GetIdentity().deviceGeneration != graphics->GetIdentity().deviceGeneration &&
            !restarted->Wait(source, error), "Restart reused an incarnation or accepted a retired point.");
        require(device.DrainDebugMessages(validation) == 0, validation);
        device.Shutdown();

        // Real removal on a separate WARP device; never remove the Editor's device.
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<IDXGIAdapter> warp;
        Microsoft::WRL::ComPtr<ID3D12Device> nativeDevice;
        Microsoft::WRL::ComPtr<ID3D12Device5> removable;
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) &&
            SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&nativeDevice))) &&
            SUCCEEDED(nativeDevice.As(&removable)), "WARP removal fixture creation failed.");
        DX12QueueService lost(nativeDevice.Get());
        std::shared_ptr<IRHICommandQueue> lostQueue;
        RHITimelinePoint lostPoint;
        require(lost.CreateQueue(RHIQueueKind::Copy, lostQueue, error) &&
            lostQueue->Signal(1, lostPoint, error), error);
        removable->RemoveDevice();
        require(lostPoint.fence->QueryCompletion().status == RHITimelineStatus::DeviceLost &&
            !lostPoint.IsComplete() && !lostQueue->Signal(2, rejected, error) && !rejected.fence &&
            !lostQueue->Wait(lostPoint, error) && !lost.QueryQueueCapabilities().copy,
            "Device loss was published as native completion or accepted work.");
        require(lost.Shutdown(error), error);
        outLog += "Q0_QUEUE_NATIVE_OK checks=" + std::to_string(checks) +
            " validationErrors=0 scope=queue-primitives payloadReadback=not-tested\n";
        return true;
    }
    catch (const std::exception& exception)
    {
        outLog += "Q0_QUEUE_NATIVE_FAILED " + std::string(exception.what()) + "\n";
        return false;
    }
}
