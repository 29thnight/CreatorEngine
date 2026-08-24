#include "VulkanDeviceResources.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace VulkanApi;

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    constexpr const char* kVkValidationLayer = "VK_LAYER_KHRONOS_validation";

    bool VkHasLayer(const std::vector<VkLayerProperties>& layers, const char* name)
    {
        for (const auto& layer : layers)
        {
            if (0 == std::strcmp(layer.layerName, name)) return true;
        }
        return false;
    }

    bool VkHasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
    {
        for (const auto& extension : extensions)
        {
            if (0 == std::strcmp(extension.extensionName, name)) return true;
        }
        return false;
    }

    uint64_t VulkanUploadSoftBudget(const RHIUploadMemoryBudget& budget,
        uint64_t currentSegmentBytes)
    {
        constexpr uint64_t MiB = 1024ull * 1024ull;
        constexpr uint64_t kMinimum = 64ull * MiB;
        constexpr uint64_t kMaximum = 512ull * MiB;
        constexpr uint64_t kFallback = 256ull * MiB;
        if (!budget.IsValid()) return kFallback;

        const uint64_t headroom = budget.budgetBytes > budget.usageBytes
            ? budget.budgetBytes - budget.usageBytes : 0;
        const uint64_t candidate = currentSegmentBytes + headroom / 8;
        return (std::min)(kMaximum, (std::max)(kMinimum, candidate));
    }

    bool VulkanUploadMemoryPressure(const RHIUploadMemoryBudget& budget,
        bool wasPressured)
    {
        if (!budget.IsValid() || budget.estimated) return false;
        const uint64_t releaseThreshold = budget.budgetBytes - budget.budgetBytes / 5;
        const uint64_t enterThreshold = budget.budgetBytes - budget.budgetBytes / 10;
        return wasPressured
            ? budget.usageBytes > releaseThreshold
            : budget.usageBytes >= enterThreshold;
    }
}

VulkanDeviceResources::~VulkanDeviceResources()
{
    Shutdown();
}

void VulkanDeviceResources::AccumulateEncoderDiagnostics()
{
    if (!m_encoder) return;
    const uint32_t count = m_encoder->GetUnimplementedCount();
    m_encoderUnimplementedTotal += count;
    if (0 != count) m_encoderLastUnimplemented = m_encoder->GetLastUnimplemented();
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDeviceResources::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData)
{
    auto* self = static_cast<VulkanDeviceResources*>(userData);
    if (nullptr == self || nullptr == data) return VK_FALSE;

    // 경고 이상만 '실제 문제'로 센다. DX12 쪽 DrainDebugMessages 가
    // ID3D12InfoQueue 에서 경고 이상을 세는 것과 같은 기준이라야 두 백엔드의
    // 판정 줄을 같은 뜻으로 읽을 수 있다.
    //
    // ★ 종류도 함께 본다. GENERAL 은 로더가 하는 말이라 코드가 아니라
    //   **기계 설정**을 가리킨다 — 실측 예: OBS 후크 레이어가 32/64비트로
    //   중복 설치돼 있다는 경고. ID3D12InfoQueue 에는 그런 부류가 없으므로,
    //   그것까지 세면 두 백엔드의 판정이 같은 뜻이 아니게 된다.
    //   로그에는 남기되 결함으로는 세지 않는다.
    const bool severe = 0 != (severity &
        (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT));
    const bool aboutUs = 0 != (types &
        (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT));
    const bool problem = severe && aboutUs;

    std::lock_guard<std::mutex> guard(self->m_messageMutex);
    if (problem) ++self->m_debugProblemCount;
    if (self->m_debugMessages.size() < 64)
    {
        self->m_debugMessages.emplace_back(
            std::string(problem ? "[경고] " : "[정보] ") +
            (data->pMessage ? data->pMessage : ""));
    }
    return VK_FALSE;
}

bool VulkanDeviceResources::Initialize(uint32_t width, uint32_t height,
    bool enableValidation, std::string& outError)
{
    if (IsInitialized()) return true;

    if (!LoadLoader(outError)) return false;
    if (!CreateInstance(enableValidation, outError)) { Shutdown(); return false; }
    if (!PickPhysicalDevice(outError))               { Shutdown(); return false; }
    if (!CreateDevice(outError))                     { Shutdown(); return false; }
    if (!CreateFrameResources(outError))             { Shutdown(); return false; }

    // DX12와 같은 완료점/크기분류 계약. 실제 정렬과 memory type만 Vulkan
    // adapter가 결정한다.
    constexpr uint64_t kRegularUploadSegmentBytes = 16ull * 1024 * 1024;
    constexpr uint64_t kLargeUploadThreshold = 8ull * 1024 * 1024;
    constexpr uint32_t kStandbyRegularSegments = 3;
    RHIUploadSegmentPolicy uploadPolicy{};
    uploadPolicy.regularSegmentBytes = kRegularUploadSegmentBytes;
    uploadPolicy.largeThreshold = kLargeUploadThreshold;
    uploadPolicy.standbyRegularSegments = kStandbyRegularSegments;
    uploadPolicy.largeCacheBudgetBytes = 64ull * 1024 * 1024;
    uploadPolicy.softBudgetBytes = 256ull * 1024 * 1024;

    if (!m_uploadAllocator.Initialize(m_device, m_physicalDevice, m_resourceTable,
        uploadPolicy, outError))
    {
        Shutdown();
        return false;
    }
    RefreshUploadBudget();
    RefreshPersistentMemoryBudgets();
    if (!m_descriptorRecycler.Initialize(m_device, kFrameCount, outError))
    {
        Shutdown();
        return false;
    }

    m_width = width;
    m_height = height;
    if (!GetRHISubmissionThread().AcquireClient(this, outError))
    {
        Shutdown();
        return false;
    }
    m_submissionClient = true;
    return true;
}

RHIUploadMemoryBudget VulkanDeviceResources::QueryUploadMemoryBudget() const
{
    RHIUploadMemoryBudget result{};
    if (VK_NULL_HANDLE == m_physicalDevice) return result;
    const uint32_t heapIndex = m_uploadAllocator.GetMemoryHeapIndex();
    if (UINT32_MAX == heapIndex) return result;

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
    VkPhysicalDeviceMemoryProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
    if (m_memoryBudgetSupported) properties.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &properties);
    if (heapIndex >= properties.memoryProperties.memoryHeapCount) return result;

    if (m_memoryBudgetSupported && 0 != budget.heapBudget[heapIndex])
    {
        result.usageBytes = budget.heapUsage[heapIndex];
        result.budgetBytes = budget.heapBudget[heapIndex];
    }
    else
    {
        result.budgetBytes = properties.memoryProperties.memoryHeaps[heapIndex].size;
        result.estimated = true;
    }
    return result;
}

