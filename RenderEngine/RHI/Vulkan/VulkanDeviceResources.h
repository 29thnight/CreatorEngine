#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "../IRHIDeviceResources.h"

#include <array>
#include <mutex>
#include <vector>

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

class VulkanDeviceResources : public IRHIDeviceResources
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

    // ── 골격이 자기 검사에 쓰는 표면 (인터페이스 밖) ──
    //
    // DX12 쪽 IRenderDeviceServices 에 해당하는 것을 여기서 만들지 않는다.
    // 그쪽은 아직 서명이 DX12 라 구현할 수가 없다(§7.2.2). 골격의 삼각형이
    // 쓰는 최소한만 노출한다.
    VkDevice         GetDevice() const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkQueue          GetQueue() const { return m_queue; }
    uint32_t         GetQueueFamily() const { return m_queueFamily; }
    VkCommandBuffer  GetCommandBuffer() const { return m_commandBuffers[m_frameIndex]; }
    VkImage          GetBackBuffer(uint32_t index) const;
    VkFormat         GetBackBufferFormat() const { return m_swapChainFormat; }

    /// 물리 디바이스가 보고한 이름·API 버전. 자가 검증의 판정 줄에 남긴다.
    const std::string& GetAdapterName() const { return m_adapterName; }
    uint32_t GetApiVersion() const { return m_apiVersion; }
    bool IsValidationEnabled() const { return VK_NULL_HANDLE != m_debugMessenger; }

    /// 메모리 타입 선택. 리소스마다 되풀이되는 계산이라 한곳에 둔다.
    /// 찾지 못하면 UINT32_MAX.
    uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;

private:
    bool CreateInstance(bool enableValidation, std::string& outError);
    bool PickPhysicalDevice(std::string& outError);
    bool CreateDevice(std::string& outError);
    bool CreateFrameResources(std::string& outError);
    void DestroySwapChain();
    bool CreateSwapChainInternal(uint32_t width, uint32_t height, std::string& outError);
    bool WaitForFenceValue(uint64_t value, std::string& outError);

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

#endif
