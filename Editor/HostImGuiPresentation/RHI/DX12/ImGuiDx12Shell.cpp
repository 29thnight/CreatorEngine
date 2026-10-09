#include "../../../../Engine/EngineDiagnostics/ProfileScope.h"
#include "ImGuiDx12Shell.h"

#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Format.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/RHICompletionRetireQueue.h"
#include "Texture.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <wrl/client.h>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <cstring>
#include <utility>
#include <cstdio>
#include <exception>

// 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
namespace
{
    constexpr uint32_t kShellSrvHeapSize = 256;   // 폰트 + 에디터 텍스처 슬롯
}

struct ImGuiDx12Shell::Impl
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool active{ false };
    bool frameOpen{ false };
    bool deviceLost{ false };
    uint64_t frameIndex{ 0 };
    std::string frameError;
    DX12DeviceResources resources;
    DX12TextureCache    textureCache;

    // 셸은 제거된 장치를 되살리지 않는다. 처음 본 제거만 알리고 이후 GPU 호출을
    // 멈춘다. 그대로 두면 실패가 즉시 반환돼 표시 스레드가 헛돌며 같은 오류를 찍는다.
    bool ObserveDeviceRemoval(const std::string& error)
    {
        ID3D12Device* device = resources.GetDevice();
        if (deviceLost || nullptr == device || S_OK == device->GetDeviceRemovedReason())
        {
            return deviceLost;
        }
        deviceLost = true;
        std::printf("[ImGui] DX12 장치 제거 — 표시를 멈춘다: %s\n", error.c_str());
        std::fflush(stdout);
        return true;
    }

    // ImGui 전용 셰이더 가시 SRV 힙 + 프리리스트 할당기.
    // ImGui_ImplDX12의 Srv{Alloc,Free}Fn 콜백이 이 할당기를 쓰고,
    // RegisterTexture/OpenSharedTexture의 사용자 텍스처도 같은 힙을 쓴다 —
    // 힙이 하나여야 SetDescriptorHeaps가 프레임에 한 번으로 끝난다.
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    uint32_t srvIncrement{ 0 };
    std::vector<uint32_t> srvFreeList;

    ImGui_ImplDX12_InitInfo initInfo{};

    // Texture의 안정 자산 신원 → ImTextureID. 주소를 키로 쓰면 파괴 뒤 같은
    // 주소에 새 Texture가 생겼을 때 이전 descriptor를 돌려주는 ABA가 생긴다.
    // 호출되는 프레임마다 lastUsedFrame을 갱신하고, 미사용 항목은 제출 완료
    // 뒤 슬롯을 반환한다.
    struct TextureSlot
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        uint64_t textureId{ 0 };
        uint64_t lastUsedFrame{ 0 };
        bool uploaded{ false };
        bool initialized{ false };
    };
    std::unordered_map<uint64_t, TextureSlot> textureSlots;

    // 공유 핸들 → (열린 리소스, ImTextureID).
    struct SharedEntry
    {
        ComPtr<ID3D12Resource> resource;
        // 생산 측 종료 뒤 핸들 값이 재사용될 수 있으므로 lease 소유자로 식별한다.
        // 캐시 자체가 강한 참조를 가지면 슬롯 재사용을 막으므로 약한 참조만 둔다.
        std::weak_ptr<RHIDisplayConsumerLease> m_consumerLease;
        uint64_t textureId{ 0 };
        uint64_t lastUsedFrame{ 0 };
    };
    std::unordered_map<HANDLE, SharedEntry> sharedTextures;

    struct DisplayUse
    {
        std::shared_ptr<RHIDisplayConsumerLease> m_lease;
        ComPtr<ID3D12Resource> m_resource;
    };
    std::vector<DisplayUse> m_pendingDisplayUses;
    RHICompletionRetireQueue<std::vector<DisplayUse>> m_displayUseRetireQueue;

    void RetainDisplayUse(std::shared_ptr<RHIDisplayConsumerLease> consumerLease,
        const ComPtr<ID3D12Resource>& resource)
    {
        for (const DisplayUse& use : m_pendingDisplayUses)
        {
            if (use.m_lease == consumerLease)
            {
                return;
            }
        }
        m_pendingDisplayUses.push_back(DisplayUse{ std::move(consumerLease), resource });
    }

    void RetireDisplayUses(uint64_t completionValue)
    {
        if (!m_pendingDisplayUses.empty())
        {
            m_displayUseRetireQueue.Enqueue(RHICompletionPoint{ completionValue },
                std::move(m_pendingDisplayUses));
            m_pendingDisplayUses.clear();
        }
    }

    void CollectDisplayUses(uint64_t completedValue)
    {
        // D3D12 장치 제거 표식은 GPU 읽기 완료를 증명하지 않는다.
        if (0 == completedValue || ~uint64_t{ 0 } == completedValue)
        {
            return;
        }
        m_displayUseRetireQueue.Collect(RHICompletionPoint{ completedValue },
            [](std::vector<DisplayUse>& uses) { uses.clear(); });
    }

    void DrainDisplayUses(bool gpuCompletionProven)
    {
        RetireDisplayUses(0);
        m_displayUseRetireQueue.Drain(
            [gpuCompletionProven](std::vector<DisplayUse>& uses)
            {
                if (!gpuCompletionProven)
                {
                    for (DisplayUse& use : uses)
                    {
                        use.m_lease->m_completionLost.store(true, std::memory_order_release);
                    }
                }
                uses.clear();
            });
    }

    // Vulkan 같은 비-DX12 RHI의 최종 화면은 CPU 리드백 뒤 이 표를 거쳐
    // 셸 디바이스의 RGBA8 텍스처가 된다. pending 쪽만 두 스레드가 공유하고,
    // GPU 객체와 디스크립터는 CE 스레드만 만진다.
    struct CpuFrame
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        std::vector<uint8_t> rgba;
        RHIDisplayFrameMetadata metadata{};
    };
    std::mutex cpuFrameMutex;
    std::unordered_map<uint64_t, CpuFrame> pendingCpuFrames;
    std::unordered_map<uint64_t, CpuFrame> recordingCpuFrames;

    struct CpuFrameEntry
    {
        ComPtr<ID3D12Resource> resource;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        uint64_t textureId{ 0 };
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        bool initialized{ false };
        bool needsRecreate{ false };
        RHIDisplayFrameMetadata metadata{};
        uint64_t lastUsedFrame{ 0 };
    };
    std::unordered_map<uint64_t, CpuFrameEntry> cpuFrames;

    // 실패한 기록의 barrier가 실행됐다고 가정하지 않는다. 다음 업로드는 새
    // 자원으로 시작하고, 옛 자원은 기존 제출 완료점 기반 은퇴 경로로 보낸다.
    void AbortCpuFrames()
    {
        for (const auto& [key, frame] : recordingCpuFrames)
        {
            const auto found = cpuFrames.find(key);
            if (found != cpuFrames.end())
            {
                found->second.metadata = {};
                found->second.needsRecreate = true;
            }
        }
        std::lock_guard<std::mutex> lock(cpuFrameMutex);
        for (auto& [key, frame] : recordingCpuFrames)
        {
            const auto pending = pendingCpuFrames.find(key);
            if (pending == pendingCpuFrames.end() ||
                pending->second.metadata.m_frameId < frame.metadata.m_frameId)
            {
                pendingCpuFrames[key] = std::move(frame);
            }
        }
        recordingCpuFrames.clear();
    }


    // descriptor와 그것이 직접 소유하는 리소스를 현재 제출의 완료점까지
    // 붙든다. 자산 텍스처 리소스는 DX12TextureCache graveyard가 따로 붙든다.
    struct RetiredDescriptor
    {
        uint64_t textureId{ 0 };
        ComPtr<ID3D12Resource> resource;
    };
    RHICompletionRetireQueue<RetiredDescriptor> retireQueue;
    std::vector<RetiredDescriptor> pendingFrameRetirements;

    uint32_t width{ 0 };
    uint32_t height{ 0 };

    // ★ 빈 표시용 폴백 슬롯(null 디스크립터).
    //
    //   DX12에서 ImTextureID 0은 '아무것도 아님'이 아니라 '무효 디스크립터'다.
    //   그대로 SetGraphicsRootDescriptorTable에 들어가면 커맨드 리스트가 그
    //   지점에서 오염되고, 오염 뒤에 기록된 것이 전부 사라진다 —
    //   에디터 창이 통째로 안 보이는 증상으로 나타났고(메뉴바만 남았다),
    //   이어서 Close 실패와 디바이스 제거로 번졌다.
    //
    //   리소스 없는 SRV(null 디스크립터)는 D3D12에서 합법이고 0을 읽는다.
    //   표시할 것이 없을 때는 0이 아니라 이것을 돌려준다.
    uint64_t fallbackTextureId{ 0 };

    // 검증 레이어 메시지(Debug 전용, 처음 본 것만 보고).
    std::unordered_set<std::string> reportedValidation;

    bool AllocateSlot(D3D12_CPU_DESCRIPTOR_HANDLE& outCpu, D3D12_GPU_DESCRIPTOR_HANDLE& outGpu)
    {
        if (srvFreeList.empty()) return false;
        const uint32_t slot = srvFreeList.back();
        srvFreeList.pop_back();
        outCpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
        outCpu.ptr += static_cast<SIZE_T>(slot) * srvIncrement;
        outGpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
        outGpu.ptr += static_cast<UINT64>(slot) * srvIncrement;
        return true;
    }

    void FreeSlot(D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        const UINT64 base = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
        srvFreeList.push_back(static_cast<uint32_t>((gpu.ptr - base) / srvIncrement));
    }

    void RetireDescriptor(uint64_t textureId, uint64_t completionValue,
        ComPtr<ID3D12Resource> resource = {})
    {
        if (0 == textureId || textureId == fallbackTextureId) return;
        retireQueue.Enqueue(RHICompletionPoint{ completionValue },
            RetiredDescriptor{ textureId, std::move(resource) });
    }

    void SweepRetired(uint64_t completedValue)
    {
        retireQueue.Collect(RHICompletionPoint{ completedValue },
            [&](RetiredDescriptor& retired)
            {
                FreeSlot(D3D12_GPU_DESCRIPTOR_HANDLE{ retired.textureId });
                retired.resource.Reset();
            });
    }

    /// 리소스 없는 SRV를 슬롯에 기록한다(빈 표시).
    void WriteNullSrv(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = 1;
        resources.GetDevice()->CreateShaderResourceView(nullptr, &desc, cpu);
    }

    uint64_t CreateSrvSlot(ID3D12Resource* resource, DXGI_FORMAT format, uint32_t mipLevels)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        if (!AllocateSlot(cpu, gpu)) return 0;

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = format;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = mipLevels;
        resources.GetDevice()->CreateShaderResourceView(resource, &desc, cpu);
        return gpu.ptr;
    }
    // UI가 본 메타데이터 뒤에 더 새 픽셀을 올리지 않도록 NewFrame에서만 소비한다.
    bool UploadCpuFrames(std::string& outError)
    {
        Impl& impl = *this;
        {
            std::lock_guard<std::mutex> lock(impl.cpuFrameMutex);
            impl.recordingCpuFrames.swap(impl.pendingCpuFrames);
        }

        auto* commandList = impl.resources.GetCommandList();

        for (const auto& pending : impl.recordingCpuFrames)
        {
            auto found = impl.cpuFrames.find(pending.first);
            if (found == impl.cpuFrames.end())
            {
                D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
                D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
                if (!impl.AllocateSlot(cpu, gpu))
                {
                    outError = "RHI CPU 프레임 표시 SRV 슬롯 부족";
                    impl.resources.AbortFrame();
                    impl.frameOpen = false;
                    return false;
                }
                impl.WriteNullSrv(cpu);
                Impl::CpuFrameEntry entry{};
                entry.cpu = cpu;
                entry.textureId = gpu.ptr;
                entry.lastUsedFrame = impl.frameIndex;
                found = impl.cpuFrames.emplace(pending.first, std::move(entry)).first;
            }

            Impl::CpuFrameEntry& entry = found->second;
            entry.metadata = {};
            const Impl::CpuFrame& frame = pending.second;
            const bool recreate = entry.needsRecreate || !entry.resource || entry.width != frame.width ||
                entry.height != frame.height;
            if (recreate)
            {
                Impl::ComPtr<ID3D12Resource> replacement;
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = frame.width;
                desc.Height = frame.height;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                if (FAILED(impl.resources.GetDevice()->CreateCommittedResource(&heap,
                    D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, IID_PPV_ARGS(&replacement))))
                {
                    outError = "RHI CPU 프레임 표시 텍스처 생성 실패";
                    impl.resources.AbortFrame();
                    impl.frameOpen = false;
                    return false;
                }
                if (entry.resource)
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE newCpu{};
                    D3D12_GPU_DESCRIPTOR_HANDLE newGpu{};
                    if (!impl.AllocateSlot(newCpu, newGpu))
                    {
                        outError = "RHI CPU 프레임 표시 SRV 슬롯 부족";
                        impl.resources.AbortFrame();
                        impl.frameOpen = false;
                        return false;
                    }
                    impl.pendingFrameRetirements.push_back(
                        Impl::RetiredDescriptor{ entry.textureId,
                            std::move(entry.resource) });
                    entry.cpu = newCpu;
                    entry.textureId = newGpu.ptr;
                }
                entry.resource = std::move(replacement);
                entry.width = frame.width;
                entry.height = frame.height;
                entry.initialized = false;
                entry.needsRecreate = false;
            }

            const uint32_t tightPitch = frame.width * 4u;
            const uint32_t uploadPitch = (tightPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
                & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
            const uint64_t uploadBytes = static_cast<uint64_t>(uploadPitch) * frame.height;
            const RHIBufferSlice upload = impl.resources.AllocateUpload(
                RHIUploadRequest{ uploadBytes, RHIUploadUsage::TextureCopy,
                    D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT });
            if (!upload.IsWritable())
            {
                outError = "RHI CPU 프레임 표시 업로드 공간 부족";
                impl.resources.AbortFrame();
                impl.frameOpen = false;
                return false;
            }
            for (uint32_t y = 0; y < frame.height; ++y)
            {
                std::memcpy(static_cast<uint8_t*>(upload.cpuAddress) +
                        static_cast<size_t>(y) * uploadPitch,
                    frame.rgba.data() + static_cast<size_t>(y) * tightPitch, tightPitch);
            }

            if (entry.initialized)
            {
                D3D12_RESOURCE_BARRIER toCopy{};
                toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                toCopy.Transition.pResource = entry.resource.Get();
                toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                commandList->ResourceBarrier(1, &toCopy);
            }

            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = impl.resources.Resolve(upload.buffer);
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint.Offset = upload.offset;
            source.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            source.PlacedFootprint.Footprint.Width = frame.width;
            source.PlacedFootprint.Footprint.Height = frame.height;
            source.PlacedFootprint.Footprint.Depth = 1;
            source.PlacedFootprint.Footprint.RowPitch = uploadPitch;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = entry.resource.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

            D3D12_RESOURCE_BARRIER toRead{};
            toRead.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            toRead.Transition.pResource = entry.resource.Get();
            toRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            toRead.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            toRead.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &toRead);
            entry.initialized = true;
            entry.metadata = frame.metadata;

            if (recreate)
            {
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Texture2D.MipLevels = 1;
                impl.resources.GetDevice()->CreateShaderResourceView(
                    entry.resource.Get(), &srv, entry.cpu);
            }
        }
        return true;
    }

};

