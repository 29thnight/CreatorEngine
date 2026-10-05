#include "PlayerPresentation.h"

#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/RHIShaderCompiler.h"

#include <array>
#include <cstdio>
#include <limits>
#include <wrl/client.h>

namespace Player
{
    namespace
    {
        template<typename T>
        using ComPtr = Microsoft::WRL::ComPtr<T>;

        class DX12Presentation final : public Presentation
        {
        public:
            ~DX12Presentation() override { Shutdown(); }

            bool Initialize(HWND window, uint32_t width, uint32_t height, std::string& outError) override
            {
                outError.clear();
                if (IsActive())
                {
                    return true;
                }
                if (m_quarantined)
                {
                    outError = "Player DX12 presentation is quarantined after unproven GPU completion";
                    return false;
                }
                if (nullptr == window || 0 == width || 0 == height)
                {
                    outError = "Player DX12 initialization requires a window with a nonzero client size";
                    return false;
                }
                m_shutdown = false;
                // Reuse the RHI device/queue, submission, admission and swapchain path.
                // Its unused offscreen target need not match the window dimensions.
                if (!m_resources.Initialize(1, 1, outError) ||
                    !m_resources.AttachSwapChain(window, width, height, outError) ||
                    !CreatePipeline(outError))
                {
                    Shutdown();
                    return false;
                }
                m_width = width;
                m_height = height;
                m_suspended = false;
                m_active.store(true, std::memory_order_release);
                return true;
            }

            bool IsActive() const override { return m_active.load(std::memory_order_acquire); }
            const char* GetName() const override { return "dx12"; }
            bool IsQuarantined() const { return m_quarantined; }

            bool Resize(uint32_t width, uint32_t height, std::string& outError) override
            {
                outError.clear();
                if (!IsActive() || m_frameOpen)
                {
                    outError = "Player DX12 resize requires an active presenter between frames";
                    return false;
                }
                m_suspended = width == 0 || height == 0;
                if (m_suspended || (width == m_width && height == m_height))
                {
                    return true;
                }
                // A faulted RHI owner may report a successful CPU abandon. That is
                // not permission to release swapchain resources still used by the GPU.
                if (!m_resources.DrainForLifecycle(RHILifecycleCommand::SwapChainResize, outError))
                {
                    return false;
                }
                if (m_resources.GetLastLifecycleResult().command == RHILifecycleCommand::UnrecoverableDeviceError ||
                    FAILED(m_resources.GetDevice()->GetDeviceRemovedReason()))
                {
                    outError = "Player DX12 resize requires proven GPU completion on a live device";
                    return false;
                }
                if (!m_resources.ResizeSwapChain(width, height, outError))
                {
                    return false;
                }
                CollectDisplayUse();
                m_width = width;
                m_height = height;
                return true;
            }

            bool BeginFrame(std::string& outError) override
            {
                outError.clear();
                if (!IsActive() || m_frameOpen)
                {
                    outError = "Player DX12 presentation is not ready for a new frame";
                    return false;
                }
                if (m_suspended)
                {
                    return true;
                }
                if (!m_resources.BeginFrame(outError))
                {
                    // Occlusion is a deferred frame, not a renderer/device failure.
                    // The existing RHI admission wait is bounded so stop/resize wakes
                    // can still be serviced. No native commands were recorded.
                    if (outError == "DX12 host frame admission timed out")
                    {
                        outError.clear();
                        return true;
                    }
                    return false;
                }
                CollectDisplayUse();
                if (m_submittedUse.lease)
                {
                    m_resources.AbortFrame();
                    outError = "Player DX12 host admission did not prove previous consumer completion";
                    return false;
                }
                m_frameOpen = true;
                m_recordingError.clear();
                return true;
            }

