#ifndef DYNAMICCPP_EXPORTS
#include "VulkanDeviceResources.h"

#include <Windows.h>
#include <algorithm>
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

    // ── 프레임마다 되감는 것 둘 (5c-4d) ──
    //
    // ★ 크기는 DX12 쪽 링과 같은 값으로 맞춘다. 두 백엔드가 다른 예산으로
    //   돌면 "한쪽만 링이 찬다"가 백엔드 차이로 오독된다.
    constexpr uint64_t kVkUploadBytesPerFrame = 8ull * 1024 * 1024;

    if (!m_uploadRing.Initialize(m_device, m_physicalDevice, m_resourceTable,
        kFrameCount, kVkUploadBytesPerFrame, outError))
    {
        Shutdown();
        return false;
    }
    if (!m_descriptorPool.Initialize(m_device, kFrameCount, outError))
    {
        Shutdown();
        return false;
    }

    m_width = width;
    m_height = height;
    return true;
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

        // 1.3 미만은 거른다 — 동적 렌더링과 synchronization2 가 필요하다.
        if (props.apiVersion < VK_API_VERSION_1_3) continue;

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
        outError = "쓸 수 있는 물리 디바이스가 없다 — Vulkan 1.3 과 그래픽 큐가 필요하다";
        return false;
    }

    m_physicalDevice = best;
    m_adapterName = bestProps.deviceName;
    m_apiVersion = bestProps.apiVersion;
    return true;
}

bool VulkanDeviceResources::CreateDevice(std::string& outError)
{
    const float priority = 1.f;
    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = m_queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

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

    VkPhysicalDeviceVulkan12Features features12{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.timelineSemaphore = VK_TRUE;
    features12.pNext = &features13;

    VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &features12;

    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.pNext = &features2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

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
        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = m_queueFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPools[i]);
        if (VK_SUCCESS != result)
        {
            outError = "커맨드 풀 생성 실패 — " + ResultToString(result);
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool = m_commandPools[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffers[i]);
        if (VK_SUCCESS != result)
        {
            outError = "커맨드 버퍼 할당 실패 — " + ResultToString(result);
            return false;
        }

        m_frameFenceValues[i] = 0;
    }

    return true;
}