void VulkanDeviceResources::RefreshUploadBudget()
{
    const RHIUploadMemoryBudget budget = QueryUploadMemoryBudget();
    const uint64_t currentBytes = m_uploadAllocator.GetStats().segmentBytes;
    m_uploadMemoryPressure = VulkanUploadMemoryPressure(budget,
        m_uploadMemoryPressure);
    m_uploadAllocator.UpdateBudget(VulkanUploadSoftBudget(budget, currentBytes),
        m_uploadMemoryPressure);
}

void VulkanDeviceResources::RefreshPersistentMemoryBudgets()
{
    if (VK_NULL_HANDLE == m_physicalDevice) return;

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
    VkPhysicalDeviceMemoryProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
    if (m_memoryBudgetSupported) properties.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &properties);

    for (uint32_t heapIndex = 0;
        heapIndex < properties.memoryProperties.memoryHeapCount; ++heapIndex)
    {
        if (0 == (properties.memoryProperties.memoryHeaps[heapIndex].flags &
            VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)) continue;

        RHIPersistentHeapBudget snapshot{};
        if (m_memoryBudgetSupported && 0 != budget.heapBudget[heapIndex])
        {
            snapshot.usageBytes = budget.heapUsage[heapIndex];
            snapshot.budgetBytes = budget.heapBudget[heapIndex];
        }
        else
        {
            snapshot.budgetBytes =
                properties.memoryProperties.memoryHeaps[heapIndex].size;
            snapshot.estimated = true;
        }
        m_persistentMemoryBudget.UpdateBudget(heapIndex, snapshot);
    }
}

void VulkanDeviceResources::RegisterUploadTransactionListener(
    IRHIUploadTransactionListener* listener)
{
    if (nullptr == listener) return;
    if (m_uploadTransactionListeners.end() == std::find(
        m_uploadTransactionListeners.begin(), m_uploadTransactionListeners.end(), listener))
        m_uploadTransactionListeners.push_back(listener);
}

void VulkanDeviceResources::UnregisterUploadTransactionListener(
    IRHIUploadTransactionListener* listener)
{
    const auto found = std::remove(m_uploadTransactionListeners.begin(),
        m_uploadTransactionListeners.end(), listener);
    m_uploadTransactionListeners.erase(found, m_uploadTransactionListeners.end());
}

