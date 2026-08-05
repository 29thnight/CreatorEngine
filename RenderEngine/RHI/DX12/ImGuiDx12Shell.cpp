#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiDx12Shell.h"

#include "DX12DeviceResources.h"
#include "DX12TextureCache.h"
#include "../../Texture.h"
#include "../../DeviceState.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <wrl/client.h>
#include <unordered_map>
#include <vector>

// 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
namespace
{
    constexpr uint32_t kShellSrvHeapSize = 256;   // 폰트 + 에디터 텍스처 슬롯
}

struct ImGuiDx12Shell::Impl
{
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool active{ false };
    DX12DeviceResources resources;
    DX12TextureCache    textureCache;

    // ImGui 전용 셰이더 가시 SRV 힙 + 프리리스트 할당기.
    // ImGui_ImplDX12의 Srv{Alloc,Free}Fn 콜백이 이 할당기를 쓰고,
    // RegisterTexture/OpenSharedTexture의 사용자 텍스처도 같은 힙을 쓴다 —
    // 힙이 하나여야 SetDescriptorHeaps가 프레임에 한 번으로 끝난다.
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    uint32_t srvIncrement{ 0 };
    std::vector<uint32_t> srvFreeList;

    ImGui_ImplDX12_InitInfo initInfo{};

    // Texture* → ImTextureID(GPU 핸들 ptr). 슬롯은 등록 즉시 확정된다 —
    // 호출부(DataSystem 아이콘 맵 등)가 이 값을 캐시하기 때문이다. 업로드
    // 전까지는 널 SRV(0을 읽는 합법 디스크립터)가 자리를 지킨다.
    std::unordered_map<Texture*, uint64_t> textureSlots;
    struct PendingUpload
    {
        Texture* texture{ nullptr };
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    };
    std::vector<PendingUpload> pendingUploads;

    // 공유 핸들 → (열린 리소스, ImTextureID).
    struct SharedEntry
    {
        ComPtr<ID3D12Resource> resource;
        uint64_t textureId{ 0 };
    };
    std::unordered_map<HANDLE, SharedEntry> sharedTextures;

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
};

ImGuiDx12Shell& ImGuiDx12Shell::Get()
{
    static ImGuiDx12Shell instance;
    return instance;
}

ImGuiDx12Shell::ImGuiDx12Shell() : m_impl(new Impl()) {}
ImGuiDx12Shell::~ImGuiDx12Shell() { delete m_impl; }

bool ImGuiDx12Shell::IsActive() const { return m_impl->active; }

