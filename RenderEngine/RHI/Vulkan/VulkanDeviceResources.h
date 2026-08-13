#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "../IRHIDeviceResources.h"
#include "../IRenderDeviceServices.h"   // 5c-4c — 5c-3 이 중립화하고 여기서 갈렸다
#include "../IRenderTextureCache.h"
#include "VulkanResourceTable.h"
#include "VulkanRenderTargetTable.h"
#include "VulkanBindingTable.h"
#include "VulkanFrameAllocators.h"
#include "VulkanEncoder.h"

#include <array>
#include <memory>
#include <mutex>
#include <vector>

class VulkanPipelineCache;
class Texture;

// Vulkan 디바이스 자원 — IRHIDeviceResources 의 두 번째 구현 (Vulkan 골격).
//
// ── 이 파일이 존재하는 이유 ──
//
// D1 이 IRHIDeviceResources 를 세우며 이렇게 적어 두었다:
//
//   "이 인터페이스가 Vulkan 에 충분한가는 두 번째 구현이 생겨야 답할 수 있는
//    질문이고, D1 은 그것을 답하지 않는다 — 그 자리를 만들 뿐이다."
//
// 여기가 그 청구다. **통과가 목적이 아니라 어긋나는 자리를 찾는 것이 목적이다.**
// 실측(§7.2.2)으로 지금 Vulkan 이 구현할 수 있는 인터페이스는 이것 하나뿐이고,
// 그래서 골격의 삼각형은 RHIEncoder 를 타지 않는다.
//
// ── 프레임 모델 ──
//
// DX12 쪽과 같은 모양으로 맞춘다: 슬롯 kFrameCount 개, BeginFrame 이 그 슬롯의
// 펜스를 기다리고 커맨드 풀을 되감으며, EndFrame 이 제출하고 값을 올린다.
//
// ★ 펜스 = **타임라인 세마포어**다. D1 이 "펜스 값 질의 → ID3D12Fence /
//   타임라인 세마포어"라고 대응을 적어 두었고, 그 대응이 실제로 성립하는지가
//   이 구현이 답하는 것 중 하나다. 이진 세마포어로는 GetCompletedFenceValue 를
//   만들 수 없다 — 값을 물을 수 없기 때문이다.
//
// ── 왜 동적 렌더링(1.3)인가 ──
//
// ★ VkRenderPass/VkFramebuffer 를 쓰지 않는다. 그것을 쓰면 상위 계약에
//   **DX12 에 대응물이 없는 개념**이 생긴다 — DX12 는 렌더 타깃을 커맨드에
//   직접 걸고 렌더 패스 객체가 없다. 중립 층이 두 API 의 교집합 위에 서야
//   하므로, 한쪽에만 있는 객체를 계약에 들이지 않는다.
//
//   대가는 Vulkan 1.3 요구다. 이 기계는 1.4 지만, 요구를 명시적으로 검사해
//   낮은 드라이버에서 조용히 실패하지 않게 한다.

// ── 두 번째 인터페이스 (5c-4c) ──
//
// ★ 위 주석이 "실측(§7.2.2)으로 지금 Vulkan 이 구현할 수 있는 인터페이스는
//   이것 하나뿐"이라고 적어 두었다. **그 조건이 5c-3 에서 끝났다** —
//   `IRenderDeviceServices` 의 순수 가상 26 중 DX12 반환형 12개가 그때
//   하강했고, 남은 15가 전부 중립이다.
//
//   남아 있던 마지막 장벽은 내용이 아니라 **위치**였다(선언이 `d3d12.h` 를
//   무는 헤더 안). 5c-4c 가 그것을 갈랐고, `VulkanEncoder` 가 5c-4b 에서
//   겪은 것과 **같은 부류의 세 번째**다.
//
// ── 15 중 무엇을 하는가 ──
//
// GizmoIcon 슬라이스 뒤에도 실물 13 · 계수 2다. 경계를 임의로 정하지 않고
// **소비자가 부르는 것**으로
// 정했다:
//
//   중립 그래프가 부르는 것 4  `CreateTexture` `ReleaseTexture`
//                              `TransitionResources` `GetImmediateEncoder`
//   그리드 패스가 부르는 것 2  `CreateRenderTargets` · (`UploadConstants` → 5c-4d)
//   표를 채우는 짝 2           `CreateBuffer` (5c-4a 의 버퍼 칸에 생산자가
//                              없었다) · `DescribeTexture` (칸이 이미 든다)
//
// 남은 것은 조용히 넘어가지 않고 세어진다. 인코더와 같은 규약이다.

class VulkanDeviceResources : public IRHIDeviceResources, public IRenderDeviceServices
{
public:
    static constexpr uint32_t kFrameCount = 3;