            uint64_t OpenSharedTexture(void* sharedHandle,
                std::shared_ptr<RHIDisplayConsumerLease> consumerLease) override
            {
                if (!m_frameOpen || !sharedHandle || !consumerLease ||
                    consumerLease->m_completionLost.load(std::memory_order_acquire))
                {
                    return 0;
                }
                if (m_pendingUse.lease)
                {
                    if (m_pendingUse.lease == consumerLease)
                    {
                        return 1;
                    }
                    m_recordingError = "Player DX12 frame acquired more than one game image";
                    return 0;
                }

                SharedImage* cached = nullptr;
                for (SharedImage& image : m_sharedImages)
                {
                    // A recycled HANDLE alone is not the producer allocation's identity.
                    if (image.handle == sharedHandle && image.lease.lock() == consumerLease)
                    {
                        cached = &image;
                        break;
                    }
                }
                if (!cached)
                {
                    SharedImage image{};
                    const HRESULT result = m_resources.GetDevice()->OpenSharedHandle(
                        sharedHandle, IID_PPV_ARGS(&image.resource));
                    if (FAILED(result))
                    {
                        m_recordingError = "Player DX12 could not open the producer display image: " +
                            std::to_string(static_cast<long>(result));
                        return 0;
                    }
                    image.handle = sharedHandle;
                    image.lease = consumerLease;
                    cached = &m_sharedImages[m_nextSharedImage];
                    *cached = std::move(image);
                    m_nextSharedImage = (m_nextSharedImage + 1) % m_sharedImages.size();
                }
                const D3D12_RESOURCE_DESC description = cached->resource->GetDesc();
                if (description.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                    description.Format != DXGI_FORMAT_R8G8B8A8_UNORM || description.DepthOrArraySize != 1 ||
                    description.SampleDesc.Count != 1)
                {
                    m_recordingError = "Player DX12 requires a single-sample RGBA8 UNORM game image";
                    return 0;
                }

                // BeginFrame proved the preceding host GPU read complete. Only one
                // descriptor is needed for this game-only owner, with no growing cache.
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                m_resources.GetDevice()->CreateShaderResourceView(cached->resource.Get(), &view,
                    m_srvHeap->GetCPUDescriptorHandleForHeapStart());
                m_pendingUse = {std::move(consumerLease), cached->resource};
                return 1;
            }

            // DX12 consumes the native producer image. A backend mismatch must not
            // quietly substitute a CPU copy or report a drawable texture.
            void SubmitCpuFrame(uint64_t, uint32_t, uint32_t, const void*, uint32_t,
                const RHIDisplayFrameMetadata&) override
            {
            }
            RHIDisplayTexture GetCpuFrameTexture(uint64_t) override { return {}; }