ImGuiDx12Shell::ImGuiDx12Shell() : m_impl(new Impl()) {}
ImGuiDx12Shell::~ImGuiDx12Shell()
{
    Shutdown();
    delete m_impl;
}

bool ImGuiDx12Shell::IsActive() const { return m_impl->active; }
bool ImGuiDx12Shell::IsDeviceLost() const { return m_impl->deviceLost; }

bool ImGuiDx12Shell::Initialize(void* windowHandle, uint32_t width, uint32_t height,
    std::string& outError)
{
    Impl& impl = *m_impl;
    if (impl.active) return true;
    HWND hwnd = static_cast<HWND>(windowHandle);

    // ★ 어댑터를 DX11에 맞추던 것을 걷었다 (D4, 2026-08-08).
    //   라이브의 공유 텍스처를 이 디바이스에서 열려면 같은 물리 어댑터여야
    //   한다는 전제는 그대로다. 다만 그 짝이 DX11이 아니라 라이브(DX12)이고,
    //   양쪽이 LUID 없이 초기화하면 둘 다 고성능 어댑터 0번을 고른다 —
    //   결정적이라 같은 것이 나온다.
    if (!impl.resources.Initialize(width, height, outError)) return false;
    if (!impl.resources.AttachSwapChain(hwnd, width, height, outError)) return false;
    if (!impl.textureCache.Initialize(&impl.resources, outError))
    {
        return false;
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kShellSrvHeapSize;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(impl.resources.GetDevice()->CreateDescriptorHeap(
            &heapDesc, IID_PPV_ARGS(&impl.srvHeap))))
        {
            outError = "ImGui SRV 힙 생성 실패";
            return false;
        }
        impl.srvIncrement = impl.resources.GetDevice()
            ->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        impl.srvFreeList.reserve(kShellSrvHeapSize);
        for (uint32_t i = kShellSrvHeapSize; i > 0; --i)
        {
            impl.srvFreeList.push_back(i - 1);
        }
    }

    impl.initInfo = {};
    impl.initInfo.Device = impl.resources.GetDevice();
    impl.initInfo.CommandQueue = impl.resources.GetCommandQueue();
    impl.initInfo.NumFramesInFlight = DX12DeviceResources::kFrameCount;
    impl.initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    impl.initInfo.SrvDescriptorHeap = impl.srvHeap.Get();
    impl.initInfo.UserData = &impl;
    impl.initInfo.SrvDescriptorAllocFn =
        [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
            D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
        {
            static_cast<Impl*>(info->UserData)->AllocateSlot(*outCpu, *outGpu);
        };
    impl.initInfo.SrvDescriptorFreeFn =
        [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE,
            D3D12_GPU_DESCRIPTOR_HANDLE gpu)
        {
            static_cast<Impl*>(info->UserData)->FreeSlot(gpu);
        };

    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        if (!impl.AllocateSlot(cpu, gpu))
        {
            outError = "폴백 SRV 슬롯 확보 실패";
            return false;
        }
        impl.WriteNullSrv(cpu);
        impl.fallbackTextureId = gpu.ptr;
    }

    if (!ImGui_ImplDX12_Init(&impl.initInfo))
    {
        outError = "ImGui_ImplDX12_Init 실패";
        return false;
    }

    impl.width = width;
    impl.height = height;
    impl.active = true;
    return true;
}

