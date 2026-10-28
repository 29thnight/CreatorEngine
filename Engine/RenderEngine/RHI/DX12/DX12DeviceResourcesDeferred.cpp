#include "../../../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.ComSupport.h"
#else
import winrt_base;
#endif
#include "DX12DeviceResources.h"
#include "DX12Encoder.h"
#include "../RHIBackendExecution.h"
#include "DX12SoftwareCommandValidator.h"
#include "../RHISoftwareRecorder.h"
#include "../../../EngineDiagnostics/ProfileScope.h"
#include "../../../EngineDiagnostics/DxCaptureSubmission.h"

struct DX12DeviceResources::PendingViews
{
    enum class Kind
    {
        Shader,
        Unordered,
        RenderTarget,
        Depth,
        Copy
    };
    struct View
    {
        Kind kind{};
        winrt::com_ptr<ID3D12Resource> resource;
        winrt::com_ptr<ID3D12Resource> counter;
        D3D12_CPU_DESCRIPTOR_HANDLE destination{};
        D3D12_CPU_DESCRIPTOR_HANDLE source{};
        D3D12_SHADER_RESOURCE_VIEW_DESC shader{};
        D3D12_UNORDERED_ACCESS_VIEW_DESC unordered{};
        D3D12_RENDER_TARGET_VIEW_DESC target{};
        D3D12_DEPTH_STENCIL_VIEW_DESC depth{};
        D3D12_DESCRIPTOR_HEAP_TYPE heap{};
        UINT count{};
        bool described{};
    };
    ce::render_vector<View> views{65536};
    std::atomic<uint32_t> used{};
    View& Reserve()
    {
        const auto index = used.fetch_add(1, std::memory_order_relaxed);
        if (index >= views.size())
        {
            throw std::length_error("Pending descriptor capacity exceeded.");
        }
        return views[index];
    }
};

void DX12DeviceResources::FlushPendingViews()
{
    if (!m_pendingViews)
    {
        return;
    }
    if (!GetRHISubmissionThread().IsCurrentThread())
    {
        throw std::logic_error("Pending descriptors must be consumed on the RHI thread after writers join.");
    }
    const auto count = m_pendingViews->used.load(std::memory_order_acquire);
    if (count > m_pendingViews->views.size())
    {
        throw std::length_error("Pending descriptor capacity exceeded.");
    }
    for (uint32_t index = 0; index < count; ++index)
    {
        auto& view = m_pendingViews->views[index];
        switch (view.kind)
        {
        case PendingViews::Kind::Shader:
            m_device->CreateShaderResourceView(view.resource.get(), view.described ? &view.shader : nullptr,
                                               view.destination);
            break;
        case PendingViews::Kind::Unordered:
            m_device->CreateUnorderedAccessView(view.resource.get(), view.counter.get(),
                                                view.described ? &view.unordered : nullptr, view.destination);
            break;
        case PendingViews::Kind::RenderTarget:
            m_device->CreateRenderTargetView(view.resource.get(), view.described ? &view.target : nullptr,
                                             view.destination);
            break;
        case PendingViews::Kind::Depth:
            m_device->CreateDepthStencilView(view.resource.get(), view.described ? &view.depth : nullptr,
                                             view.destination);
            break;
        case PendingViews::Kind::Copy:
            m_device->CopyDescriptorsSimple(view.count, view.destination, view.source, view.heap);
            break;
        }
        view = {};
    }
    m_pendingViews->used.store(0, std::memory_order_release);
}

struct DX12DeviceResources::DeferredNativeFrame
{
    winrt::com_ptr<ID3D12CommandAllocator> allocator;
    ce::render_vector<winrt::com_ptr<ID3D12GraphicsCommandList>> lists;
    size_t used{};
    bool reset{};
};