    VulkanDeviceResources() = default;
    ~VulkanDeviceResources() override;

    VulkanDeviceResources(const VulkanDeviceResources&) = delete;
    VulkanDeviceResources& operator=(const VulkanDeviceResources&) = delete;

    /// ★ 인터페이스에 없는 함수다. D1 이 "Initialize 는 백엔드마다 인자가
    ///   다르다"며 일부러 뺐고(§4), 그 판단이 맞는지가 여기서 확인된다 —
    ///   DX12 쪽은 (width, height, error) 인데 이쪽은 검증 레이어 스위치가
    ///   더 필요하다. 공통 서명을 만들었다면 옵션 뭉치가 됐을 자리다.
    bool Initialize(uint32_t width, uint32_t height, bool enableValidation,
        std::string& outError);

    // ── IRHIDeviceResources ──

    bool IsInitialized() const override { return VK_NULL_HANDLE != m_device; }
    void Shutdown() override;

    bool Resize(uint32_t width, uint32_t height, std::string& outError) override;
    uint32_t GetWidth() const override { return m_width; }
    uint32_t GetHeight() const override { return m_height; }

    bool BeginFrame(std::string& outError) override;
    bool EndFrame(std::string& outError) override;
    bool FlushCommandList(std::string& outError) override;
    void WaitForGpu() override;

    uint64_t GetLastSignaledFenceValue() const override { return m_nextFenceValue - 1; }
    uint64_t GetCompletedFenceValue() const override;

    bool AttachSwapChain(void* windowHandle, uint32_t width, uint32_t height,
        std::string& outError) override;
    bool ResizeSwapChain(uint32_t width, uint32_t height, std::string& outError) override;
    bool Present(std::string& outError) override;
    bool HasSwapChain() const override { return VK_NULL_HANDLE != m_swapChain; }
    uint32_t GetBackBufferIndex() const override { return m_backBufferIndex; }

    uint32_t DrainDebugMessages(std::string& outMessages) override;
    RHIVideoMemoryInfo QueryVideoMemory() const override;
    RHIGpuObjectCensus CaptureLiveObjectCensus(bool allowDeviceEnumeration) override;
    void ReportLiveObjectsToDebugOutput() override;

    // ── IRenderDeviceServices — 실물 ──

    bool CreateBuffer(const RHIBufferDesc& desc,
        RHIBufferHandle& outHandle, std::string& outError) override;
    bool CreateTexture(const RHITextureDesc& desc,
        RHITextureHandle& outHandle, std::string& outError) override;

    RHITextureInfo DescribeTexture(RHITextureHandle handle) const override;
    void ReleaseTexture(RHITextureHandle handle) override;

    void TransitionResources(std::span<const RHITransition> transitions) override;

    RHIRenderTargetBinding CreateRenderTargets(
        std::span<const RHITextureHandle> colors,
        const RHIDepthTargetDesc* depth = nullptr) override;

    RHIEncoder& GetImmediateEncoder() override;

    // ── IRenderDeviceServices — 실물 (5c-4d) ──

    RHIBufferSlice AllocateUpload(uint64_t bytes, uint64_t alignment) override;
    RHIBufferSlice UploadConstants(const void* data, size_t bytes) override;

    /// 이번 프레임에 링에서 잘라 쓴 바이트. 자가 검증이 본다.
    uint64_t GetUploadUsedBytes() const { return m_uploadRing.UsedBytes(); }

    // ── IRenderDeviceServices — 동적 표와 아직 못 하는 것 ──
    //
    // ★ 인코더와 같은 규약이다: 부르면 이름과 함께 세어지고, `vk.*` 검사가
    //   **그 수가 0 인가**를 판정에 넣는다. 조용한 실패로 두면 패스를 옮길 때
    //   "왜 화면이 비었나"부터 되짚어야 한다.