uint64_t ImGuiDx12Shell::GetFallbackTextureId() const
{
    return m_impl->fallbackTextureId;
}

void ImGuiDx12Shell::NewFrame()
{
    Impl& impl = *m_impl;
    if (!impl.active || impl.frameOpen) return;
    if (impl.deviceLost)
    {
        ImGui_ImplDX12_NewFrame();
        return;
    }

    const uint64_t completed = impl.resources.GetCompletedFenceValue();
    impl.CollectDisplayUses(completed);
    impl.SweepRetired(completed);
    impl.textureCache.SweepGraveyard(completed);
    impl.textureCache.BeginFrame(impl.frameIndex);

    impl.frameError.clear();
    if (!impl.resources.BeginFrame(impl.frameError))
    {
        if (!impl.ObserveDeviceRemoval(impl.frameError))
            std::printf("[ImGui] DX12 BeginFrame 실패: %s\n", impl.frameError.c_str());
    }
    else
    {
        // BeginFrame의 호스트 프레임 수 제한 대기 중 완료됐을 수도 있다.
        impl.CollectDisplayUses(impl.resources.GetCompletedFenceValue());
        impl.frameOpen = true;
        if (!impl.UploadCpuFrames(impl.frameError))
        {
            impl.AbortCpuFrames();
            std::printf("[ImGui] DX12 CPU 프레임 업로드 실패: %s\n", impl.frameError.c_str());
        }
    }
    ImGui_ImplDX12_NewFrame();
}