struct DX12DeviceResources::DeferredImmediate
{
    explicit DeferredImmediate(DX12DeviceResources& resources) : validator(resources), recorder(validator) {}
    DX12SoftwareCommandValidator validator;
    RHISoftwareRecorder recorder;
    own::shared_owner<DeferredNativeFrame> frame;
    struct Bindings
    {
        own::shared_owner<const DX12RecordingResources> resources;
        bool needed{};
    };
    own::shared_owner<Bindings> bindings;
    bool recording{};
};

void DX12DeviceResources::SetDeferredImmediateRecording(bool enabled)
{
    if (m_currentRecordingId)
    {
        throw std::logic_error("Immediate recording mode changes only at frame admission.");
    }
    m_deferredImmediate = enabled;
}

void DX12DeviceResources::BeginDeferredImmediate()
{
    if (!m_deferred)
    {
        m_deferred = own::make_shared<DeferredImmediate>(*this);
    }
    if (!m_pendingViews)
    {
        m_pendingViews = own::make_shared<PendingViews>();
    }
    auto& frame = m_deferredFrames[m_frameIndex];
    if (!frame)
    {
        frame = own::make_shared<DeferredNativeFrame>();
        frame->allocator = m_allocators[m_frameIndex];
    }
    // BeginFrame has joined the slot's CPU ticket and GPU fence. Only its RHI
    // owner changes native objects until that slot is admitted again.
    frame->used = 0;
    frame->reset = false;
    m_deferred->frame = frame;
    BeginPendingImmediate();
}

void DX12DeviceResources::BeginPendingImmediate()
{
    m_pendingCommands = own::make_shared<DX12PendingCommands>(m_pendingCommandPool);
    m_deferred->bindings = own::make_shared<DeferredImmediate::Bindings>();
}

RHIEncoder& DX12DeviceResources::GetDeferredImmediateEncoder()
{
    if (!m_pendingCommands || !m_currentRecordingId)
    {
        throw std::logic_error("Software immediate commands require an admitted recording.");
    }
    if (!m_deferred->recording)
    {
        own::shared_owner<IRHICommandQueue> queue;
        std::string error;
        if (!GetPrimaryGraphicsQueue(queue, error) ||
            !m_deferred->recorder.BeginRecording({queue->GetIdentity(), 1, 64ull << 20, 64ull << 20}, error))
        {
            throw std::runtime_error(error);
        }
        m_deferred->recording = true;
    }
    return m_deferred->recorder.AcquireEncoder(0);
}

bool DX12DeviceResources::SealImmediateSoftware(std::string& error)
{
    if (!m_deferred || !m_deferred->recording)
    {
        return true;
    }
    own::shared_owner<IRHICommandQueue> queue;
    if (!GetPrimaryGraphicsQueue(queue, error))
    {
        return false;
    }
    RHISoftwareBatchDesc desc;
    desc.queue = queue->GetIdentity();
    desc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(this);
    desc.uploadRecordingId = m_currentRecordingId;
    desc.descriptorVersion = GetDescriptorVersionToken();
    desc.lifetimeToken = m_deferred->bindings;
    RHISoftwareBatch batch;
    const ce::fixed_array<uint32_t, 1> order{0};
    if (!m_deferred->recorder.FinishRecording(order, desc, batch, error))
    {
        return false;
    }
    m_deferred->recording = false;
    m_deferred->bindings->needed = true;
    m_pendingCommands->Add(
        [this, bindings = m_deferred->bindings, batch = std::move(batch)](ID3D12GraphicsCommandList* list) mutable
        {
            DX12Encoder encoder(list, this);
            encoder.SetRecordingResources(bindings->resources);
            RHICommandFailure failure;
            if (!batch.ReplaySerial(encoder, failure))
            {
                throw std::runtime_error(failure.message);
            }
        });
    return true;
}

