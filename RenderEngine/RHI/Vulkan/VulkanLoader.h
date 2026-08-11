#pragma once
#ifndef DYNAMICCPP_EXPORTS

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <string>

// Vulkan 진입점 동적 로딩 (Vulkan 골격).
//
// ★ vulkan-1.lib 을 정적으로 링크하지 않는다. 링크하면 로더가 없는 기계에서
//   **실행 파일이 아예 뜨지 않는다** — 프로세스 시작 시점의 임포트 해석 실패라
//   코드로 막을 수가 없다. DX12 는 Windows 10+ 에 보장되지만 Vulkan 로더는
//   드라이버가 깔아 주는 것이라 보장이 아니다. 백엔드가 둘이 되는 순간
//   "둘 중 하나가 없는 기계"가 정상 상황이 되므로, 없으면 조용히 DX12 로
//   가야지 못 뜨면 안 된다.
//
//   대가는 이 파일이다 — 진입점을 손으로 받아 와야 한다. X 매크로로 목록을
//   한 곳에 모아 선언·정의·로드가 어긋날 수 없게 했다.
//
// ★ 세 단계인 것은 Vulkan 의 규칙이다. 인스턴스가 없으면 인스턴스 함수를
//   못 받고, 디바이스 함수를 vkGetInstanceProcAddr 로 받으면 로더의 디스패치
//   층을 한 번 더 거친다(여러 GPU 를 가리려고 있는 층이다). 디바이스 함수는
//   vkGetDeviceProcAddr 로 받아 그 층을 건너뛴다.

#define VK_GLOBAL_FUNCTIONS(X)          \
    X(vkCreateInstance)                 \
    X(vkEnumerateInstanceVersion)       \
    X(vkEnumerateInstanceLayerProperties) \
    X(vkEnumerateInstanceExtensionProperties)

#define VK_INSTANCE_FUNCTIONS(X)              \
    X(vkDestroyInstance)                      \
    X(vkEnumeratePhysicalDevices)             \
    X(vkGetPhysicalDeviceProperties)          \
    X(vkGetPhysicalDeviceMemoryProperties)    \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkCreateDevice)                         \
    X(vkGetDeviceProcAddr)                    \
    X(vkCreateDebugUtilsMessengerEXT)         \
    X(vkDestroyDebugUtilsMessengerEXT)        \
    X(vkCreateWin32SurfaceKHR)                \
    X(vkDestroySurfaceKHR)                    \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)   \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)   \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define VK_DEVICE_FUNCTIONS(X)          \
    X(vkDestroyDevice)                  \
    X(vkGetDeviceQueue)                 \
    X(vkDeviceWaitIdle)                 \
    X(vkQueueSubmit2)                   \
    X(vkQueueWaitIdle)                  \
    X(vkCreateCommandPool)              \
    X(vkDestroyCommandPool)             \
    X(vkResetCommandPool)               \
    X(vkAllocateCommandBuffers)         \
    X(vkFreeCommandBuffers)             \
    X(vkBeginCommandBuffer)             \
    X(vkEndCommandBuffer)               \
    X(vkCreateSemaphore)                \
    X(vkDestroySemaphore)               \
    X(vkGetSemaphoreCounterValue)       \
    X(vkWaitSemaphores)                 \
    X(vkCreateImage)                    \
    X(vkDestroyImage)                   \
    X(vkGetImageMemoryRequirements)     \
    X(vkBindImageMemory)                \
    X(vkCreateImageView)                \
    X(vkDestroyImageView)               \
    X(vkCreateBuffer)                   \
    X(vkDestroyBuffer)                  \
    X(vkGetBufferMemoryRequirements)    \
    X(vkBindBufferMemory)               \
    X(vkAllocateMemory)                 \
    X(vkFreeMemory)                     \
    X(vkMapMemory)                      \
    X(vkUnmapMemory)                    \
    X(vkInvalidateMappedMemoryRanges)   \
    X(vkCreateShaderModule)             \
    X(vkDestroyShaderModule)            \
    X(vkCreatePipelineLayout)           \
    X(vkDestroyPipelineLayout)          \
    X(vkCreateGraphicsPipelines)        \
    X(vkDestroyPipeline)                \
    X(vkCreateDescriptorSetLayout)      \
    X(vkDestroyDescriptorSetLayout)     \
    X(vkCreateDescriptorPool)           \
    X(vkDestroyDescriptorPool)          \
    X(vkResetDescriptorPool)            \
    X(vkCreateSampler)                  \
    X(vkDestroySampler)                 \
    X(vkCmdCopyBufferToImage)           \
    X(vkAllocateDescriptorSets)         \
    X(vkUpdateDescriptorSets)           \
    X(vkCmdBeginRendering)              \
    X(vkCmdEndRendering)                \
    X(vkCmdPipelineBarrier2)            \
    X(vkCmdBindPipeline)                \
    X(vkCmdBindDescriptorSets)          \
    X(vkCmdSetViewport)                 \
    X(vkCmdSetScissor)                  \
    X(vkCmdSetPrimitiveTopology)        \
    X(vkCmdClearAttachments)            \
    X(vkCmdDraw)                        \
    X(vkCmdDrawIndexed)                 \
    X(vkCmdDispatch)                    \
    X(vkCmdBindVertexBuffers)           \
    X(vkCmdBindIndexBuffer)             \
    X(vkCmdCopyImageToBuffer)           \
    X(vkSetDebugUtilsObjectNameEXT)     \
    X(vkCreateSwapchainKHR)             \
    X(vkDestroySwapchainKHR)            \
    X(vkGetSwapchainImagesKHR)          \
    X(vkAcquireNextImageKHR)            \
    X(vkQueuePresentKHR)

namespace VulkanApi
{
    extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

#define VK_DECLARE_FN(name) extern PFN_##name name;
    VK_GLOBAL_FUNCTIONS(VK_DECLARE_FN)
    VK_INSTANCE_FUNCTIONS(VK_DECLARE_FN)
    VK_DEVICE_FUNCTIONS(VK_DECLARE_FN)
#undef VK_DECLARE_FN

    /// vulkan-1.dll 을 열고 전역 진입점을 받는다.
    /// 로더가 없으면 false — **이것이 정상 실패다**(위 ★). 호출부는 이 실패를
    /// 오류가 아니라 "이 기계에는 Vulkan 이 없다"로 다뤄야 한다.
    bool LoadLoader(std::string& outError);

    bool LoadInstance(VkInstance instance, std::string& outError);
    bool LoadDevice(VkDevice device, std::string& outError);

    void Unload();

    /// 로더가 이미 열려 있는가. 여러 번 세워도 DLL 은 한 번만 연다.
    bool IsLoaderReady();

    /// VkResult 를 사람이 읽는 문자열로. 실패 보고에 코드 숫자만 남으면
    /// 원인 추적이 검색부터 시작된다.
    std::string ResultToString(VkResult result);
}

#endif