void ImGuiDx12Shell::RebuildFontAtlas()
{
    if (!m_impl->active) return;
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
}

void ImGuiDx12Shell::Resize(uint32_t width, uint32_t height)
{
    Impl& impl = *m_impl;
    if (!impl.active) return;
    if (0 == width || 0 == height) return;
    if (width == impl.width && height == impl.height) return;

    std::string error;
    if (impl.resources.ResizeSwapChain(width, height, error))
    {
        impl.width = width;
        impl.height = height;
    }
}

uint64_t ImGuiDx12Shell::RegisterTexture(const Texture* texture,
    const own::shared_owner<const Texture::CodecImage>& image)
{
    Impl& impl = *m_impl;
    if (!impl.active)
    {
        return 0;
    }
    if (nullptr == texture)
    {
        return impl.fallbackTextureId;
    }

    const uint64_t assetId = static_cast<uint64_t>(texture->m_assetId.m_ID_Data);
    auto found = impl.textureSlots.find(assetId);
    if (found == impl.textureSlots.end())
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        if (!impl.AllocateSlot(cpu, gpu))
        {
            return impl.fallbackTextureId;
        }
        impl.WriteNullSrv(cpu);

        Impl::TextureSlot slot{};
        slot.cpu = cpu;
        slot.textureId = gpu.ptr;
        slot.lastUsedFrame = impl.frameIndex;
        found = impl.textureSlots.emplace(assetId, slot).first;
    }

    Impl::TextureSlot& slot = found->second;
    slot.lastUsedFrame = impl.frameIndex;
    const bool previousReady = slot.uploaded && impl.textureCache.IsResident(texture);
    slot.uploaded = previousReady;

    // Outside a frame, reserve only the slot. The caller retains any prepared
    // payload; no raw descriptor or borrowed rows enter a deferred upload queue.
    if (impl.frameOpen)
    {
        std::string uploadError;
        const auto entry = impl.textureCache.GetOrUpload(texture, image, uploadError);
        if (!uploadError.empty())
        {
            // Error fallbacks may be valid white entries. Never install one over
            // the last good SRV or classify it as a completed thumbnail upload.
            std::printf("[ImGui] DX12 사용자 텍스처 업로드 실패: %s\n",
                uploadError.c_str());
            return previousReady ? slot.textureId : impl.fallbackTextureId;
        }
        if (entry.preparationNeeded || !entry.IsValid())
        {
            return previousReady ? slot.textureId : impl.fallbackTextureId;
        }
        if (!previousReady)
        {
            ID3D12Resource* resource = impl.resources.Resolve(entry.handle);
            if (resource == nullptr)
            {
                return impl.fallbackTextureId;
            }
            if (slot.initialized)
            {
                // Older submissions may still read the prior descriptor even
                // after logical cache eviction. Never overwrite that slot.
                D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
                D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
                if (!impl.AllocateSlot(cpu, gpu))
                {
                    return impl.fallbackTextureId;
                }
                try
                {
                    impl.pendingFrameRetirements.push_back(
                        Impl::RetiredDescriptor{ slot.textureId, {} });
                }
                catch (...)
                {
                    impl.FreeSlot(gpu);
                    throw;
                }
                slot.cpu = cpu;
                slot.textureId = gpu.ptr;
            }
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format = ToDXGI(entry.format);
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Texture2D.MipLevels = entry.mipLevels;
            impl.resources.GetDevice()->CreateShaderResourceView(
                resource, &desc, slot.cpu);
            slot.initialized = true;
            slot.uploaded = true;
        }
        return slot.textureId;
    }

    return previousReady ? slot.textureId : impl.fallbackTextureId;
}