bool DX12DeviceResources::SubmitPendingImmediate(bool preserveRecording, bool endFrame, std::string& error)
{
    ce::profile_scope seal{ce::marker<"RHIImmediateSeal">()};
    if (!m_pendingCommands || !m_currentRecordingId || !SealImmediateSoftware(error))
    {
        return false;
    }
    if (m_deferred->bindings->needed && !CaptureRecordingResources(m_deferred->bindings->resources, error))
    {
        return false;
    }
    const auto recording = m_currentRecordingId;
    const auto completion = RHICompletionPoint{m_nextFenceValue++};
    const auto frameSlot = m_frameIndex;
    const auto frame = m_deferred->frame;
    const auto commands = m_pendingCommands;
    const auto bindings = m_deferred->bindings;
    struct Lifetime
    {
        own::shared_owner<DX12PendingCommands> commands;
        own::shared_owner<DeferredNativeFrame> frame;
        own::shared_owner<DeferredImmediate::Bindings> bindings;
    };
    auto lifetime = own::make_shared<Lifetime>(commands, frame, bindings);
    RHIRecordingAdmissionGuard admission(
        [this, recording, completion, preserveRecording]
        {
            if (!preserveRecording)
            {
                RejectNeverEnqueuedRecording(recording, completion);
            }
        });
    if (!preserveRecording)
    {
        m_uploadAllocator.OnSubmitted(recording, completion);
        m_descriptorRecycler.OnSubmitted(recording, completion);
        for (auto* listener : m_uploadTransactionListeners)
        {
            listener->OnUploadSubmitted(recording, completion);
        }
        m_currentRecordingId = 0;
    }
    RHISubmissionTicket ticket;
    if (!GetRHISubmissionThread().Enqueue(
            this, "DX12 pending immediate commands",
            [this, device = m_device, queue = m_queue, fence = m_fence, frame, commands,
             completion](std::string& outError)
            {
                ce::profile_scope execute{ce::marker<"RHIImmediateExecute">()};
                if (!GetRHISubmissionThread().IsCurrentThread())
                {
                    outError = "Pending native commands must execute on the RHI thread.";
                    return false;
                }
                if (!frame->reset)
                {
                    if (FAILED(frame->allocator->Reset()))
                    {
                        outError = "Pending immediate allocator Reset failed.";
                        return false;
                    }
                    frame->reset = true;
                }
                winrt::com_ptr<ID3D12GraphicsCommandList> list;
                if (frame->used < frame->lists.size())
                {
                    list = frame->lists[frame->used];
                    if (FAILED(list->Reset(frame->allocator.get(), nullptr)))
                    {
                        outError = "Pending immediate list Reset failed.";
                        return false;
                    }
                }
                else
                {
                    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frame->allocator.get(),
                                                         nullptr, IID_PPV_ARGS(list.put()))))
                    {
                        outError = "Pending immediate list creation failed.";
                        return false;
                    }
                    frame->lists.push_back(list);
                }
                ++frame->used;
                commands->Execute(list.get());
                if (FAILED(list->Close()))
                {
                    outError = "Pending immediate list Close failed.";
                    return false;
                }
                ID3D12CommandList* lists[]{list.get()};
                RHICommandFailureScope::SetExecutionUncertain(true);
                {
                const ce::dx_capture::submission_scope capture(queue.get(), 1);
                queue->ExecuteCommandLists(1, lists);
            }
                if (FAILED(queue->Signal(fence.get(), completion.value)))
                {
                    outError = "Pending immediate queue Signal failed.";
                    return false;
                }
                return true;
            },
            ticket, error, completion, [fence = m_fence] { return fence->GetCompletedValue(); }, lifetime))
    {
        return false;
    }
    admission.Accept();
    if (!preserveRecording)
    {
        for (auto* listener : m_uploadTransactionListeners)
        {
            listener->OnUploadAccepted(recording, completion);
        }
    }
    m_frameSubmissionTickets[frameSlot] = ticket;
    m_frameFenceValues[frameSlot] = completion.value;
    m_lastAdmittedFenceValue = completion.value;
    m_pendingCommands.reset();
    if (endFrame)
    {
        m_frameIndex = (m_frameIndex + 1) % kFrameCount;
    }
    else
    {
        if (!preserveRecording)
        {
            m_currentRecordingId = m_nextRecordingId++;
            m_uploadAllocator.BeginRecording(m_currentRecordingId);
            if (!m_descriptorRecycler.BeginRecording(m_currentRecordingId, error))
            {
                m_uploadAllocator.AbortRecording(m_currentRecordingId);
                return false;
            }
        }
        BeginPendingImmediate();
    }
    return true;
}