bool VulkanDeviceResources::CreateInstance(bool enableValidation, std::string& outError)
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    if (0 != layerCount) vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (0 != extensionCount)
    {
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
    }

    std::vector<const char*> enabledLayers;
    std::vector<const char*> enabledExtensions;

    if (VkHasExtension(extensions, VK_KHR_SURFACE_EXTENSION_NAME))
    {
        enabledExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    }
    if (VkHasExtension(extensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
    {
        enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }

    const bool wantValidation = enableValidation
        && VkHasLayer(layers, kVkValidationLayer)
        && VkHasExtension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (wantValidation)
    {
        enabledLayers.push_back(kVkValidationLayer);
        enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    else if (enableValidation)
    {
        // ★ 조용히 넘어가지 않는다. 검증이 꺼진 채 "통과"가 나오면 그 통과는
        //   아무것도 뜻하지 않는다 — dx12.selftest 가 검증 메시지 0건을 통과
        //   조건으로 삼는 것과 같은 이유다.
        outError = "검증 레이어를 켤 수 없다 — " + std::string(kVkValidationLayer) +
            " 또는 " + VK_EXT_DEBUG_UTILS_EXTENSION_NAME + " 가 없다";
        return false;
    }

    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "CreatorEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "CreatorEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // ★ 메신저 생성 정보를 인스턴스의 pNext 에 건다. 그래야 vkCreateInstance
    //   자체와 vkDestroyInstance 도 검증을 받는다 — 나중에 따로 만들면 그
    //   두 호출만 사각지대가 된다.
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    messengerInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = &VulkanDeviceResources::DebugCallback;
    messengerInfo.pUserData = this;

    VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
    instanceInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    instanceInfo.ppEnabledExtensionNames =
        enabledExtensions.empty() ? nullptr : enabledExtensions.data();
    if (wantValidation) instanceInfo.pNext = &messengerInfo;

    const VkResult created = vkCreateInstance(&instanceInfo, nullptr, &m_instance);
    if (VK_SUCCESS != created)
    {
        outError = "vkCreateInstance 실패 — " + ResultToString(created);
        return false;
    }

    if (!LoadInstance(m_instance, outError)) return false;

    if (wantValidation)
    {
        if (nullptr == vkCreateDebugUtilsMessengerEXT)
        {
            outError = "vkCreateDebugUtilsMessengerEXT 진입점이 없다";
            return false;
        }
        const VkResult made = vkCreateDebugUtilsMessengerEXT(
            m_instance, &messengerInfo, nullptr, &m_debugMessenger);
        if (VK_SUCCESS != made)
        {
            outError = "디버그 메신저 생성 실패 — " + ResultToString(made);
            return false;
        }
    }

    return true;
}

bool VulkanDeviceResources::PickPhysicalDevice(std::string& outError)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (0 == count)
    {
        outError = "Vulkan 물리 디바이스가 없다";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties bestProps{};

    for (VkPhysicalDevice candidate : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(candidate, &props);

        VkPhysicalDeviceVulkan11Features features11{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        VkPhysicalDeviceFeatures2 coreFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        coreFeatures.pNext = &features11;
        vkGetPhysicalDeviceFeatures2(candidate, &coreFeatures);

        // 1.3 미만은 거른다 — 동적 렌더링과 synchronization2 가 필요하다.
        // WireFrame 공용 패스는 polygonMode=LINE을 사용하므로 non-solid fill도
        // 장치 선택 때 계약한다. 지원 여부를 파이프라인 생성까지 미루면 editor
        // 토글 시점에야 실패한다.
        if (props.apiVersion < VK_API_VERSION_1_3 ||
            !coreFeatures.features.independentBlend ||
            !coreFeatures.features.fillModeNonSolid ||
            !features11.shaderDrawParameters) continue;

        // 그래픽 큐가 있어야 한다.
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        uint32_t graphics = UINT32_MAX;
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            if (0 != (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { graphics = i; break; }
        }
        if (UINT32_MAX == graphics) continue;

        const bool better = (VK_NULL_HANDLE == best)
            || (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.deviceType
                && VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU != bestProps.deviceType);
        if (better)
        {
            best = candidate;
            bestProps = props;
            m_queueFamily = graphics;
        }
    }

    if (VK_NULL_HANDLE == best)
    {
        outError = "쓸 수 있는 물리 디바이스가 없다 — Vulkan 1.3, 그래픽 큐, non-solid fill, shaderDrawParameters가 필요하다";
        return false;
    }

    m_physicalDevice = best;
    m_adapterName = bestProps.deviceName;
    m_apiVersion = bestProps.apiVersion;

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(best, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    if (0 != extensionCount)
    {
        vkEnumerateDeviceExtensionProperties(best, nullptr, &extensionCount,
            extensions.data());
    }
    m_memoryBudgetSupported = VkHasExtension(extensions,
        VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    const bool hasRobustness2 = VkHasExtension(extensions,
        VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    if (hasRobustness2)
    {
        VkPhysicalDeviceRobustness2FeaturesEXT robustness{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
        VkPhysicalDeviceFeatures2 features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        features.pNext = &robustness;
        vkGetPhysicalDeviceFeatures2(best, &features);
        m_nullDescriptorSupported = VK_TRUE == robustness.nullDescriptor;
    }
    return true;
}

bool VulkanDeviceResources::CreateDevice(std::string& outError)
{
    const float priority = 1.f;
    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (m_memoryBudgetSupported)
        deviceExtensions.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    if (m_nullDescriptorSupported)
        deviceExtensions.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);

    // 세 기능을 명시적으로 켠다. 켜지 않고 쓰면 검증 레이어가 잡아 주지만,
    // 여기서 요구를 적어 두면 드라이버가 못 주는 경우 생성 단계에서 실패한다.
    VkPhysicalDeviceVulkan13Features features13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    // ★ HLSL 의 discard/clip 이 요구한다 (5d 실측 — vk.grid). dxc 는 SM6 의
    //   discard 를 OpDemoteToHelperInvocation 으로 굽고, 그 능력은 1.3 코어
    //   **기능**이라 켜야 쓴다. 그리드가 첫 소비자였고 패스 17종 대부분이
    //   clip 을 쓰므로 슬라이스 7 전체가 이것에 기댄다.
    features13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceRobustness2FeaturesEXT robustness{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT };
    if (m_nullDescriptorSupported)
    {
        robustness.nullDescriptor = VK_TRUE;
        features13.pNext = &robustness;
    }

    VkPhysicalDeviceVulkan12Features features12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.timelineSemaphore = VK_TRUE;
    features12.pNext = &features13;

    // Slang의 SPIR-V lowering은 SV_InstanceID를 gl_InstanceIndex로 옮기며
    // DrawParameters capability를 선언한다. Vulkan 1.1 코어 기능이지만
    // 선택 기능이므로 장치 생성 때 명시적으로 켜야 한다.
    VkPhysicalDeviceVulkan11Features features11{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    features11.shaderDrawParameters = VK_TRUE;
    features11.pNext = &features12;

    VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    // Decal처럼 MRT마다 쓰기 마스크·blend가 다른 공용 패스가 요구한다.
    // PSO 설명만 independent로 만들어서는 부족하고 장치 기능도 명시적으로
    // 켜야 한다(VUID-VkPipelineColorBlendStateCreateInfo-pAttachments-00605).
    features2.features.independentBlend = VK_TRUE;
    // EnhancedWireFramePass의 RHIFillMode::Wireframe은
    // VkPipelineRasterizationStateCreateInfo::polygonMode=LINE으로 번역된다.
    features2.features.fillModeNonSolid = VK_TRUE;
    features2.pNext = &features11;

    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.pNext = &features2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    const VkResult created = vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device);
    if (VK_SUCCESS != created)
    {
        outError = "vkCreateDevice 실패 — " + ResultToString(created);
        return false;
    }

    if (!LoadDevice(m_device, outError)) return false;

    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    return true;
}

bool VulkanDeviceResources::CreateFrameResources(std::string& outError)
{
    // ★ 타임라인 세마포어 하나가 DX12 의 ID3D12Fence 자리다. D1 이 적어 둔
    //   대응(펜스 값 질의 ↔ 타임라인 세마포어)이 여기서 성립한다 —
    //   GetCompletedFenceValue 가 vkGetSemaphoreCounterValue 그대로다.
    VkSemaphoreTypeCreateInfo typeInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    typeInfo.initialValue = 0;

    VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semaphoreInfo.pNext = &typeInfo;

    VkResult result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_timeline);
    if (VK_SUCCESS != result)
    {
        outError = "타임라인 세마포어 생성 실패 — " + ResultToString(result);
        return false;
    }

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        if (!CreateCommandContext(outError)) return false;
        m_frameFenceValues[i] = 0;
    }

    return true;
}

void VulkanDeviceResources::Shutdown()
{
    if (VK_NULL_HANDLE != m_device)
    {
        if (m_submissionClient)
        {
            std::string lifecycleError;
            const RHISubmissionOwnerStats owner =
                GetRHISubmissionThread().GetOwnerStats(this);
            const RHILifecycleCommand command = owner.faulted
                ? RHILifecycleCommand::UnrecoverableDeviceError
                : RHILifecycleCommand::BackendShutdown;
            if (!DrainForLifecycle(command, lifecycleError) &&
                !lifecycleError.empty())
            {
                OutputDebugStringA(("[Vulkan] lifecycle shutdown 실패: " +
                    lifecycleError + "\n").c_str());
            }
        }
        else vkDeviceWaitIdle(m_device);
        if (m_submissionClient)
        {
            GetRHISubmissionThread().ReleaseClient(this);
            m_submissionClient = false;
        }

        // ★ 표를 디바이스보다 먼저 비운다 (5c-4c). 칸이 vkDestroy 를 들고
        //   있으므로 순서가 뒤집히면 죽은 디바이스로 부른다 — 표가 순서를
        //   한곳에 모은 이유가 여기서도 같다.
        AccumulateEncoderDiagnostics();
        m_encoder.reset();
        m_renderTargetTable.Reset(m_device);
        m_samplerTable.Shutdown(m_device);
        m_descriptorRecycler.Shutdown(m_device);
        m_uploadAllocator.Shutdown(m_device);
        m_resourceTable.Shutdown(m_device);

        DestroySwapChain();

        for (CommandContext& context : m_commandContexts)
        {
            if (VK_NULL_HANDLE != context.pool)
                vkDestroyCommandPool(m_device, context.pool, nullptr);
        }
        m_commandContexts.clear();
        m_currentCommandContext = UINT32_MAX;

        if (VK_NULL_HANDLE != m_timeline)
        {
            vkDestroySemaphore(m_device, m_timeline, nullptr);
            m_timeline = VK_NULL_HANDLE;
        }

        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (VK_NULL_HANDLE != m_instance)
    {
        if (VK_NULL_HANDLE != m_debugMessenger && nullptr != vkDestroyDebugUtilsMessengerEXT)
        {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
            m_debugMessenger = VK_NULL_HANDLE;
        }
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_physicalDevice = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
    m_queueFamily = UINT32_MAX;
    m_memoryBudgetSupported = false;
    m_nullDescriptorSupported = false;
    m_uploadMemoryPressure = false;
    m_persistentMemoryBudget.Reset();
    m_frameOpen = false;
    m_nextFenceValue = 1;
}

bool VulkanDeviceResources::Resize(uint32_t width, uint32_t height, std::string& outError)
{
    if (0 == width || 0 == height)
    {
        outError = "크기가 0이다";
        return false;
    }

    m_width = width;
    m_height = height;

    // 크기에 딸린 것은 스왑체인뿐이다 — 오프스크린 타깃은 골격의 자가 검증이
    // 자기 것으로 들고 있다(DX12 쪽과 달리 이 클래스가 씬 타깃을 소유하지 않는다).
    if (HasSwapChain()) return ResizeSwapChain(width, height, outError);
    return true;
}

bool VulkanDeviceResources::BeginFrame(std::string& outError)
{
    if (!IsInitialized()) { outError = "디바이스가 없다"; return false; }
    if (m_frameOpen)      { outError = "프레임이 이미 열려 있다"; return false; }

    RHISubmissionThread& submission = GetRHISubmissionThread();
    if (submission.ConsumeFailure(this, outError)) return false;
    if (m_frameSubmissionTickets[m_frameIndex].IsValid() &&
        !submission.Wait(m_frameSubmissionTickets[m_frameIndex], outError))
    {
        return false;
    }

    // 이 슬롯이 마지막으로 제출한 작업이 끝나기를 기다린다.
    if (!WaitForFenceValue(m_frameFenceValues[m_frameIndex], outError)) return false;

    if (!AcquireCommandContext(outError)) return false;

    m_frameOpen = true;
    m_acquireConsumed = false;

    // ★ 렌더 타깃 표는 프레임 수명이다 (5c-4c). 위에서 이 슬롯의 펜스를 이미
    //   기다렸으므로 표가 만든 부분 뷰를 여기서 놓아도 GPU 가 쓰는 중이 아니다
    //   — 표가 펜스를 보지 않는 계약이 성립하는 근거가 이 순서다.
    m_renderTargetTable.Reset(m_device);
    m_bindingTable.Reset();
    RefreshUploadBudget();
    RefreshPersistentMemoryBudgets();
    const uint64_t completedFence = GetCompletedFenceValue();
    m_uploadAllocator.Collect(completedFence);
    m_descriptorRecycler.Collect(RHICompletionPoint{ completedFence });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadCompleted(completedFence);
    m_currentRecordingId = m_nextRecordingId++;
    m_uploadAllocator.BeginRecording(m_currentRecordingId);
    if (!m_descriptorRecycler.BeginRecording(
        m_device, m_currentRecordingId, outError))
    {
        m_uploadAllocator.AbortRecording(m_currentRecordingId);
        return false;
    }
    AccumulateEncoderDiagnostics();
    m_encoder.reset();

    // ★ 백버퍼 인덱스를 여기서 얻는다. **이것이 DX12 와 갈리는 첫 자리다** —
    //   DX12 는 IDXGISwapChain3::GetCurrentBackBufferIndex() 로 아무 때나
    //   물을 수 있지만, Vulkan 은 vkAcquireNextImageKHR 을 불러야 하고 그것은
    //   세마포어를 요구하며 블록될 수 있다. 즉 GetBackBufferIndex() 는 DX12
    //   에서 질의지만 Vulkan 에서는 **획득**이다.
    if (HasSwapChain())
    {
        const uint32_t slot = m_semaphoreIndex;
        const VkResult acquired = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
            m_acquireSemaphores[slot], VK_NULL_HANDLE, &m_backBufferIndex);
        if (VK_SUCCESS != acquired && VK_SUBOPTIMAL_KHR != acquired)
        {
            outError = "백버퍼 획득 실패 — " + ResultToString(acquired);
            return false;
        }
        m_imageAcquired = true;
    }

    return true;
}

bool VulkanDeviceResources::EndFrame(std::string& outError)
{
    if (!m_frameOpen) { outError = "열린 프레임이 없다"; return false; }

    // 렌더링이 열린 채 커맨드 버퍼를 닫으면 안 된다 (5d). 인코더는
    // BeginFrame 에서야 리셋되므로 소멸자의 보험이 여기서는 안 뛴다 —
    // 프레임 경계가 직접 닫는다.
    if (nullptr != m_encoder) m_encoder->EndRenderTargets();

    const VkResult ended = vkEndCommandBuffer(GetCommandBuffer());
    if (VK_SUCCESS != ended)
    {
        outError = "커맨드 버퍼 종료 실패 — " + ResultToString(ended);
        return false;
    }

    const VkCommandBuffer commandBuffer = GetCommandBuffer();
    const bool waitForAcquire = m_imageAcquired && !m_acquireConsumed;
    const VkSemaphore acquireSemaphore = waitForAcquire
        ? m_acquireSemaphores[m_semaphoreIndex] : VK_NULL_HANDLE;
    const bool signalPresent = m_imageAcquired;
    const VkSemaphore presentSemaphore = signalPresent
        ? m_presentSemaphores[m_backBufferIndex] : VK_NULL_HANDLE;
    const uint64_t fenceValue = m_nextFenceValue++;

    m_uploadAllocator.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_descriptorRecycler.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadSubmitted(
            m_currentRecordingId, RHICompletionPoint{ fenceValue });
    RetireCurrentCommandContext(fenceValue);
    m_currentRecordingId = 0;
    const uint32_t frameSlot = m_frameIndex;
    m_frameFenceValues[frameSlot] = fenceValue;
    if (waitForAcquire) m_acquireConsumed = true;
    m_frameOpen = false;
    m_frameIndex = (m_frameIndex + 1) % kFrameCount;

    RHISubmissionTicket ticket;
    if (!GetRHISubmissionThread().Enqueue(this, "Vulkan EndFrame",
        [owner = this, queue = m_queue, timeline = m_timeline, commandBuffer,
            acquireSemaphore, waitForAcquire, presentSemaphore, signalPresent,
            fenceValue](std::string& error)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                error = "vkQueueSubmit2가 RHI thread 밖에서 호출됐다";
                return false;
            }
            VkCommandBufferSubmitInfo commandInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            commandInfo.commandBuffer = commandBuffer;
            VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
            wait.semaphore = acquireSemaphore;
            wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSemaphoreSubmitInfo signals[2]{};
            signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signals[0].semaphore = timeline;
            signals[0].value = fenceValue;
            signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signals[1].semaphore = presentSemaphore;
            signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.waitSemaphoreInfoCount = waitForAcquire ? 1u : 0u;
            submit.pWaitSemaphoreInfos = &wait;
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &commandInfo;
            submit.signalSemaphoreInfoCount = signalPresent ? 2u : 1u;
            submit.pSignalSemaphoreInfos = signals;
            const VkResult result = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
            if (VK_SUCCESS != result)
            {
                error = "Vulkan EndFrame 큐 제출 실패 — " + ResultToString(result);
                if (VK_ERROR_DEVICE_LOST == result)
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(owner, error);
                return false;
            }
            return true;
        }, ticket, outError))
    {
        return false;
    }
    m_frameSubmissionTickets[frameSlot] = ticket;
    return true;
}

bool VulkanDeviceResources::CreateCommandContext(std::string& outError)
{
    CommandContext context{};
    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = m_queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &context.pool);
    if (VK_SUCCESS != result)
    {
        outError = "커맨드 풀 생성 실패 — " + ResultToString(result);
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = context.pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(m_device, &allocInfo, &context.buffer);
    if (VK_SUCCESS != result)
    {
        vkDestroyCommandPool(m_device, context.pool, nullptr);
        outError = "커맨드 버퍼 할당 실패 — " + ResultToString(result);
        return false;
    }

    m_commandContexts.push_back(context);
    return true;
}

bool VulkanDeviceResources::AcquireCommandContext(std::string& outError)
{
    const uint64_t completed = GetCompletedFenceValue();
    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < m_commandContexts.size(); ++i)
    {
        CommandContext& context = m_commandContexts[i];
        if (context.state == CommandContextState::Pending &&
            context.completionValue <= completed)
        {
            context.state = CommandContextState::Available;
            context.completionValue = 0;
        }
        if (UINT32_MAX == selected && context.state == CommandContextState::Available)
            selected = i;
    }

    if (UINT32_MAX == selected)
    {
        if (!CreateCommandContext(outError)) return false;
        selected = static_cast<uint32_t>(m_commandContexts.size() - 1);
    }

    CommandContext& context = m_commandContexts[selected];
    VkResult result = vkResetCommandPool(m_device, context.pool, 0);
    if (VK_SUCCESS != result)
    {
        outError = "커맨드 풀 되감기 실패 — " + ResultToString(result);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(context.buffer, &beginInfo);
    if (VK_SUCCESS != result)
    {
        outError = "커맨드 버퍼 시작 실패 — " + ResultToString(result);
        return false;
    }

    context.state = CommandContextState::Recording;
    m_currentCommandContext = selected;
    return true;
}

void VulkanDeviceResources::RetireCurrentCommandContext(uint64_t completionValue)
{
    if (m_currentCommandContext >= m_commandContexts.size()) return;
    CommandContext& context = m_commandContexts[m_currentCommandContext];
    context.state = CommandContextState::Pending;
    context.completionValue = completionValue;
    m_currentCommandContext = UINT32_MAX;
}

VkCommandBuffer VulkanDeviceResources::GetCommandBuffer() const
{
    return m_currentCommandContext < m_commandContexts.size()
        ? m_commandContexts[m_currentCommandContext].buffer : VK_NULL_HANDLE;
}

void VulkanDeviceResources::AbortFrame()
{
    if (!m_frameOpen) return;

    // 아직 queue에 제출되지 않은 command buffer는 pool reset으로 폐기한다.
    AccumulateEncoderDiagnostics();
    m_encoder.reset();
    if (m_currentCommandContext < m_commandContexts.size())
    {
        CommandContext& context = m_commandContexts[m_currentCommandContext];
        vkResetCommandPool(m_device, context.pool, 0);

        if (m_imageAcquired)
        {
            // acquire semaphore와 획득 이미지는 그냥 버릴 수 없다. 비어 있는
            // command buffer를 제출해 acquire를 소비하고 present semaphore를
            // signal한 뒤 즉시 표시한다. partial recording의 명령은 reset으로
            // 이미 폐기됐고 업로드 transaction도 아래에서 rollback한다.
            VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VkResult result = vkBeginCommandBuffer(context.buffer, &begin);
            if (VK_SUCCESS == result)
            {
                // 획득 이미지를 표시 엔진에 돌려주려면 빈 프레임이어도 반드시
                // PRESENT_SRC 레이아웃이어야 한다. 이전 내용은 버리므로
                // UNDEFINED를 oldLayout으로 써서 어떤 이전 레이아웃도 의존하지 않는다.
                VkImageMemoryBarrier2 imageBarrier{
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
                imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                imageBarrier.image = m_backBuffers[m_backBufferIndex];
                imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageBarrier.subresourceRange.levelCount = 1;
                imageBarrier.subresourceRange.layerCount = 1;

                VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                dependency.imageMemoryBarrierCount = 1;
                dependency.pImageMemoryBarriers = &imageBarrier;
                vkCmdPipelineBarrier2(context.buffer, &dependency);
            }
            if (VK_SUCCESS == result) result = vkEndCommandBuffer(context.buffer);

            VkCommandBufferSubmitInfo command{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            command.commandBuffer = context.buffer;

            VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
            uint32_t waitCount = 0;
            if (!m_acquireConsumed)
            {
                wait.semaphore = m_acquireSemaphores[m_semaphoreIndex];
                wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                waitCount = 1;
            }

            VkSemaphoreSubmitInfo signals[2]{};
            signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signals[0].semaphore = m_timeline;
            signals[0].value = m_nextFenceValue;
            signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
            signals[1].semaphore = m_presentSemaphores[m_backBufferIndex];
            signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.waitSemaphoreInfoCount = waitCount;
            submit.pWaitSemaphoreInfos = &wait;
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &command;
            submit.signalSemaphoreInfoCount = 2;
            submit.pSignalSemaphoreInfos = signals;
            std::string abortSubmissionError;
            if (VK_SUCCESS == result)
            {
                const VkQueue queue = m_queue;
                const VkSwapchainKHR swapChain = m_swapChain;
                const uint32_t imageIndex = m_backBufferIndex;
                result = GetRHISubmissionThread().ExecuteAndWait(this,
                    "Vulkan AbortFrame submit/present",
                    [queue, submit, command, wait, signals, swapChain, imageIndex](
                        std::string& error) mutable
                    {
                        if (!GetRHISubmissionThread().IsCurrentThread())
                        {
                            error = "Vulkan AbortFrame queue 호출이 RHI thread 밖에서 실행됐다";
                            return false;
                        }
                        submit.pCommandBufferInfos = &command;
                        submit.pWaitSemaphoreInfos = &wait;
                        submit.pSignalSemaphoreInfos = signals;
                        VkResult queueResult = vkQueueSubmit2(queue, 1, &submit,
                            VK_NULL_HANDLE);
                        if (VK_SUCCESS != queueResult)
                        {
                            error = "Vulkan AbortFrame 제출 실패 — " +
                                ResultToString(queueResult);
                            return false;
                        }
                        VkPresentInfoKHR present{
                            VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
                        present.waitSemaphoreCount = 1;
                        present.pWaitSemaphores = &signals[1].semaphore;
                        present.swapchainCount = 1;
                        present.pSwapchains = &swapChain;
                        present.pImageIndices = &imageIndex;
                        queueResult = vkQueuePresentKHR(queue, &present);
                        if (VK_SUCCESS != queueResult &&
                            VK_SUBOPTIMAL_KHR != queueResult)
                        {
                            error = "Vulkan AbortFrame 표시 실패 — " +
                                ResultToString(queueResult);
                            return false;
                        }
                        return true;
                    }, abortSubmissionError) ? VK_SUCCESS : VK_ERROR_DEVICE_LOST;
            }

            if (VK_SUCCESS == result)
            {
                RetireCurrentCommandContext(m_nextFenceValue);
                m_frameFenceValues[m_frameIndex] = m_nextFenceValue++;

                m_semaphoreIndex = (m_semaphoreIndex + 1)
                    % static_cast<uint32_t>(m_acquireSemaphores.size());
                m_imageAcquired = false;
                m_acquireConsumed = false;
                m_frameIndex = (m_frameIndex + 1) % kFrameCount;
            }
            else
            {
                OutputDebugStringA(("[Vulkan] AbortFrame acquire 정리 실패: "
                    + (abortSubmissionError.empty() ? ResultToString(result)
                        : abortSubmissionError) + "\n").c_str());
                context.state = CommandContextState::Available;
                context.completionValue = 0;
                m_currentCommandContext = UINT32_MAX;
            }
        }
        else
        {
            context.state = CommandContextState::Available;
            context.completionValue = 0;
            m_currentCommandContext = UINT32_MAX;
        }
    }
    m_uploadAllocator.AbortRecording(m_currentRecordingId);
    m_descriptorRecycler.AbortRecording(m_currentRecordingId);
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadAborted(m_currentRecordingId);
    m_currentRecordingId = 0;
    m_frameOpen = false;
}

bool VulkanDeviceResources::FlushCommandList(std::string& outError)
{
    if (!m_frameOpen) { outError = "열린 프레임이 없다"; return false; }

    // 렌더링이 열린 채 커맨드 버퍼를 닫으면 안 된다 (5d). 인코더는
    // BeginFrame 에서야 리셋되므로 소멸자의 보험이 여기서는 안 뛴다 —
    // 프레임 경계가 직접 닫는다.
    if (nullptr != m_encoder) m_encoder->EndRenderTargets();

    const VkResult ended = vkEndCommandBuffer(GetCommandBuffer());
    if (VK_SUCCESS != ended)
    {
        outError = "커맨드 버퍼 종료 실패 — " + ResultToString(ended);
        return false;
    }

    const VkCommandBuffer commandBuffer = GetCommandBuffer();
    const bool waitForAcquire = m_imageAcquired && !m_acquireConsumed;
    const VkSemaphore acquireSemaphore = waitForAcquire
        ? m_acquireSemaphores[m_semaphoreIndex] : VK_NULL_HANDLE;
    const uint64_t fenceValue = m_nextFenceValue++;

    m_uploadAllocator.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_descriptorRecycler.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadSubmitted(
            m_currentRecordingId, RHICompletionPoint{ fenceValue });
    RetireCurrentCommandContext(fenceValue);
    if (waitForAcquire) m_acquireConsumed = true;
    const uint32_t frameSlot = m_frameIndex;
    m_frameFenceValues[frameSlot] = fenceValue;

    RHISubmissionTicket ticket;
    if (!GetRHISubmissionThread().Enqueue(this, "Vulkan immediate flush",
        [owner = this, queue = m_queue, timeline = m_timeline, commandBuffer,
            acquireSemaphore, waitForAcquire, fenceValue](std::string& error)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                error = "Vulkan 중간 vkQueueSubmit2가 RHI thread 밖에서 호출됐다";
                return false;
            }
            VkCommandBufferSubmitInfo commandInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
            commandInfo.commandBuffer = commandBuffer;
            VkSemaphoreSubmitInfo wait{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
            wait.semaphore = acquireSemaphore;
            wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
            signal.semaphore = timeline;
            signal.value = fenceValue;
            signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
            submit.waitSemaphoreInfoCount = waitForAcquire ? 1u : 0u;
            submit.pWaitSemaphoreInfos = &wait;
            submit.commandBufferInfoCount = 1;
            submit.pCommandBufferInfos = &commandInfo;
            submit.signalSemaphoreInfoCount = 1;
            submit.pSignalSemaphoreInfos = &signal;
            const VkResult result = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE);
            if (VK_SUCCESS != result)
            {
                error = "Vulkan 중간 제출 실패 — " + ResultToString(result);
                if (VK_ERROR_DEVICE_LOST == result)
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(owner, error);
                return false;
            }
            return true;
        }, ticket, outError))
    {
        return false;
    }
    m_frameSubmissionTickets[frameSlot] = ticket;

    // 제출한 pool은 Pending으로 남기고 완료된 다른 pool을 즉시 얻는다.
    // available이 없으면 현재 요청에서 하나를 만든다. CPU wait는 없다.
    AccumulateEncoderDiagnostics();
    m_encoder.reset();
    if (!AcquireCommandContext(outError)) return false;

    m_currentRecordingId = m_nextRecordingId++;
    m_uploadAllocator.BeginRecording(m_currentRecordingId);
    if (!m_descriptorRecycler.BeginRecording(
        m_device, m_currentRecordingId, outError))
    {
        m_uploadAllocator.AbortRecording(m_currentRecordingId);
        return false;
    }

    return true;
}

bool VulkanDeviceResources::PrepareParallelSubmission(
    RHICompletionPoint& outCompletion, std::string& outError)
{
    if (!m_frameOpen || VK_NULL_HANDLE == m_queue || VK_NULL_HANDLE == m_timeline)
    {
        outError = "Vulkan 병렬 제출 준비 coordinator가 초기화되지 않았다";
        return false;
    }
    const RHICompletionPoint completion{ m_nextFenceValue++ };
    m_uploadAllocator.OnSubmitted(m_currentRecordingId, completion);
    m_descriptorRecycler.OnSubmitted(m_currentRecordingId, completion);
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadSubmitted(m_currentRecordingId, completion);
    m_frameFenceValues[m_frameIndex] = completion.value;
    outCompletion = completion;

    // immediate command context는 Prepare 뒤 열린 빈 buffer 그대로 이어 쓴다.
    // 다만 descriptor version이 바뀌었으므로 encoder의 binding 기억은 버린다.
    AccumulateEncoderDiagnostics();
    m_encoder.reset();
    m_currentRecordingId = m_nextRecordingId++;
    m_uploadAllocator.BeginRecording(m_currentRecordingId);
    if (!m_descriptorRecycler.BeginRecording(
        m_device, m_currentRecordingId, outError))
    {
        m_uploadAllocator.AbortRecording(m_currentRecordingId);
        return false;
    }
    return true;
}

bool VulkanDeviceResources::SubmitParallelCommandBuffers(
    std::span<const VkCommandBuffer> buffers, RHICompletionPoint completion,
    std::string& outError)
{
    if (VK_NULL_HANDLE == m_queue || VK_NULL_HANDLE == m_timeline ||
        !completion.IsValid())
    {
        outError = "Vulkan 병렬 제출 coordinator/completion이 초기화되지 않았다";
        return false;
    }
    if (!GetRHISubmissionThread().IsCurrentThread())
    {
        outError = "Vulkan 병렬 vkQueueSubmit2가 RHI thread 밖에서 호출됐다";
        return false;
    }

    std::vector<VkCommandBufferSubmitInfo> commandInfos(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i)
    {
        commandInfos[i].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfos[i].commandBuffer = buffers[i];
    }
    VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    signal.semaphore = m_timeline;
    signal.value = completion.value;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = static_cast<uint32_t>(commandInfos.size());
    submit.pCommandBufferInfos = commandInfos.data();
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    const VkResult submitted = vkQueueSubmit2(m_queue, 1, &submit, VK_NULL_HANDLE);
    if (VK_SUCCESS != submitted)
    {
        outError = "Vulkan 병렬 command buffer 제출 실패 — " +
            ResultToString(submitted);
        if (VK_ERROR_DEVICE_LOST == submitted)
            GetRHISubmissionThread().MarkUnrecoverableDeviceError(this, outError);
        return false;
    }
    return true;
}

bool VulkanDeviceResources::WaitForFenceValue(uint64_t value, std::string& outError)
{
    if (0 == value) return true;

    VkSemaphoreWaitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_timeline;
    waitInfo.pValues = &value;

    const VkResult waited = vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);
    if (VK_SUCCESS != waited)
    {
        outError = "펜스 대기 실패 — " + ResultToString(waited);
        return false;
    }
    return true;
}

void VulkanDeviceResources::WaitForGpu()
{
    std::string error;
    if (!DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error) &&
        !error.empty())
    {
        OutputDebugStringA(("[Vulkan] offline GPU drain 실패: " + error + "\n").c_str());
    }
}

bool VulkanDeviceResources::DrainForLifecycle(RHILifecycleCommand command,
    std::string& outError)
{
    if (!IsInitialized() || !m_submissionClient) return true;
    RHISubmissionThread& submission = GetRHISubmissionThread();
    const RHISubmissionOwnerStats before = submission.GetOwnerStats(this);
    if (before.IsIdle() &&
        ((before.lastCommand == command &&
            (RHILifecycleCommand::BackendShutdown == command ||
             RHILifecycleCommand::SwapChainResize == command)) ||
         (before.faulted && RHILifecycleCommand::UnrecoverableDeviceError ==
            before.lastCommand)))
    {
        m_lastLifecycleResult = {};
        m_lastLifecycleResult.command = before.lastCommand;
        m_lastLifecycleResult.previousGeneration = before.generation - 1u;
        m_lastLifecycleResult.generation = before.generation;
        m_lastLifecycleResult.drained = true;
        return true;
    }
    if (RHILifecycleCommand::UnrecoverableDeviceError == command || before.faulted)
    {
        return submission.AbandonForDeviceError(this,
            m_lastLifecycleResult, outError);
    }

    if (!submission.ExecuteLifecycleDrain(this, command,
        [owner = this, device = m_device](std::string& taskError)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                taskError = "Vulkan lifecycle drain이 RHI thread 밖에서 호출됐다";
                return false;
            }
            const VkResult result = vkDeviceWaitIdle(device);
            if (VK_SUCCESS != result)
            {
                taskError = "Vulkan lifecycle vkDeviceWaitIdle 실패 — " +
                    ResultToString(result);
                if (VK_ERROR_DEVICE_LOST == result)
                {
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(
                        owner, taskError);
                }
                return false;
            }
            return true;
        }, m_lastLifecycleResult, outError)) return false;

    const uint64_t completed = GetCompletedFenceValue();
    m_descriptorRecycler.Collect(RHICompletionPoint{ completed });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadCompleted(completed);
    std::printf("[RHI lifecycle][Vulkan] %s generation %llu->%llu"
        " pending task/batch/retirement %u/%u/%u\n",
        ToString(command),
        static_cast<unsigned long long>(m_lastLifecycleResult.previousGeneration),
        static_cast<unsigned long long>(m_lastLifecycleResult.generation),
        m_lastLifecycleResult.pendingTasks,
        m_lastLifecycleResult.pendingBatches,
        m_lastLifecycleResult.pendingRetirements);
    return m_lastLifecycleResult.IsClean();
}

uint64_t VulkanDeviceResources::GetCompletedFenceValue() const
{
    if (VK_NULL_HANDLE == m_timeline) return 0;

    uint64_t value = 0;
    if (VK_SUCCESS != vkGetSemaphoreCounterValue(m_device, m_timeline, &value)) return 0;
    return value;
}

// ── 스왑체인 ──

bool VulkanDeviceResources::AttachSwapChain(void* windowHandle, uint32_t width,
    uint32_t height, std::string& outError)
{
    if (!IsInitialized()) { outError = "디바이스가 없다"; return false; }
    if (nullptr == windowHandle) { outError = "창 핸들이 없다"; return false; }
    if (HasSwapChain()) { outError = "이미 스왑체인이 붙어 있다"; return false; }

    VkWin32SurfaceCreateInfoKHR surfaceInfo{
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    surfaceInfo.hinstance = ::GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(windowHandle);

    VkResult result = vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface);
    if (VK_SUCCESS != result)
    {
        outError = "Win32 서피스 생성 실패 — " + ResultToString(result);
        return false;
    }

    VkBool32 supported = VK_FALSE;
    result = vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_queueFamily,
        m_surface, &supported);
    if (VK_SUCCESS != result || VK_FALSE == supported)
    {
        outError = "이 큐 패밀리가 서피스를 표시할 수 없다";
        DestroySwapChain();
        return false;
    }

    if (!CreateSwapChainInternal(width, height, outError))
    {
        DestroySwapChain();
        return false;
    }

    m_width = width;
    m_height = height;
    return true;
}