            bool Present(uint64_t textureId, std::string& outError) override
            {
                outError.clear();
                if (!m_frameOpen)
                {
                    return true;
                }
                if (!m_recordingError.empty())
                {
                    outError = m_recordingError;
                    m_resources.AbortFrame();
                    m_frameOpen = false;
                    m_pendingUse = {};
                    return false;
                }

                auto* commands = m_resources.GetCommandList();
                const uint32_t index = m_resources.GetBackBufferIndex();
                auto* backBuffer = m_resources.GetBackBuffer(index);
                D3D12_RESOURCE_BARRIER targetBarrier{};
                targetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                targetBarrier.Transition = {backBuffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
                commands->ResourceBarrier(1, &targetBarrier);
                const D3D12_CPU_DESCRIPTOR_HANDLE target = m_resources.GetBackBufferRtv(index);
                constexpr float kBackground[4]{0.06f, 0.06f, 0.08f, 1.0f};
                commands->ClearRenderTargetView(target, kBackground, 0, nullptr);
                commands->OMSetRenderTargets(1, &target, FALSE, nullptr);

                if (textureId == 1 && m_pendingUse.lease)
                {
                    // Producer live_present leaves this non-simultaneous-access image
                    // in COPY_DEST. Return exactly that state before its next writer.
                    // Producer fence + lease enforce cross-device ordering; COM lifetime
                    // alone does not prevent the producer from overwriting these pixels.
                    D3D12_RESOURCE_BARRIER sourceBarrier{};
                    sourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    sourceBarrier.Transition = {m_pendingUse.resource.Get(),
                        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
                    commands->ResourceBarrier(1, &sourceBarrier);
                    ID3D12DescriptorHeap* heaps[]{m_srvHeap.Get()};
                    commands->SetDescriptorHeaps(1, heaps);
                    commands->SetGraphicsRootSignature(m_rootSignature.Get());
                    commands->SetPipelineState(m_pipeline.Get());
                    commands->SetGraphicsRootDescriptorTable(0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
                    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(m_width),
                        static_cast<float>(m_height), 0.0f, 1.0f};
                    const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
                    commands->RSSetViewports(1, &viewport);
                    commands->RSSetScissorRects(1, &scissor);
                    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    commands->DrawInstanced(3, 1, 0, 0);
                    std::swap(sourceBarrier.Transition.StateBefore, sourceBarrier.Transition.StateAfter);
                    commands->ResourceBarrier(1, &sourceBarrier);
                }
                std::swap(targetBarrier.Transition.StateBefore, targetBarrier.Transition.StateAfter);
                commands->ResourceBarrier(1, &targetBarrier);
                m_frameOpen = false;
                if (!m_resources.EndFrame(outError))
                {
                    m_resources.AbortFrame();
                    // Submission may already have reached the queue. Keep the pending
                    // source quarantined until lifecycle drain proves consumer completion.
                    return false;
                }
                m_submittedUse = std::move(m_pendingUse);
                m_pendingUse = {};
                m_consumerFence = m_resources.GetLastSignaledFenceValue();
                // Native Present waits the submission ticket inside the existing RHI.
                // Its return is not the GPU fence nor physical scan-out completion.
                if (!m_resources.Present(outError))
                {
                    return false;
                }
                if (textureId == 1 && m_submittedUse.lease)
                {
                    RecordSubmittedGameFrame();
                }
                return true;
            }

            void Shutdown() override
            {
                // DX12DeviceResources intentionally retains its device COM object after
                // Shutdown; IsInitialized alone cannot distinguish a second shutdown.
                if (m_shutdown)
                {
                    return;
                }
                m_shutdown = true;
                m_active.store(false, std::memory_order_release);
                if (m_frameOpen)
                {
                    m_resources.AbortFrame();
                    m_frameOpen = false;
                }
                if (m_resources.IsInitialized())
                {
                    std::string error;
                    const bool drained = m_resources.DrainForLifecycle(RHILifecycleCommand::BackendShutdown, error);
                    bool completionProven = drained &&
                        m_resources.GetLastLifecycleResult().command != RHILifecycleCommand::UnrecoverableDeviceError &&
                        SUCCEEDED(m_resources.GetDevice()->GetDeviceRemovedReason());
                    bool deviceLost = FAILED(m_resources.GetDevice()->GetDeviceRemovedReason());
                    if (!completionProven)
                    {
                        MarkShutdownFailure();
                        std::printf("[Player presentation] DX12 shutdown completion failed: %s\n", error.c_str());
                        std::string abandonError;
                        const bool abandoned = m_resources.DrainForLifecycle(
                            RHILifecycleCommand::UnrecoverableDeviceError, abandonError);
                        // Abandon stops CPU queue access; it does not prove that submitted
                        // native commands are complete. Probe only after that owner is quiet.
                        if (abandoned && !deviceLost)
                        {
                            completionProven = CompleteAbandonedQueue();
                            deviceLost = FAILED(m_resources.GetDevice()->GetDeviceRemovedReason());
                        }
                        for (DisplayUse* use : {&m_pendingUse, &m_submittedUse})
                        {
                            if (use->lease && !completionProven)
                            {
                                use->lease->m_completionLost.store(true, std::memory_order_release);
                            }
                        }
                        if (!abandoned || (!completionProven && !deviceLost))
                        {
                            // The factory deliberately retains this entire object until process
                            // exit, including RHI members, descriptor heap, COM images and leases.
                            m_quarantined = true;
                            std::fputs("[Player presentation] FATAL: DX12 consumer completion unproven; "
                                       "native resources quarantined until process exit\n", stderr);
                            return;
                        }
                    }
                }
                if (nullptr != m_shutdownFenceEvent)
                {
                    CloseHandle(m_shutdownFenceEvent);
                    m_shutdownFenceEvent = nullptr;
                }
                m_shutdownFence.Reset();
                m_pendingUse = {};
                m_submittedUse = {};
                m_sharedImages = {};
                m_pipeline.Reset();
                m_rootSignature.Reset();
                m_srvHeap.Reset();
                m_resources.Shutdown();
                m_consumerFence = 0;
                m_nextSharedImage = 0;
                m_width = 0;
                m_height = 0;
                m_recordingError.clear();
            }

        private:
            struct DisplayUse
            {
                std::shared_ptr<RHIDisplayConsumerLease> lease;
                ComPtr<ID3D12Resource> resource;
            };
            struct SharedImage
            {
                void* handle{nullptr};
                std::weak_ptr<RHIDisplayConsumerLease> lease;
                ComPtr<ID3D12Resource> resource;
            };

            bool CompleteAbandonedQueue()
            {
                // Exceptional shutdown recovery only. The RHI owner has been abandoned,
                // so no submission thread can concurrently access this queue anymore.
                if (!m_resources.GetCommandQueue() || FAILED(m_resources.GetDevice()->CreateFence(
                        0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_shutdownFence))))
                {
                    return false;
                }
                m_shutdownFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!m_shutdownFenceEvent || FAILED(m_resources.GetCommandQueue()->Signal(m_shutdownFence.Get(), 1)) ||
                    FAILED(m_shutdownFence->SetEventOnCompletion(1, m_shutdownFenceEvent)))
                {
                    return false;
                }
                constexpr DWORD kShutdownRecoveryWaitMilliseconds = 5000;
                const DWORD wait = WaitForSingleObject(m_shutdownFenceEvent, kShutdownRecoveryWaitMilliseconds);
                const uint64_t completed = m_shutdownFence->GetCompletedValue();
                return WAIT_OBJECT_0 == wait && UINT64_MAX != completed && completed >= 1;
            }

