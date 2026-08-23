#pragma once

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <string>

// Vulkan 진입점 동적 로딩 (Vulkan 골격).
//
// ★ vulkan-1.lib 을 정적으로 링크하지 않는다. 링크하면 로더가 없는 기계에서
//   **실행 파일이 아예 뜨지 않는다** — 프로세스 시작 시점의 임포트 해석 실패라
//   코드로 막을 수가 없다. DX12 는 Windows 10+ 에 보장되지만 Vulkan 로더는
//   드라이버가 깔아 주는 것이라 보장이 아니다. 그래서 DX12를 선택한 프로세스는
//   이 DLL을 열지 않는다. 반대로 Vulkan을 명시한 프로세스에서 로더가 없으면
//   그 선택의 초기화 실패이며 부팅을 중단한다. 다른 backend로 전환하지 않는다.
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
    X(vkEnumerateDeviceExtensionProperties)   \
    X(vkGetPhysicalDeviceProperties)          \
    X(vkGetPhysicalDeviceFeatures2)           \
    X(vkGetPhysicalDeviceMemoryProperties)    \
    X(vkGetPhysicalDeviceMemoryProperties2)   \
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
    X(vkGetImageMemoryRequirements2)    \
    X(vkBindImageMemory)                \
    X(vkCreateImageView)                \
    X(vkDestroyImageView)               \
    X(vkCreateBuffer)                   \
    X(vkDestroyBuffer)                  \
    X(vkGetBufferMemoryRequirements)    \
    X(vkGetBufferMemoryRequirements2)   \
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
    X(vkCreateComputePipelines)         \
    X(vkDestroyPipeline)                \
    X(vkCreateDescriptorSetLayout)      \
    X(vkDestroyDescriptorSetLayout)     \
    X(vkCreateDescriptorPool)           \
    X(vkDestroyDescriptorPool)          \
    X(vkResetDescriptorPool)            \
    X(vkCreateSampler)                  \
    X(vkDestroySampler)                 \
    X(vkCmdCopyBuffer)                  \
    X(vkCmdCopyBufferToImage)           \
    X(vkCmdCopyImage)                   \
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
    X(vkCmdClearColorImage)             \
    X(vkCmdDraw)                        \
    X(vkCmdDrawIndexed)                 \
    X(vkCmdDispatch)                    \
    X(vkCmdBindVertexBuffers)           \
    X(vkCmdBindVertexBuffers2)          \
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
    /// 로더가 없으면 false. Vulkan이 active backend라면 호출부는 이 실패를
    /// 부팅 실패로 올려야 하며 DX12 fallback을 만들면 안 된다.
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