bool VulkanDeviceResources::CreateSwapChainInternal(uint32_t width, uint32_t height,
    std::string& outError)
{
    VkSurfaceCapabilitiesKHR caps{};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice,
        m_surface, &caps);
    if (VK_SUCCESS != result)
    {
        outError = "서피스 능력 질의 실패 — " + ResultToString(result);
        return false;
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    if (0 == formatCount) { outError = "서피스 포맷이 없다"; return false; }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount,
        formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& format : formats)
    {
        if (VK_FORMAT_B8G8R8A8_UNORM == format.format
            && VK_COLOR_SPACE_SRGB_NONLINEAR_KHR == format.colorSpace)
        {
            chosen = format;
            break;
        }
    }

    // ★ 괄호로 감싸는 것은 취향이 아니다. Windows.h 가 max/min 을 매크로로 정의하고
    //   이 리포는 NOMINMAX 를 세우지 않는다. #define NOMINMAX 를 이 파일에 넣어도
    //   유니티 빌드에서는 옆 파일이 이미 Windows.h 를 끌어온 뒤라 듣지 않는다 —
    //   괄호는 그 두 경우 모두에서 매크로 확장을 막는다.
    uint32_t imageCount = (std::max)(caps.minImageCount, kFrameCount);
    if (0 != caps.maxImageCount) imageCount = (std::min)(imageCount, caps.maxImageCount);

    VkExtent2D extent{ width, height };
    if (UINT32_MAX != caps.currentExtent.width) extent = caps.currentExtent;

    VkSwapchainCreateInfoKHR swapInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapInfo.surface = m_surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = chosen.format;
    swapInfo.imageColorSpace = chosen.colorSpace;
    swapInfo.imageExtent = extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;   // 항상 있는 유일한 모드
    swapInfo.clipped = VK_TRUE;

    result = vkCreateSwapchainKHR(m_device, &swapInfo, nullptr, &m_swapChain);
    if (VK_SUCCESS != result)
    {
        outError = "스왑체인 생성 실패 — " + ResultToString(result);
        return false;
    }

    m_swapChainFormat = chosen.format;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &actualCount, nullptr);
    m_backBuffers.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &actualCount, m_backBuffers.data());

    m_acquireSemaphores.resize(actualCount, VK_NULL_HANDLE);
    m_presentSemaphores.resize(actualCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < actualCount; ++i)
    {
        VkSemaphoreCreateInfo info{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (VK_SUCCESS != vkCreateSemaphore(m_device, &info, nullptr, &m_acquireSemaphores[i])
            || VK_SUCCESS != vkCreateSemaphore(m_device, &info, nullptr, &m_presentSemaphores[i]))
        {
            outError = "표시용 세마포어 생성 실패";
            return false;
        }
    }

    m_semaphoreIndex = 0;
    m_backBufferIndex = 0;
    m_imageAcquired = false;
    return true;
}