bool ImGuiDx12Shell::IsTextureReady(const Texture* texture) const
{
    const Impl& impl = *m_impl;
    if (!impl.active || nullptr == texture)
    {
        return false;
    }

    // A slot can outlive cache eviction or an aborted upload. Never hand its
    // stale SRV to ImGui after the cache has relinquished the native allocation.
    const auto found = impl.textureSlots.find(
        static_cast<uint64_t>(texture->m_assetId.m_ID_Data));
    return found != impl.textureSlots.end() && found->second.uploaded
        && impl.textureCache.IsResident(texture);
}

uint64_t ImGuiDx12Shell::OpenSharedTexture(void* sharedHandleValue,
    std::shared_ptr<RHIDisplayConsumerLease> consumerLease)
{
    Impl& impl = *m_impl;
    HANDLE sharedHandle = static_cast<HANDLE>(sharedHandleValue);
    if (!impl.active || !impl.frameOpen || nullptr == sharedHandle || !consumerLease ||
        consumerLease->m_completionLost.load(std::memory_order_acquire))
    {
        return 0;
    }
    // 분리된 ImGui 창의 별도 큐·펜스는 이 셸이 추적하지 않는다.
    // 소비자 수명을 증명할 수 없으므로 공유 이미지를 게시하지 않는다.
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        return 0;
    }

    const auto found = impl.sharedTextures.find(sharedHandle);
    if (found != impl.sharedTextures.end())
    {
        if (found->second.m_consumerLease.lock() == consumerLease)
        {
            found->second.lastUsedFrame = impl.frameIndex;
            impl.RetainDisplayUse(std::move(consumerLease), found->second.resource);
            return found->second.textureId;
        }
        // 재사용된 핸들이 이전 생산자의 descriptor를 가리키면 안 된다.
        // 이전 descriptor도 현재 기록의 GPU 완료까지 보관한다.
        impl.pendingFrameRetirements.push_back(Impl::RetiredDescriptor{
            found->second.textureId, std::move(found->second.resource) });
        impl.sharedTextures.erase(found);
    }

    Impl::SharedEntry entry{};
    // 별도 디바이스에서 핸들을 여는 성공 여부로 디바이스 간 호환성을 확인한다.
    // 열린 리소스와 생산자 lease를 모두 소비자 GPU 완료까지 보관한다.
    if (FAILED(impl.resources.GetDevice()->OpenSharedHandle(sharedHandle,
        IID_PPV_ARGS(&entry.resource))))
    {
        return impl.fallbackTextureId;
    }

    const D3D12_RESOURCE_DESC desc = entry.resource->GetDesc();
    entry.textureId = impl.CreateSrvSlot(entry.resource.Get(), desc.Format,
        static_cast<uint32_t>(desc.MipLevels));
    if (0 == entry.textureId)
    {
        return impl.fallbackTextureId;
    }
    entry.lastUsedFrame = impl.frameIndex;
    entry.m_consumerLease = consumerLease;
    impl.RetainDisplayUse(std::move(consumerLease), entry.resource);
    const uint64_t textureId = entry.textureId;
    impl.sharedTextures.emplace(sharedHandle, std::move(entry));
    return textureId;
}