void DX12DeviceResources::AbortPendingImmediate()
{
    if (m_deferred)
    {
        m_deferred->recorder.CancelRecording();
        m_deferred->recording = false;
    }
    m_pendingCommands.reset();
    if (m_pendingViews)
    {
        const auto count = (std::min)(m_pendingViews->used.exchange(0), static_cast<uint32_t>(m_pendingViews->views.size()));
        for (uint32_t index = 0; index < count; ++index)
        {
            m_pendingViews->views[index] = {};
        }
    }
}

void DX12DeviceResources::WriteShaderResourceView(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
                                                  D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
    if (!m_deferredImmediate || RHIBackendExecutionScope::IsActive() || GetRHISubmissionThread().IsCurrentThread())
    {
        m_device->CreateShaderResourceView(resource, desc, destination);
        return;
    }
    auto& view = m_pendingViews->Reserve();
    view.kind = PendingViews::Kind::Shader;
    view.resource.copy_from(resource);
    view.destination = destination;
    view.described = desc != nullptr;
    if (desc)
    {
        view.shader = *desc;
    }
}

void DX12DeviceResources::WriteUnorderedAccessView(ID3D12Resource* resource, ID3D12Resource* counter,
                                                   const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc,
                                                   D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
    if (!m_deferredImmediate || RHIBackendExecutionScope::IsActive() || GetRHISubmissionThread().IsCurrentThread())
    {
        m_device->CreateUnorderedAccessView(resource, counter, desc, destination);
        return;
    }
    auto& view = m_pendingViews->Reserve();
    view.kind = PendingViews::Kind::Unordered;
    view.resource.copy_from(resource);
    view.destination = destination;
    view.described = desc != nullptr;
    if (desc)
    {
        view.unordered = *desc;
    }
    view.counter.copy_from(counter);
}

void DX12DeviceResources::WriteRenderTargetView(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
    if (!m_deferredImmediate || RHIBackendExecutionScope::IsActive() || GetRHISubmissionThread().IsCurrentThread())
    {
        m_device->CreateRenderTargetView(resource, desc, destination);
        return;
    }
    auto& view = m_pendingViews->Reserve();
    view.kind = PendingViews::Kind::RenderTarget;
    view.resource.copy_from(resource);
    view.destination = destination;
    view.described = desc != nullptr;
    if (desc)
    {
        view.target = *desc;
    }
}

void DX12DeviceResources::WriteDepthStencilView(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
                                                D3D12_CPU_DESCRIPTOR_HANDLE destination)
{
    if (!m_deferredImmediate || RHIBackendExecutionScope::IsActive() || GetRHISubmissionThread().IsCurrentThread())
    {
        m_device->CreateDepthStencilView(resource, desc, destination);
        return;
    }
    auto& view = m_pendingViews->Reserve();
    view.kind = PendingViews::Kind::Depth;
    view.resource.copy_from(resource);
    view.destination = destination;
    view.described = desc != nullptr;
    if (desc)
    {
        view.depth = *desc;
    }
}

void DX12DeviceResources::CopyNativeDescriptors(UINT count, D3D12_CPU_DESCRIPTOR_HANDLE destination,
                                                D3D12_CPU_DESCRIPTOR_HANDLE source, D3D12_DESCRIPTOR_HEAP_TYPE heap)
{
    if (!m_deferredImmediate || RHIBackendExecutionScope::IsActive() || GetRHISubmissionThread().IsCurrentThread())
    {
        m_device->CopyDescriptorsSimple(count, destination, source, heap);
        return;
    }
    auto& view = m_pendingViews->Reserve();
    view.kind = PendingViews::Kind::Copy;
    view.count = count;
    view.destination = destination;
    view.source = source;
    view.heap = heap;
}