bool ImGuiDx12Shell::Initialize(HWND hwnd, uint32_t width, uint32_t height,
    std::string& outError)
{
    Impl& impl = *m_impl;
    if (impl.active) return true;

    // DX11과 같은 어댑터. 라이브의 공유 텍스처를 이 디바이스에서 열려면
    // (씬 뷰 표시) 같은 물리 어댑터여야 한다 — 공유의 전제 그대로다.
    LUID dx11Luid{ 0, 0 };
    if (DirectX11::DeviceStates->g_pDevice)
    {
        Microsoft::WRL::ComPtr<IDXGIDevice>  dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        if (SUCCEEDED(DirectX11::DeviceStates->g_pDevice->QueryInterface(
                IID_PPV_ARGS(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
        {
            DXGI_ADAPTER_DESC adapterDesc{};
            adapter->GetDesc(&adapterDesc);
            dx11Luid = adapterDesc.AdapterLuid;
        }
    }

    if (!impl.resources.Initialize(width, height, outError, dx11Luid)) return false;
    if (!impl.resources.AttachSwapChain(hwnd, width, height, outError)) return false;
    if (!impl.textureCache.Initialize(&impl.resources,
        DirectX11::DeviceStates->g_pDevice,
        DirectX11::DeviceStates->g_pDeviceContext, outError))
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
    if (!m_impl->active) return;
    ImGui_ImplDX12_NewFrame();
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

uint64_t ImGuiDx12Shell::RegisterTexture(Texture* texture)
{
    Impl& impl = *m_impl;
    if (!impl.active) return 0;
    if (nullptr == texture) return impl.fallbackTextureId;

    const auto found = impl.textureSlots.find(texture);
    if (found != impl.textureSlots.end()) return found->second;

    // 슬롯을 즉시 확정하고 널 SRV로 채워 둔다(리소스 nullptr SRV는 0을 읽는
    // 합법 디스크립터다). 실제 내용은 다음 RenderAndPresent가 업로드 후 같은
    // 슬롯을 덮어쓴다 — 덮어쓰기 전에 인플라이트를 비우므로(WaitForGpu)
    // '참조 중 디스크립터 수정' 규칙을 어기지 않는다.
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!impl.AllocateSlot(cpu, gpu)) return impl.fallbackTextureId;   // 힙 소진
    impl.WriteNullSrv(cpu);

    impl.textureSlots.emplace(texture, gpu.ptr);
    impl.pendingUploads.push_back({ texture, cpu });
    return gpu.ptr;
}

uint64_t ImGuiDx12Shell::OpenSharedTexture(HANDLE sharedHandle)
{
    Impl& impl = *m_impl;
    if (!impl.active || nullptr == sharedHandle) return 0;

    const auto found = impl.sharedTextures.find(sharedHandle);
    if (found != impl.sharedTextures.end()) return found->second.textureId;


    Impl::SharedEntry entry{};
    if (FAILED(impl.resources.GetDevice()->OpenSharedHandle(sharedHandle,
        IID_PPV_ARGS(&entry.resource))))
    {
        return impl.fallbackTextureId;
    }

    const D3D12_RESOURCE_DESC desc = entry.resource->GetDesc();
    entry.textureId = impl.CreateSrvSlot(entry.resource.Get(), desc.Format,
        static_cast<uint32_t>(desc.MipLevels));

    impl.sharedTextures.emplace(sharedHandle, entry);
    return entry.textureId;
}

bool ImGuiDx12Shell::RenderAndPresent(std::string& outError)
{
    Impl& impl = *m_impl;
    if (!impl.active) return true;

    // 새 텍스처 업로드가 있으면 인플라이트를 먼저 비운다 — 이미 확정해 준
    // 슬롯의 디스크립터를 덮어써야 하는데, 이전 프레임 커맨드가 아직 그
    // 슬롯을 참조 중일 수 있다. 에디터 부팅 초기의 아이콘 몇 프레임에만
    // 드는 비용이다.
    const bool hasPendingUploads = !impl.pendingUploads.empty();
    if (hasPendingUploads) impl.resources.WaitForGpu();

    if (!impl.resources.BeginFrame(outError)) return false;
    auto* commandList = impl.resources.GetCommandList();

    if (hasPendingUploads)
    {
        for (const Impl::PendingUpload& pending : impl.pendingUploads)
        {
            std::string uploadError;
            const auto entry = impl.textureCache.GetOrUpload(pending.texture, uploadError);
            if (nullptr == entry.resource) continue;

            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format = entry.format;
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Texture2D.MipLevels = entry.mipLevels;
            impl.resources.GetDevice()->CreateShaderResourceView(
                entry.resource, &desc, pending.cpu);
        }
        impl.pendingUploads.clear();
    }

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

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

    D3D12_RESOURCE_BARRIER toPresent = toRender;
    toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    commandList->ResourceBarrier(1, &toPresent);

    if (!impl.resources.EndFrame(outError)) return false;
    return impl.resources.Present(outError);
}

void ImGuiDx12Shell::Shutdown()
{
    Impl& impl = *m_impl;
    if (!impl.active) return;

    impl.resources.WaitForGpu();
    ImGui_ImplDX12_Shutdown();

    impl.sharedTextures.clear();
    impl.textureSlots.clear();
    impl.pendingUploads.clear();
    impl.textureCache.Shutdown();
    impl.srvHeap.Reset();
    impl.resources.Shutdown();
    impl.active = false;
}

#endif