void ImGuiDx12Shell::SubmitCpuRgbaFrame(uint64_t key, uint32_t width,
    uint32_t height, const void* rgba, uint32_t rowPitch, const RHIDisplayFrameMetadata& metadata)
{
    Impl& impl = *m_impl;
    if (!impl.active || 0 == key || 0 == width || 0 == height || nullptr == rgba)
    {
        return;
    }
    const uint32_t tightPitch = width * 4u;
    if (rowPitch < tightPitch)
    {
        return;
    }

    Impl::CpuFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.metadata = metadata;
    frame.rgba.resize(static_cast<size_t>(tightPitch) * height);
    const auto* source = static_cast<const uint8_t*>(rgba);
    for (uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(frame.rgba.data() + static_cast<size_t>(y) * tightPitch,
            source + static_cast<size_t>(y) * rowPitch, tightPitch);
    }

    std::lock_guard<std::mutex> lock(impl.cpuFrameMutex);
    impl.pendingCpuFrames[key] = std::move(frame);   // 최신 완성 프레임만 유지
}

RHIDisplayTexture ImGuiDx12Shell::GetCpuFrameTexture(uint64_t key)
{
    Impl& impl = *m_impl;
    if (!impl.active || !impl.frameOpen || 0 == key)
    {
        return {};
    }
    const auto found = impl.cpuFrames.find(key);
    if (found == impl.cpuFrames.end() || !found->second.initialized ||
        0 == found->second.metadata.m_frameId)
    {
        return {};
    }
    auto& entry = found->second;
    entry.lastUsedFrame = impl.frameIndex;
    return {entry.textureId, entry.width, entry.height, entry.metadata};
}