void VulkanDeviceResources::Shutdown()
{
    if (VK_NULL_HANDLE != m_device)
    {
        vkDeviceWaitIdle(m_device);

        // ★ 표를 디바이스보다 먼저 비운다 (5c-4c). 칸이 vkDestroy 를 들고
        //   있으므로 순서가 뒤집히면 죽은 디바이스로 부른다 — 표가 순서를
        //   한곳에 모은 이유가 여기서도 같다.
        AccumulateEncoderDiagnostics();
        m_encoder.reset();
        m_renderTargetTable.Reset(m_device);
        m_descriptorPool.Shutdown(m_device);
        m_uploadRing.Shutdown(m_device);
        m_resourceTable.Shutdown(m_device);

        DestroySwapChain();

        for (uint32_t i = 0; i < kFrameCount; ++i)
        {
            if (VK_NULL_HANDLE != m_commandPools[i])
            {
                vkDestroyCommandPool(m_device, m_commandPools[i], nullptr);
                m_commandPools[i] = VK_NULL_HANDLE;
            }
            m_commandBuffers[i] = VK_NULL_HANDLE;
        }

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

    // 이 슬롯이 마지막으로 제출한 작업이 끝나기를 기다린다.
    if (!WaitForFenceValue(m_frameFenceValues[m_frameIndex], outError)) return false;

    const VkResult reset = vkResetCommandPool(m_device, m_commandPools[m_frameIndex], 0);
    if (VK_SUCCESS != reset)
    {
        outError = "커맨드 풀 되감기 실패 — " + ResultToString(reset);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult begun = vkBeginCommandBuffer(m_commandBuffers[m_frameIndex], &beginInfo);
    if (VK_SUCCESS != begun)
    {
        outError = "커맨드 버퍼 시작 실패 — " + ResultToString(begun);
        return false;
    }

    m_frameOpen = true;

    // ★ 렌더 타깃 표는 프레임 수명이다 (5c-4c). 위에서 이 슬롯의 펜스를 이미
    //   기다렸으므로 표가 만든 부분 뷰를 여기서 놓아도 GPU 가 쓰는 중이 아니다
    //   — 표가 펜스를 보지 않는 계약이 성립하는 근거가 이 순서다.
    m_renderTargetTable.Reset(m_device);
    m_bindingTable.Reset();
    m_uploadRing.Reset(m_frameIndex);
    m_descriptorPool.Reset(m_device, m_frameIndex);
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

    const VkResult ended = vkEndCommandBuffer(m_commandBuffers[m_frameIndex]);
    if (VK_SUCCESS != ended)
    {
        outError = "커맨드 버퍼 종료 실패 — " + ResultToString(ended);
        return false;
    }

    VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    commandInfo.commandBuffer = m_commandBuffers[m_frameIndex];

    VkSemaphoreSubmitInfo waits[1]{};
    uint32_t waitCount = 0;
    if (m_imageAcquired)
    {
        waits[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waits[0].semaphore = m_acquireSemaphores[m_semaphoreIndex];
        waits[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        waitCount = 1;
    }

    VkSemaphoreSubmitInfo signals[2]{};
    signals[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signals[0].semaphore = m_timeline;
    signals[0].value = m_nextFenceValue;
    signals[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    uint32_t signalCount = 1;

    if (m_imageAcquired)
    {
        signals[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signals[1].semaphore = m_presentSemaphores[m_semaphoreIndex];
        signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalCount = 2;
    }

    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.waitSemaphoreInfoCount = waitCount;
    submit.pWaitSemaphoreInfos = waits;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = signalCount;
    submit.pSignalSemaphoreInfos = signals;

    const VkResult submitted = vkQueueSubmit2(m_queue, 1, &submit, VK_NULL_HANDLE);
    if (VK_SUCCESS != submitted)
    {
        outError = "큐 제출 실패 — " + ResultToString(submitted);
        return false;
    }

    m_frameFenceValues[m_frameIndex] = m_nextFenceValue;
    ++m_nextFenceValue;
    m_frameOpen = false;
    m_frameIndex = (m_frameIndex + 1) % kFrameCount;
    return true;
}

bool VulkanDeviceResources::FlushCommandList(std::string& outError)
{
    if (!m_frameOpen) { outError = "열린 프레임이 없다"; return false; }

    // 렌더링이 열린 채 커맨드 버퍼를 닫으면 안 된다 (5d). 인코더는
    // BeginFrame 에서야 리셋되므로 소멸자의 보험이 여기서는 안 뛴다 —
    // 프레임 경계가 직접 닫는다.
    if (nullptr != m_encoder) m_encoder->EndRenderTargets();

    const VkResult ended = vkEndCommandBuffer(m_commandBuffers[m_frameIndex]);
    if (VK_SUCCESS != ended)
    {
        outError = "커맨드 버퍼 종료 실패 — " + ResultToString(ended);
        return false;
    }

    VkCommandBufferSubmitInfo commandInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
    commandInfo.commandBuffer = m_commandBuffers[m_frameIndex];

    VkSemaphoreSubmitInfo signal{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    signal.semaphore = m_timeline;
    signal.value = m_nextFenceValue;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;

    const VkResult submitted = vkQueueSubmit2(m_queue, 1, &submit, VK_NULL_HANDLE);
    if (VK_SUCCESS != submitted)
    {
        outError = "중간 제출 실패 — " + ResultToString(submitted);
        return false;
    }

    m_frameFenceValues[m_frameIndex] = m_nextFenceValue;
    ++m_nextFenceValue;

    // ★ 여기서 풀을 되감을 수 없다. GPU 가 아직 그 커맨드를 읽고 있다.
    //   DX12 도 같은 제약이라(얼로케이터 되감기는 완료 후) 계약은 어긋나지
    //   않는다 — 같은 버퍼에 이어서 기록한다.
    if (!WaitForFenceValue(m_frameFenceValues[m_frameIndex], outError)) return false;

    const VkResult reset = vkResetCommandPool(m_device, m_commandPools[m_frameIndex], 0);
    if (VK_SUCCESS != reset)
    {
        outError = "커맨드 풀 되감기 실패 — " + ResultToString(reset);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult begun = vkBeginCommandBuffer(m_commandBuffers[m_frameIndex], &beginInfo);
    if (VK_SUCCESS != begun)
    {
        outError = "커맨드 버퍼 재시작 실패 — " + ResultToString(begun);
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
    if (!IsInitialized()) return;
    vkDeviceWaitIdle(m_device);
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

    vkDeviceWaitIdle(m_device);

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

    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_presentSemaphores[m_semaphoreIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapChain;
    present.pImageIndices = &m_backBufferIndex;

    const VkResult presented = vkQueuePresentKHR(m_queue, &present);
    if (VK_SUCCESS != presented && VK_SUBOPTIMAL_KHR != presented)
    {
        outError = "표시 실패 — " + ResultToString(presented);
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

    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &props);

    for (uint32_t i = 0; i < props.memoryHeapCount; ++i)
    {
        if (0 != (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
        {
            info.budgetMB += props.memoryHeaps[i].size / (1024ull * 1024ull);
        }
    }

    // ★ usedMB 를 채울 수 없다. DXGI 는 QueryVideoMemoryInfo 로 '지금 얼마나
    //   쓰는가'를 코어 기능으로 주지만, Vulkan 은 VK_EXT_memory_budget 확장이
    //   있어야 한다. 인터페이스가 **한쪽에만 코어인 값**을 요구하고 있는
    //   자리다 — 골격이 찾아낸 어긋남 중 하나로 기록한다.
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

#endif