    RHISamplerTable CreateSamplers(std::span<const RHISamplerDesc> descs) override;
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descs) override;

    bool CreateReadback(uint32_t width, uint32_t height, RHIFormat format,
        uint32_t sliceCount, RHIReadback& outReadback, std::string& outError) override;
    bool CreateBufferReadback(uint64_t bytes,
        RHIReadback& outReadback, std::string& outError) override;
    bool MapReadback(const RHIReadback& readback,
        RHIReadbackImage& outImage, std::string& outError) override;
    void ReleaseReadback(RHIReadback& readback) override;

    /// 미구현 호출 수와 마지막 이름. `vk.*` 검사의 판정에 쓴다.
    uint32_t    GetUnimplementedCount() const { return m_unimplemented; }
    const char* GetLastUnimplemented() const { return m_lastUnimplemented; }
    uint32_t GetEncoderUnimplementedCount() const
    {
        return m_encoderUnimplementedTotal +
            (m_encoder ? m_encoder->GetUnimplementedCount() : 0);
    }
    const char* GetEncoderLastUnimplemented() const
    {
        if (m_encoder && 0 != m_encoder->GetUnimplementedCount())
            return m_encoder->GetLastUnimplemented();
        return m_encoderLastUnimplemented;
    }

    /// 핸들 표. 자가 검증과 패스가 자기 리소스를 등록할 때 쓴다.
    VulkanResourceTable&       GetResourceTable() { return m_resourceTable; }
    const VulkanResourceTable& GetResourceTable() const { return m_resourceTable; }

    /// 파이프라인 캐시를 알려 준다 — `GetImmediateEncoder` 가 준 인코더가
    /// `RHIPipelineHandle` 을 풀 수 있어야 한다.
    ///
    /// ★ **디바이스가 소유하지 않는다.** DX12 는 PSO 관리자가 디바이스 곁에
    ///   사는데, 여기서는 캐시가 셰이더 모듈·셋 레이아웃까지 들어서 수명이
    ///   더 크고 지금 소유자가 자가 검증이다. 소유를 옮기는 것은 슬라이스 8
    ///   (러너 배선)의 몫이라, 그때까지 **가리키기만** 한다.
    void SetPipelineCache(const VulkanPipelineCache* cache) { m_pipelineCache = cache; }

    // ── 자가 검증이 쓰는 원시 표면 (인터페이스 밖) ──
    //
    // ★ 5 마무리에서 일곱 → 셋으로 줄었다(호출자 0 이 된 넷을 지웠다 —
    //   `GetPhysicalDevice`·`GetQueue`·`GetQueueFamily`·`GetBackBufferFormat`.
    //   슬라이스 8 의 스왑체인 셸이 필요해지면 그때 소비자와 함께 되살린다).
    //
    //   남은 셋의 남는 이유: `GetDevice` 는 캐시 초기화와 표 직접 해제,
    //   `GetCommandBuffer`·`GetBackBuffer` 는 **백버퍼 표시 전이** — 백버퍼는
    //   스왑체인이 만든 이미지라 핸들 표에 없어 전이를 계약으로 못 건다(R6).
    VkDevice        GetDevice() const { return m_device; }
    VkCommandBuffer GetCommandBuffer() const { return m_commandBuffers[m_frameIndex]; }
    VkImage         GetBackBuffer(uint32_t index) const;

    /// 물리 디바이스가 보고한 이름·API 버전. 자가 검증의 판정 줄에 남긴다.
    const std::string& GetAdapterName() const { return m_adapterName; }
    uint32_t GetApiVersion() const { return m_apiVersion; }
    bool IsValidationEnabled() const { return VK_NULL_HANDLE != m_debugMessenger; }

    /// 메모리 타입 선택. 리소스마다 되풀이되는 계산이라 한곳에 둔다.
    /// 찾지 못하면 UINT32_MAX.
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