bool ImGuiDx12Shell::RenderAndPresent(std::string& outError,
    const std::function<void()>& onRecorded)
{
    ce::profile_scope profile{ce::marker<"ImGuiDX12MainViewport">()};
    Impl& impl = *m_impl;
    if (!impl.active) return true;
    if (impl.deviceLost)
    {
        outError = "ImGui DX12 장치가 제거됐다";
        return false;
    }
    if (!impl.frameOpen)
    {
        outError = impl.frameError.empty()
            ? "ImGui DX12 프레임이 열리지 않았다" : impl.frameError;
        return false;
    }

    auto* commandList = impl.resources.GetCommandList();

    const uint32_t backBufferIndex = impl.resources.GetBackBufferIndex();
    ID3D12Resource* backBuffer = impl.resources.GetBackBuffer(backBufferIndex);

    D3D12_RESOURCE_BARRIER toRender{};
    toRender.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRender.Transition.pResource = backBuffer;
    toRender.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    toRender.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRender.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toRender);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        impl.resources.GetBackBufferRtv(backBufferIndex);
    constexpr float kClear[4] = { 0.06f, 0.06f, 0.08f, 1.f };
    commandList->ClearRenderTargetView(rtv, kClear, 0, nullptr);
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = { impl.srvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    {
        ce::profile_scope draw{ce::marker<"ImGuiDX12RecordDrawData">()};
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
    }

    D3D12_RESOURCE_BARRIER toPresent = toRender;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &toPresent);

    // 드로우 콜백과 장면 CPU 읽기가 끝났고 공유 표시 사용은 lease를 확보했다.
    // 이후 작업은 보관된 네이티브 리소스와 캐시 상태만 만진다.
    if (onRecorded)
    {
        onRecorded();
    }

    if (!impl.resources.EndFrame(outError))
    {
        impl.resources.AbortFrame();
        impl.AbortCpuFrames();
        // 네이티브 작업이 이미 제출된 뒤 Enqueue가 실패할 수도 있다.
        // 완료를 증명할 수 없으므로 수명 전환 GPU drain까지 소유권을 격리한다.
        impl.RetireDisplayUses(0);
        impl.frameOpen = false;
        impl.ObserveDeviceRemoval(outError);
        return false;
    }
    impl.recordingCpuFrames.clear();

    const uint64_t completionValue = impl.resources.GetLastSignaledFenceValue();
    // CPU 제출이나 Present 반환은 소비자 GPU 완료가 아니다.
    // 생산자와 독립적인 호스트 디바이스 펜스로만 이 큐를 회수한다.
    impl.RetireDisplayUses(completionValue);
    for (Impl::RetiredDescriptor& retired : impl.pendingFrameRetirements)
    {
        impl.RetireDescriptor(retired.textureId, completionValue,
            std::move(retired.resource));
    }
    impl.pendingFrameRetirements.clear();
    for (auto it = impl.textureSlots.begin(); it != impl.textureSlots.end();)
    {
        if (!ImGuiTextureLifetimePolicy::ShouldRetire(
                impl.frameIndex, it->second.lastUsedFrame))
        {
            ++it;
            continue;
        }
        impl.RetireDescriptor(it->second.textureId, completionValue);
        it = impl.textureSlots.erase(it);
    }
    for (auto it = impl.sharedTextures.begin(); it != impl.sharedTextures.end();)
    {
        if (!ImGuiTextureLifetimePolicy::ShouldRetire(
                impl.frameIndex, it->second.lastUsedFrame))
        {
            ++it;
            continue;
        }
        impl.RetireDescriptor(it->second.textureId, completionValue,
            std::move(it->second.resource));
        it = impl.sharedTextures.erase(it);
    }
    for (auto it = impl.cpuFrames.begin(); it != impl.cpuFrames.end();)
    {
        if (!ImGuiTextureLifetimePolicy::ShouldRetire(
                impl.frameIndex, it->second.lastUsedFrame))
        {
            ++it;
            continue;
        }
        impl.RetireDescriptor(it->second.textureId, completionValue,
            std::move(it->second.resource));
        it = impl.cpuFrames.erase(it);
    }
    const RHIDeviceMemoryPressureInfo pressureInfo = impl.resources
        .GetPersistentMemoryBudgetCoordinator().GetMemoryPressureInfo();
    RHIAssetEvictionPass evictionPass = BeginRHIAssetEvictionPass(
        pressureInfo.memoryPressure, pressureInfo.targetReleaseBytes);
    impl.textureCache.RetireUnused(completionValue, &evictionPass);
    impl.frameOpen = false;
    ++impl.frameIndex;

    // W8-3: 가드를 걷었다. 셸의 디바이스는 라이브 렌더러와 **다른 InfoQueue** 를
    // 갖고, 출하 구성에서 이 자리가 닫혀 있으면 그 큐는 영영 읽히지 않는다.
    // 레이어가 꺼져 있으면 드레인이 큐 포인터 하나를 보고 즉시 돌아온다.
    {
        std::string validation;
        if (0 != impl.resources.DrainDebugMessages(validation) && !validation.empty())
        {
            if (impl.reportedValidation.insert(validation).second)
            {
                std::printf("[ImGui 셸 검증] %s\n", validation.c_str());
            }
        }
    }

    const bool presented = impl.resources.Present(outError);
    if (!presented) impl.ObserveDeviceRemoval(outError);
    return presented;
}