            void CollectDisplayUse()
            {
                const uint64_t completed = m_resources.GetCompletedFenceValue();
                if (m_consumerFence != 0 && completed >= m_consumerFence)
                {
                    m_submittedUse = {};
                    m_consumerFence = 0;
                }
            }

            bool CreatePipeline(std::string& outError)
            {
                D3D12_DESCRIPTOR_HEAP_DESC heap{};
                heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                heap.NumDescriptors = 1;
                heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                if (FAILED(m_resources.GetDevice()->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&m_srvHeap))))
                {
                    outError = "Player DX12 display descriptor creation failed";
                    return false;
                }
                D3D12_DESCRIPTOR_RANGE range{};
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                range.NumDescriptors = 1;
                D3D12_ROOT_PARAMETER parameter{};
                parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                parameter.DescriptorTable = {1, &range};
                parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                D3D12_STATIC_SAMPLER_DESC sampler{};
                sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
                sampler.MaxAnisotropy = 1;
                sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                sampler.MaxLOD = D3D12_FLOAT32_MAX;
                sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
                D3D12_ROOT_SIGNATURE_DESC signature{};
                signature.NumParameters = 1;
                signature.pParameters = &parameter;
                signature.NumStaticSamplers = 1;
                signature.pStaticSamplers = &sampler;
                ComPtr<ID3DBlob> serialized;
                ComPtr<ID3DBlob> errors;
                HRESULT result = D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1,
                    &serialized, &errors);
                if (FAILED(result))
                {
                    outError = "Player DX12 root signature serialization failed";
                    if (errors)
                    {
                        outError.append(": ").append(static_cast<const char*>(errors->GetBufferPointer()),
                            errors->GetBufferSize());
                    }
                    return false;
                }
                result = m_resources.GetDevice()->CreateRootSignature(0, serialized->GetBufferPointer(),
                    serialized->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
                if (FAILED(result))
                {
                    outError = "Player DX12 root signature creation failed";
                    return false;
                }
                RHIShaderCompiler::ScopedOutput output(RHIShaderBinary::Dxil);
                RHIShaderBlob vertex;
                RHIShaderBlob pixel;
                if (!RHIShaderCompiler::CompileFile("PlayerPresentation.slang", "VSMain", "vs_5_0", vertex, outError) ||
                    !RHIShaderCompiler::CompileFile("PlayerPresentation.slang", "PSMain", "ps_5_0", pixel, outError))
                {
                    return false;
                }
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
                pipeline.pRootSignature = m_rootSignature.Get();
                pipeline.VS = {vertex.Data(), vertex.Size()};
                pipeline.PS = {pixel.Data(), pixel.Size()};
                pipeline.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
                pipeline.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
                pipeline.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
                pipeline.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
                pipeline.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
                pipeline.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
                pipeline.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
                pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                pipeline.SampleMask = (std::numeric_limits<UINT>::max)();
                pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                pipeline.RasterizerState.DepthClipEnable = TRUE;
                pipeline.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
                pipeline.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
                pipeline.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
                pipeline.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
                pipeline.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                    D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
                pipeline.DepthStencilState.BackFace = pipeline.DepthStencilState.FrontFace;
                pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pipeline.NumRenderTargets = 1;
                pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                pipeline.SampleDesc.Count = 1;
                if (FAILED(m_resources.GetDevice()->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&m_pipeline))))
                {
                    outError = "Player DX12 fullscreen pipeline creation failed";
                    return false;
                }
                return true;
            }

            DX12DeviceResources m_resources;
            ComPtr<ID3D12DescriptorHeap> m_srvHeap;
            ComPtr<ID3D12RootSignature> m_rootSignature;
            ComPtr<ID3D12PipelineState> m_pipeline;
            std::array<SharedImage, DX12DeviceResources::kFrameCount> m_sharedImages{};
            size_t m_nextSharedImage{0};
            DisplayUse m_pendingUse;
            DisplayUse m_submittedUse;
            uint64_t m_consumerFence{0};
            uint32_t m_width{0};
            uint32_t m_height{0};
            std::atomic<bool> m_active{false};
            bool m_shutdown{true};
            bool m_quarantined{false};
            ComPtr<ID3D12Fence> m_shutdownFence;
            HANDLE m_shutdownFenceEvent{nullptr};
            bool m_frameOpen{false};
            bool m_suspended{false};
            std::string m_recordingError;
        };
    }

    std::shared_ptr<Presentation> CreateDX12Presentation()
    {
        return std::shared_ptr<Presentation>(new DX12Presentation, [](DX12Presentation* presentation)
        {
            presentation->Shutdown();
            if (!presentation->IsQuarantined())
            {
                delete presentation;
            }
        });
    }
}