void VulkanDeviceResources::DestroySwapChain()
{
    if (VK_NULL_HANDLE == m_device) return;

    for (VkSemaphore semaphore : m_acquireSemaphores)
    {
        if (VK_NULL_HANDLE != semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : m_presentSemaphores)
    {
        if (VK_NULL_HANDLE != semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_acquireSemaphores.clear();
    m_presentSemaphores.clear();
    m_backBuffers.clear();

    if (VK_NULL_HANDLE != m_swapChain)
    {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
    if (VK_NULL_HANDLE != m_surface)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    m_imageAcquired = false;
}

bool VulkanDeviceResources::ResizeSwapChain(uint32_t width, uint32_t height,
    std::string& outError)
{
    if (!HasSwapChain()) { outError = "스왑체인이 없다"; return false; }

    if (!DrainForLifecycle(RHILifecycleCommand::SwapChainResize, outError))
        return false;

    for (VkSemaphore semaphore : m_acquireSemaphores)
    {
        if (VK_NULL_HANDLE != semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : m_presentSemaphores)
    {
        if (VK_NULL_HANDLE != semaphore) vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_acquireSemaphores.clear();
    m_presentSemaphores.clear();
    m_backBuffers.clear();
    vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
    m_swapChain = VK_NULL_HANDLE;

    if (!CreateSwapChainInternal(width, height, outError)) return false;

    m_width = width;
    m_height = height;
    return true;
}

bool VulkanDeviceResources::Present(std::string& outError)
{
    if (!HasSwapChain()) { outError = "스왑체인이 없다"; return false; }
    if (!m_imageAcquired) { outError = "획득한 백버퍼가 없다"; return false; }

    const VkSemaphore waitSemaphore = m_presentSemaphores[m_backBufferIndex];
    const VkSwapchainKHR swapChain = m_swapChain;
    const uint32_t imageIndex = m_backBufferIndex;
    if (!GetRHISubmissionThread().ExecuteAndWait(this, "Vulkan Present",
        [owner = this, queue = m_queue, waitSemaphore, swapChain, imageIndex](
            std::string& error)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                error = "vkQueuePresentKHR가 RHI thread 밖에서 호출됐다";
                return false;
            }
            VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = &waitSemaphore;
            present.swapchainCount = 1;
            present.pSwapchains = &swapChain;
            present.pImageIndices = &imageIndex;
            const VkResult result = vkQueuePresentKHR(queue, &present);
            if (VK_SUCCESS != result && VK_SUBOPTIMAL_KHR != result)
            {
                error = "Vulkan 표시 실패 — " + ResultToString(result);
                if (VK_ERROR_DEVICE_LOST == result)
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(owner, error);
                return false;
            }
            return true;
        }, outError))
    {
        return false;
    }

    m_semaphoreIndex = (m_semaphoreIndex + 1) % static_cast<uint32_t>(m_acquireSemaphores.size());
    m_imageAcquired = false;
    return true;
}

VkImage VulkanDeviceResources::GetBackBuffer(uint32_t index) const
{
    return (index < m_backBuffers.size()) ? m_backBuffers[index] : VK_NULL_HANDLE;
}

// ── 진단 ──

uint32_t VulkanDeviceResources::DrainDebugMessages(std::string& outMessages)
{
    std::lock_guard<std::mutex> guard(m_messageMutex);

    for (const auto& message : m_debugMessages)
    {
        outMessages += message;
        outMessages += "\n";
    }
    m_debugMessages.clear();

    const uint32_t problems = m_debugProblemCount;
    m_debugProblemCount = 0;
    return problems;
}

RHIVideoMemoryInfo VulkanDeviceResources::QueryVideoMemory() const
{
    RHIVideoMemoryInfo info{};
    if (VK_NULL_HANDLE == m_physicalDevice) return info;

    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
    VkPhysicalDeviceMemoryProperties2 properties{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
    if (m_memoryBudgetSupported) properties.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &properties);
    const VkPhysicalDeviceMemoryProperties& props = properties.memoryProperties;

    for (uint32_t i = 0; i < props.memoryHeapCount; ++i)
    {
        if (0 != (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
        {
            const uint64_t budgetBytes = m_memoryBudgetSupported &&
                0 != budget.heapBudget[i]
                ? budget.heapBudget[i] : props.memoryHeaps[i].size;
            info.budgetMB += budgetBytes / (1024ull * 1024ull);
            if (m_memoryBudgetSupported)
                info.usedMB += budget.heapUsage[i] / (1024ull * 1024ull);
        }
    }
    return info;
}

RHIGpuObjectCensus VulkanDeviceResources::CaptureLiveObjectCensus(bool /*allowDeviceEnumeration*/)
{
    RHIGpuObjectCensus census{};

    const RHIVideoMemoryInfo memory = QueryVideoMemory();
    census.vramUsedMB = memory.usedMB;
    census.vramBudgetMB = memory.budgetMB;

    // ★ available=false 로 둔다. Vulkan 에는 '살아 있는 객체를 타입별로
    //   나열한다'는 개념이 없다 — ID3D12DebugDevice::ReportLiveDeviceObjects
    //   에 대응하는 것이 없다. 인터페이스의 byType 예시가 "ID3D12Resource"인
    //   것이 이 필드가 DX12 어휘라는 증거다.
    census.available = false;
    return census;
}

void VulkanDeviceResources::ReportLiveObjectsToDebugOutput()
{
    // 대응물이 없다(위 ★). 검증 레이어가 vkDestroyInstance 시점에 남은 객체를
    // 스스로 보고하므로, 같은 목적은 그쪽이 이미 수행한다.
}

uint32_t VulkanDeviceResources::FindMemoryType(uint32_t typeBits,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memory);

    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    {
        const bool typeAllowed = 0 != (typeBits & (1u << i));
        const bool hasProperties =
            properties == (memory.memoryTypes[i].propertyFlags & properties);
        if (typeAllowed && hasProperties) return i;
    }
    return UINT32_MAX;
}

