#include "VulkanLoader.h"

#include <Windows.h>
#include <sstream>

namespace VulkanApi
{
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;

#define VK_DEFINE_FN(name) PFN_##name name = nullptr;
    VK_GLOBAL_FUNCTIONS(VK_DEFINE_FN)
    VK_INSTANCE_FUNCTIONS(VK_DEFINE_FN)
    VK_DEVICE_FUNCTIONS(VK_DEFINE_FN)
#undef VK_DEFINE_FN
}

namespace
{
    HMODULE g_vulkanModule = nullptr;
}

bool VulkanApi::IsLoaderReady()
{
    return nullptr != g_vulkanModule && nullptr != vkGetInstanceProcAddr;
}

bool VulkanApi::LoadLoader(std::string& outError)
{
    if (IsLoaderReady()) return true;

    if (nullptr == g_vulkanModule)
    {
        g_vulkanModule = ::LoadLibraryW(L"vulkan-1.dll");
        if (nullptr == g_vulkanModule)
        {
            outError = "vulkan-1.dll 을 열 수 없다 — 이 기계에 Vulkan 로더가 없다";
            return false;
        }
    }

    vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        ::GetProcAddress(g_vulkanModule, "vkGetInstanceProcAddr"));
    if (nullptr == vkGetInstanceProcAddr)
    {
        outError = "vkGetInstanceProcAddr 이 없다 — 로더가 손상됐다";
        Unload();
        return false;
    }

    // 전역 진입점은 인스턴스 없이(VK_NULL_HANDLE) 받는다.
    //
    // ★ vkEnumerateInstanceVersion 만 널을 허용한다. Vulkan 1.0 로더에는 없는
    //   함수라, 없으면 "1.0 이다"로 읽는 것이 규격이 정한 방식이다. 나머지가
    //   널이면 로더가 깨진 것이다.
#define VK_LOAD_GLOBAL(name)                                                        \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(VK_NULL_HANDLE, #name)); \
    if (nullptr == name && std::string(#name) != "vkEnumerateInstanceVersion")       \
    {                                                                                \
        outError = std::string("전역 진입점을 받지 못했다: ") + #name;               \
        Unload();                                                                    \
        return false;                                                                \
    }
    VK_GLOBAL_FUNCTIONS(VK_LOAD_GLOBAL)
#undef VK_LOAD_GLOBAL

    return true;
}

bool VulkanApi::LoadInstance(VkInstance instance, std::string& outError)
{
    if (VK_NULL_HANDLE == instance)
    {
        outError = "인스턴스가 없다";
        return false;
    }

    // ★ 디버그 메신저 진입점 둘은 널을 허용한다. VK_EXT_debug_utils 확장을
    //   켜지 못한 경우(레이어 미설치)에도 골격 자체는 서야 하기 때문이다.
    //   대신 그 사실을 호출부가 알아야 하므로, 널 여부로 판단하게 둔다 —
    //   조용히 대체 경로로 가면 "검증이 돌고 있다"는 착각이 생긴다.
#define VK_LOAD_INSTANCE(name)                                                   \
    name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name)); \
    if (nullptr == name                                                          \
        && std::string(#name) != "vkCreateDebugUtilsMessengerEXT"                \
        && std::string(#name) != "vkDestroyDebugUtilsMessengerEXT")              \
    {                                                                            \
        outError = std::string("인스턴스 진입점을 받지 못했다: ") + #name;       \
        return false;                                                            \
    }
    VK_INSTANCE_FUNCTIONS(VK_LOAD_INSTANCE)
#undef VK_LOAD_INSTANCE

    return true;
}

bool VulkanApi::LoadDevice(VkDevice device, std::string& outError)
{
    if (VK_NULL_HANDLE == device)
    {
        outError = "디바이스가 없다";
        return false;
    }
    if (nullptr == vkGetDeviceProcAddr)
    {
        outError = "vkGetDeviceProcAddr 이 없다 — 인스턴스 진입점을 먼저 받아야 한다";
        return false;
    }

    // ★ 이름 붙이기 하나는 널을 허용한다 (5c-4c). VK_EXT_debug_utils 가 없으면
    //   못 받는데, 그때 잃는 것은 **PIX·RenderDoc 에 뜨는 이름뿐**이고 그리는
    //   것은 같다. 위 메신저 둘과 같은 부류이고, 조용히 넘어가도 되는 이유가
    //   같다 — 없어서 잘못 그려지는 일이 없다.
#define VK_LOAD_DEVICE(name)                                                  \
    name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));  \
    if (nullptr == name                                                       \
        && std::string(#name) != "vkSetDebugUtilsObjectNameEXT")              \
    {                                                                         \
        outError = std::string("디바이스 진입점을 받지 못했다: ") + #name;    \
        return false;                                                         \
    }
    VK_DEVICE_FUNCTIONS(VK_LOAD_DEVICE)
#undef VK_LOAD_DEVICE

    return true;
}

void VulkanApi::Unload()
{
#define VK_CLEAR_FN(name) name = nullptr;
    VK_GLOBAL_FUNCTIONS(VK_CLEAR_FN)
    VK_INSTANCE_FUNCTIONS(VK_CLEAR_FN)
    VK_DEVICE_FUNCTIONS(VK_CLEAR_FN)
#undef VK_CLEAR_FN

    vkGetInstanceProcAddr = nullptr;

    if (nullptr != g_vulkanModule)
    {
        ::FreeLibrary(g_vulkanModule);
        g_vulkanModule = nullptr;
    }
}

std::string VulkanApi::ResultToString(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:                        return "VK_SUCCESS";
    case VK_NOT_READY:                      return "VK_NOT_READY";
    case VK_TIMEOUT:                        return "VK_TIMEOUT";
    case VK_INCOMPLETE:                     return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR:                 return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY:       return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:     return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:    return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:              return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:        return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:    return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:     return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_SURFACE_LOST_KHR:         return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:          return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    default: break;
    }

    std::ostringstream oss;
    oss << "VkResult " << static_cast<int>(result);
    return oss.str();
}