void ImGuiDx12Shell::Shutdown()
{
    Impl& impl = *m_impl;

    if (impl.active && impl.frameOpen)
    {
        impl.resources.AbortFrame();
        impl.frameOpen = false;
    }
    std::string lifecycleError;
    bool drained = true;
    if (GetRHISubmissionThread().GetOwnerStats(&impl.resources).registered)
    {
        drained = impl.resources.DrainForLifecycle(RHILifecycleCommand::BackendShutdown, lifecycleError);
        if (!drained && GetRHISubmissionThread().GetOwnerStats(&impl.resources).faulted)
        {
            drained = impl.resources.DrainForLifecycle(RHILifecycleCommand::UnrecoverableDeviceError, lifecycleError);
        }
    }
    if (!drained)
    {
        OutputDebugStringA(("[ImGui DX12] Fatal: forced destruction before verified GPU idle/device loss: " +
            lifecycleError + "\n").c_str());
        std::fprintf(stderr, "Fatal ImGui DX12 teardown invariant: GPU idle/device loss unproven: %s\n",
            lifecycleError.c_str());
        std::fflush(stderr);
        std::terminate();
    }
    // 부분 초기화 실패의 멤버 파괴도 위의 동일한 수명 증명을 지난다.
    if (!impl.active)
    {
        return;
    }
    const bool gpuCompletionProven = drained &&
        impl.resources.GetLastLifecycleResult().command !=
            RHILifecycleCommand::UnrecoverableDeviceError &&
        SUCCEEDED(impl.resources.GetDevice()->GetDeviceRemovedReason());
    // 장치 오류로 제출을 포기한 것은 GPU 완료가 아니다. 호스트 참조를 놓기
    // 전에 관련 생산자 슬롯을 사용 불가로 표시해 재사용을 차단한다.
    impl.DrainDisplayUses(gpuCompletionProven);
    ImGui_ImplDX12_Shutdown();

    impl.sharedTextures.clear();
    impl.cpuFrames.clear();
    impl.recordingCpuFrames.clear();
    {
        std::lock_guard<std::mutex> lock(impl.cpuFrameMutex);
        impl.pendingCpuFrames.clear();
    }
    impl.textureSlots.clear();
    impl.retireQueue.Drain([&](Impl::RetiredDescriptor& retired)
        {
            impl.FreeSlot(D3D12_GPU_DESCRIPTOR_HANDLE{ retired.textureId });
            retired.resource.Reset();
        });
    for (Impl::RetiredDescriptor& retired : impl.pendingFrameRetirements)
    {
        impl.FreeSlot(D3D12_GPU_DESCRIPTOR_HANDLE{ retired.textureId });
        retired.resource.Reset();
    }
    impl.pendingFrameRetirements.clear();
    impl.textureCache.Shutdown();
    impl.srvHeap.Reset();
    impl.resources.Shutdown();
    impl.frameIndex = 0;
    impl.frameError.clear();
    impl.active = false;
}