private:
    /// 미구현을 센다 (5c-4c). 인코더의 것과 같은 규약이다.
    void NoteUnimplemented(const char* name)
    {
        ++m_unimplemented;
        m_lastUnimplemented = name;
    }

    /// 깊이 타깃이 통째면 칸의 기본 뷰, 부분이면 만들어 표에 맡긴다 (5c-4c).
    VkImageView ResolveDepthView(const RHIDepthTargetDesc& desc,
        const VulkanImageEntry& entry);

    bool CreateInstance(bool enableValidation, std::string& outError);
    bool PickPhysicalDevice(std::string& outError);
    bool CreateDevice(std::string& outError);
    bool CreateFrameResources(std::string& outError);
    void DestroySwapChain();
    bool CreateSwapChainInternal(uint32_t width, uint32_t height, std::string& outError);
    bool WaitForFenceValue(uint64_t value, std::string& outError);
    void AccumulateEncoderDiagnostics();

    VkInstance       m_instance{ VK_NULL_HANDLE };
    VkPhysicalDevice m_physicalDevice{ VK_NULL_HANDLE };
    VkDevice         m_device{ VK_NULL_HANDLE };
    VkQueue          m_queue{ VK_NULL_HANDLE };
    uint32_t         m_queueFamily{ UINT32_MAX };

    VkDebugUtilsMessengerEXT m_debugMessenger{ VK_NULL_HANDLE };

    // 타임라인 세마포어 하나가 DX12 의 ID3D12Fence 자리다.
    VkSemaphore m_timeline{ VK_NULL_HANDLE };
    uint64_t    m_nextFenceValue{ 1 };
    std::array<uint64_t, kFrameCount> m_frameFenceValues{};

    std::array<VkCommandPool, kFrameCount>   m_commandPools{};
    std::array<VkCommandBuffer, kFrameCount> m_commandBuffers{};
    uint32_t m_frameIndex{ 0 };
    bool     m_frameOpen{ false };

    // ── 스왑체인 ──
    VkSurfaceKHR   m_surface{ VK_NULL_HANDLE };
    VkSwapchainKHR m_swapChain{ VK_NULL_HANDLE };
    VkFormat       m_swapChainFormat{ VK_FORMAT_UNDEFINED };
    std::vector<VkImage> m_backBuffers;
    uint32_t m_backBufferIndex{ 0 };

    // ★ 획득·표시용 이진 세마포어. 타임라인으로 대체할 수 없다 —
    //   vkAcquireNextImageKHR 와 vkQueuePresentKHR 이 이진만 받는다.
    //   DX12 에는 대응이 없는 개념이라, 이것이 인터페이스로 새면 계약이
    //   Vulkan 쪽으로 기운다. 여기 가둬 둔다.
    std::vector<VkSemaphore> m_acquireSemaphores;
    std::vector<VkSemaphore> m_presentSemaphores;
    uint32_t m_semaphoreIndex{ 0 };
    bool     m_imageAcquired{ false };

    uint32_t m_width{ 0 };
    uint32_t m_height{ 0 };

    std::string m_adapterName;
    uint32_t    m_apiVersion{ 0 };

    // ── 서비스 (5c-4c) ──

    VulkanResourceTable m_resourceTable;

    /// 프레임 수명이다. `BeginFrame` 이 비운다 — 계약이 "수명은 이 프레임"
    /// 이라고 적어 두었고, DX12 는 프레임 디스크립터 링이 그것을 강제한다.
    VulkanRenderTargetTable m_renderTargetTable;

    /// CreateBindings가 보관하는 프레임 수명 요청. backend에는 이 표의
    /// 1-based 슬롯만 들어가며, BeginFrame의 펜스 대기 뒤에 비운다.
    VulkanBindingTable m_bindingTable;

    /// ★ 프레임마다 다시 만든다. `DX12DeviceResources` 는 제자리 되감기
    ///   (`ResetState`)를 하는데, 이쪽은 커맨드 버퍼가 슬롯마다 다른 객체라
    ///   되감을 것이 아니라 **갈아 끼울 것**이다.
    std::unique_ptr<VulkanEncoder> m_encoder;
    uint32_t    m_encoderUnimplementedTotal{ 0 };
    const char* m_encoderLastUnimplemented{ nullptr };

    // ── 프레임마다 되감는 것 둘 (5c-4d) ──
    VulkanUploadRing     m_uploadRing;
    VulkanDescriptorPool m_descriptorPool;

    /// 소유하지 않는다 (위 `SetPipelineCache` ★).
    const VulkanPipelineCache* m_pipelineCache{ nullptr };

    uint32_t    m_unimplemented{ 0 };
    const char* m_lastUnimplemented{ nullptr };

    // 검증 레이어 콜백이 다른 스레드에서 올 수 있다.
    mutable std::mutex       m_messageMutex;
    std::vector<std::string> m_debugMessages;
    uint32_t                 m_debugProblemCount{ 0 };

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData);
};

/// Texture의 CPU 픽셀을 Vulkan 이미지로 올리는 자산 캐시.
///
/// 이미지와 기본 뷰는 캐시가 소유하고, 프레임 디스크립터 셋은
/// VulkanDeviceResources가 슬롯 펜스 뒤에 되감는다. 업로드 스테이징은 같은
/// 프레임 링에서 오므로 GPU가 복사를 끝내기 전에 덮어쓰이지 않는다.
class VulkanTextureCache final : public IRenderTextureCache
{
public:
    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t uploads{ 0 };
        uint32_t failures{ 0 };
        uint32_t fromCpuPixels{ 0 };
        uint64_t bytesUploaded{ 0 };
    };

    VulkanTextureCache();
    ~VulkanTextureCache() override;

    VulkanTextureCache(const VulkanTextureCache&) = delete;
    VulkanTextureCache& operator=(const VulkanTextureCache&) = delete;

    bool Initialize(VulkanDeviceResources* resources, std::string& outError);
    void Shutdown();

    RHITextureEntry GetOrUpload(Texture* texture, std::string& outError) override;
    RHITextureEntry GetBlackTexture(std::string& outError) override;
    RHITextureEntry GetOrmNeutralTexture(std::string& outError) override;

    Stats GetStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif
