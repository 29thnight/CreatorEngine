#ifndef DYNAMICCPP_EXPORTS
#include "ImGuiVulkanShell.h"
#include "VulkanDeviceResources.h"
#include "VulkanFormat.h"
#include "../RHICompletionRetireQueue.h"
#include "../../Texture.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// vcpkg의 공식 ImGui Vulkan backend는 vulkan-1.lib에 연결된다. 지연 로드로
// 두어 DX12 부팅에서는 Vulkan loader가 없는 환경도 계속 허용한다. Vulkan을
// 실제 선택했을 때는 VulkanDeviceResources의 동적 로더 검사가 먼저 실행된다.
#pragma comment(linker, "/DELAYLOAD:vulkan-1.dll")
#pragma comment(lib, "delayimp.lib")

namespace
{
    constexpr uint32_t kImGuiDescriptorPoolSize = 512;

    template <typename T>
    uint64_t ToImTextureId(T handle)
    {
        if constexpr (std::is_pointer_v<T>)
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
        else
            return static_cast<uint64_t>(handle);
    }
}

struct ImGuiVulkanShell::Impl
{
    bool active{ false };
    bool frameOpen{ false };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint64_t frameIndex{ 0 };

    VulkanDeviceResources resources;
    VulkanTextureCache textureCache;
    std::vector<VkImageView> backBufferViews;
    std::vector<bool> backBufferInitialized;

    struct TextureSetEntry
    {
        VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
        uint64_t lastUsedFrame{ 0 };
    };
    std::unordered_map<uint64_t, TextureSetEntry> textureSets;
    VkDescriptorSet fallbackSet{ VK_NULL_HANDLE };

    struct CpuFrame
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        std::vector<uint8_t> rgba;
    };
    std::mutex cpuFrameMutex;
    std::unordered_map<uint64_t, CpuFrame> pendingCpuFrames;

    struct CpuFrameEntry
    {
        RHITextureHandle handle;
        VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        bool initialized{ false };
        uint64_t lastUsedFrame{ 0 };
    };
    std::unordered_map<uint64_t, CpuFrameEntry> cpuFrames;

    struct RetiredTexture
    {
        VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
        RHITextureHandle handle;
    };
    RHICompletionRetireQueue<RetiredTexture> retireQueue;
    std::vector<RetiredTexture> pendingFrameRetirements;
    std::unordered_set<std::string> reportedValidation;

    void RetireTexture(VkDescriptorSet descriptorSet, RHITextureHandle handle,
        uint64_t completionValue)
    {
        if (VK_NULL_HANDLE == descriptorSet && !handle.IsValid()) return;
        retireQueue.Enqueue(RHICompletionPoint{ completionValue },
            RetiredTexture{ descriptorSet, handle });
    }

    void SweepRetired(uint64_t completedValue)
    {
        retireQueue.Collect(RHICompletionPoint{ completedValue },
            [&](RetiredTexture& retired)
            {
                if (VK_NULL_HANDLE != retired.descriptorSet)
                    ImGui_ImplVulkan_RemoveTexture(retired.descriptorSet);
                if (retired.handle.IsValid()) resources.ReleaseTexture(retired.handle);
            });
    }

    void DestroyBackBufferViews()
    {
        if (VK_NULL_HANDLE != resources.GetDevice())
        {
            for (VkImageView view : backBufferViews)
            {
                if (VK_NULL_HANDLE != view)
                    VulkanApi::vkDestroyImageView(resources.GetDevice(), view, nullptr);
            }
        }
        backBufferViews.clear();
        backBufferInitialized.clear();
    }

    bool CreateBackBufferViews(std::string& outError)
    {
        DestroyBackBufferViews();
        const uint32_t count = resources.GetBackBufferCount();
        backBufferViews.resize(count, VK_NULL_HANDLE);
        backBufferInitialized.assign(count, false);

        for (uint32_t i = 0; i < count; ++i)
        {
            VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            info.image = resources.GetBackBuffer(i);
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = resources.GetBackBufferFormat();
            info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.levelCount = 1;
            info.subresourceRange.layerCount = 1;
            const VkResult result = VulkanApi::vkCreateImageView(
                resources.GetDevice(), &info, nullptr, &backBufferViews[i]);
            if (VK_SUCCESS != result)
            {
                outError = "ImGui Vulkan 백버퍼 뷰 생성 실패 — " +
                    VulkanApi::ResultToString(result);
                DestroyBackBufferViews();
                return false;
            }
        }
        return true;
    }

    VkDescriptorSet AddTexture(RHITextureHandle handle)
    {
        const VulkanImageEntry entry = resources.GetResourceTable().Resolve(handle);
        if (!entry.IsValid()) return VK_NULL_HANDLE;
        return ImGui_ImplVulkan_AddTexture(entry.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    bool EnsureFallback(std::string& outError)
    {
        if (VK_NULL_HANDLE != fallbackSet) return true;
        const RHITextureEntry black = textureCache.GetBlackTexture(outError);
        if (!black.IsValid()) return false;
        fallbackSet = AddTexture(black.handle);
        if (VK_NULL_HANDLE == fallbackSet)
        {
            outError = "ImGui Vulkan 폴백 descriptor set 생성 실패";
            return false;
        }
        return true;
    }

    bool UploadCpuFrame(uint64_t key, const CpuFrame& frame, std::string& outError)
    {
        CpuFrameEntry& entry = cpuFrames[key];
        const bool recreate = !entry.handle.IsValid() || entry.width != frame.width ||
            entry.height != frame.height;
        if (recreate)
        {
            CpuFrameEntry replacement{};
            RHITextureDesc desc{};
            desc.width = frame.width;
            desc.height = frame.height;
            desc.format = RHIFormat::RGBA8Unorm;
            desc.debugName = L"ImGuiVulkan.CpuFrame";
            if (!resources.CreateTexture(desc, replacement.handle, outError))
                return false;
            replacement.width = frame.width;
            replacement.height = frame.height;
            replacement.lastUsedFrame = entry.lastUsedFrame;
            if (VK_NULL_HANDLE != entry.descriptorSet || entry.handle.IsValid())
            {
                pendingFrameRetirements.push_back(
                    RetiredTexture{ entry.descriptorSet, entry.handle });
            }
            entry = replacement;
        }

        const uint64_t bytes = static_cast<uint64_t>(frame.width) * frame.height * 4u;
        const RHIBufferSlice upload = resources.AllocateUpload(
            RHIUploadRequest{ bytes, RHIUploadUsage::TextureCopy, 1 });
        if (!upload.IsWritable())
        {
            outError = "ImGui Vulkan CPU 프레임 업로드 공간 부족";
            return false;
        }
        std::memcpy(upload.cpuAddress, frame.rgba.data(), static_cast<size_t>(bytes));

        const RHITransition toCopy{ entry.handle,
            entry.initialized ? RHIResourceState::PixelShaderResource : RHIResourceState::Common,
            RHIResourceState::CopyDest };
        resources.TransitionResources({ &toCopy, 1 });

        const VulkanBufferEntry source = resources.GetResourceTable().Resolve(upload.buffer);
        const VulkanImageEntry destination = resources.GetResourceTable().Resolve(entry.handle);
        if (!source.IsValid() || !destination.IsValid())
        {
            outError = "ImGui Vulkan CPU 프레임 업로드 핸들을 풀지 못했다";
            return false;
        }

        VkBufferImageCopy copy{};
        copy.bufferOffset = upload.offset;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = { frame.width, frame.height, 1 };
        VulkanApi::vkCmdCopyBufferToImage(resources.GetCommandBuffer(), source.buffer,
            destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        const RHITransition toRead{ entry.handle, RHIResourceState::CopyDest,
            RHIResourceState::PixelShaderResource };
        resources.TransitionResources({ &toRead, 1 });
        entry.initialized = true;

        if (VK_NULL_HANDLE == entry.descriptorSet)
        {
            entry.descriptorSet = AddTexture(entry.handle);
            if (VK_NULL_HANDLE == entry.descriptorSet)
            {
                outError = "ImGui Vulkan CPU 프레임 descriptor set 생성 실패";
                return false;
            }
        }
        return true;
    }
};

ImGuiVulkanShell::ImGuiVulkanShell() : m_impl(new Impl()) {}
ImGuiVulkanShell::~ImGuiVulkanShell() { delete m_impl; }

bool ImGuiVulkanShell::IsActive() const { return m_impl->active; }

bool ImGuiVulkanShell::Initialize(void* windowHandle, uint32_t width, uint32_t height,
    std::string& outError)
{
    Impl& impl = *m_impl;
    if (impl.active) return true;

#if defined(_DEBUG)
    constexpr bool kEnableValidation = true;
#else
    constexpr bool kEnableValidation = false;
#endif
    if (!impl.resources.Initialize(width, height, kEnableValidation, outError)) return false;
    if (!impl.resources.AttachSwapChain(windowHandle, width, height, outError))
    {
        impl.resources.Shutdown();
        return false;
    }
    if (!impl.CreateBackBufferViews(outError))
    {
        impl.resources.Shutdown();
        return false;
    }
    if (!impl.textureCache.Initialize(&impl.resources, outError))
    {
        impl.DestroyBackBufferViews();
        impl.resources.Shutdown();
        return false;
    }

    const uint32_t imageCount = impl.resources.GetBackBufferCount();
    const VkFormat colorFormat = impl.resources.GetBackBufferFormat();
    ImGui_ImplVulkan_InitInfo init{};
    // VulkanDeviceResources의 VkApplicationInfo가 요청한 인스턴스 버전과 맞춘다.
    // physical-device 버전(예: 1.4)을 넣으면 backend가 인스턴스보다 새 함수를
    // 고를 수 있다.
    init.ApiVersion = VK_API_VERSION_1_3;
    init.Instance = impl.resources.GetInstance();
    init.PhysicalDevice = impl.resources.GetPhysicalDevice();
    init.Device = impl.resources.GetDevice();
    init.QueueFamily = impl.resources.GetQueueFamily();
    init.Queue = impl.resources.GetQueue();
    init.DescriptorPoolSize = kImGuiDescriptorPoolSize;
    init.MinImageCount = (std::max)(2u, (std::min)(imageCount, 3u));
    init.ImageCount = imageCount;
    init.UseDynamicRendering = true;
    init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    init.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    init.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.PipelineInfoForViewports.PipelineRenderingCreateInfo =
        init.PipelineInfoMain.PipelineRenderingCreateInfo;
    init.MinAllocationSize = 1024u * 1024u;

    // Win32 backend는 HWND 생성까지만 맡고 Vulkan surface 생성 함수는 앱이
    // 연결해야 한다. 이 훅이 없으면 ImGui_ImplVulkan_Init의 viewport 계약이
    // 즉시 assertion으로 실패한다.
    ImGui::GetPlatformIO().Platform_CreateVkSurface =
        [](ImGuiViewport* viewport, ImU64 instanceValue, const void* allocators,
            ImU64* outSurface) -> int
        {
            if (nullptr == viewport || nullptr == viewport->PlatformHandle ||
                nullptr == outSurface) return static_cast<int>(VK_ERROR_INITIALIZATION_FAILED);

            VkWin32SurfaceCreateInfoKHR surfaceInfo{
                VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
            surfaceInfo.hinstance = ::GetModuleHandleW(nullptr);
            surfaceInfo.hwnd = static_cast<HWND>(viewport->PlatformHandle);
            VkSurfaceKHR surface{ VK_NULL_HANDLE };
            const VkInstance instance = reinterpret_cast<VkInstance>(
                static_cast<uintptr_t>(instanceValue));
            const VkResult result = VulkanApi::vkCreateWin32SurfaceKHR(instance,
                &surfaceInfo, static_cast<const VkAllocationCallbacks*>(allocators), &surface);
            if (VK_SUCCESS == result) *outSurface = ToImTextureId(surface);
            return static_cast<int>(result);
        };

    if (!ImGui_ImplVulkan_Init(&init))
    {
        outError = "ImGui_ImplVulkan_Init 실패";
        impl.textureCache.Shutdown();
        impl.DestroyBackBufferViews();
        impl.resources.Shutdown();
        return false;
    }

    impl.width = width;
    impl.height = height;
    impl.active = true;
    return true;
}

void ImGuiVulkanShell::Resize(uint32_t width, uint32_t height)
{
    Impl& impl = *m_impl;
    if (!impl.active || 0 == width || 0 == height ||
        (width == impl.width && height == impl.height)) return;

    std::string error;
    if (!impl.resources.DrainForLifecycle(
            RHILifecycleCommand::SwapChainResize, error))
    {
        std::printf("[ImGui] Vulkan resize drain 실패: %s\n", error.c_str());
        return;
    }
    impl.DestroyBackBufferViews();
    if (!impl.resources.ResizeSwapChain(width, height, error) ||
        !impl.CreateBackBufferViews(error))
    {
        std::printf("[ImGui] Vulkan resize 실패: %s\n", error.c_str());
        return;
    }
    ImGui_ImplVulkan_SetMinImageCount((std::max)(2u,
        (std::min)(impl.resources.GetBackBufferCount(), 3u)));
    impl.width = width;
    impl.height = height;
}

void ImGuiVulkanShell::NewFrame()
{
    Impl& impl = *m_impl;
    if (!impl.active || impl.frameOpen) return;

    const uint64_t completed = impl.resources.GetCompletedFenceValue();
    impl.SweepRetired(completed);
    impl.textureCache.SweepGraveyard(completed);
    impl.textureCache.BeginFrame(impl.frameIndex);

    std::unordered_map<uint64_t, Impl::CpuFrame> pending;
    {
        std::lock_guard<std::mutex> lock(impl.cpuFrameMutex);
        pending.swap(impl.pendingCpuFrames);
    }

    std::string error;
    if (!impl.resources.BeginFrame(error))
    {
        std::printf("[ImGui] Vulkan BeginFrame 실패: %s\n", error.c_str());
        return;
    }
    impl.frameOpen = true;

    if (!impl.EnsureFallback(error))
        std::printf("[ImGui] Vulkan 폴백 텍스처 실패: %s\n", error.c_str());

    for (const auto& item : pending)
    {
        if (impl.cpuFrames.end() == impl.cpuFrames.find(item.first)) continue;
        error.clear();
        if (!impl.UploadCpuFrame(item.first, item.second, error))
            std::printf("[ImGui] Vulkan CPU 프레임 업로드 실패: %s\n", error.c_str());
    }

    ImGui_ImplVulkan_NewFrame();
}

bool ImGuiVulkanShell::RenderAndPresent(std::string& outError)
{
    Impl& impl = *m_impl;
    if (!impl.active) return true;
    if (!impl.frameOpen)
    {
        outError = "ImGui Vulkan 프레임이 열리지 않았다";
        return false;
    }

    const uint32_t index = impl.resources.GetBackBufferIndex();
    if (index >= impl.backBufferViews.size())
    {
        outError = "ImGui Vulkan 백버퍼 인덱스가 범위를 벗어났다";
        impl.resources.AbortFrame();
        impl.frameOpen = false;
        return false;
    }

    VkImageMemoryBarrier2 toRender{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    toRender.srcStageMask = impl.backBufferInitialized[index]
        ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_NONE;
    toRender.srcAccessMask = 0;
    toRender.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toRender.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toRender.oldLayout = impl.backBufferInitialized[index]
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    toRender.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toRender.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRender.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRender.image = impl.resources.GetBackBuffer(index);
    toRender.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toRender.subresourceRange.levelCount = 1;
    toRender.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toRender;
    VulkanApi::vkCmdPipelineBarrier2(impl.resources.GetCommandBuffer(), &dependency);

    VkRenderingAttachmentInfo color{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color.imageView = impl.backBufferViews[index];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = { { 0.06f, 0.06f, 0.08f, 1.0f } };
    VkRenderingInfo rendering{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    rendering.renderArea.extent = { impl.width, impl.height };
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    VulkanApi::vkCmdBeginRendering(impl.resources.GetCommandBuffer(), &rendering);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), impl.resources.GetCommandBuffer());
    VulkanApi::vkCmdEndRendering(impl.resources.GetCommandBuffer());

    VkImageMemoryBarrier2 toPresent = toRender;
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    dependency.pImageMemoryBarriers = &toPresent;
    VulkanApi::vkCmdPipelineBarrier2(impl.resources.GetCommandBuffer(), &dependency);
    impl.backBufferInitialized[index] = true;

    if (!impl.resources.EndFrame(outError))
    {
        impl.frameOpen = false;
        return false;
    }
    impl.frameOpen = false;

    const uint64_t completionValue = impl.resources.GetLastSignaledFenceValue();
    for (Impl::RetiredTexture& retired : impl.pendingFrameRetirements)
    {
        impl.RetireTexture(retired.descriptorSet, retired.handle,
            completionValue);
    }
    impl.pendingFrameRetirements.clear();
    for (auto it = impl.textureSets.begin(); it != impl.textureSets.end();)
    {
        if (!ImGuiTextureLifetimePolicy::ShouldRetire(
                impl.frameIndex, it->second.lastUsedFrame))
        {
            ++it;
            continue;
        }
        impl.RetireTexture(it->second.descriptorSet, {}, completionValue);
        it = impl.textureSets.erase(it);
    }
    for (auto it = impl.cpuFrames.begin(); it != impl.cpuFrames.end();)
    {
        if (!ImGuiTextureLifetimePolicy::ShouldRetire(
                impl.frameIndex, it->second.lastUsedFrame))
        {
            ++it;
            continue;
        }
        impl.RetireTexture(it->second.descriptorSet,
            it->second.handle, completionValue);
        it = impl.cpuFrames.erase(it);
    }
    const RHIDeviceMemoryPressureInfo pressureInfo = impl.resources
        .GetPersistentMemoryBudgetCoordinator().GetMemoryPressureInfo();
    RHIAssetEvictionPass evictionPass = BeginRHIAssetEvictionPass(
        pressureInfo.memoryPressure, pressureInfo.targetReleaseBytes);
    impl.textureCache.RetireUnused(completionValue, &evictionPass);
    ++impl.frameIndex;

#if defined(_DEBUG)
    std::string validation;
    if (0 != impl.resources.DrainDebugMessages(validation) && !validation.empty() &&
        impl.reportedValidation.insert(validation).second)
    {
        std::printf("[ImGui Vulkan 검증] %s\n", validation.c_str());
    }
#endif
    return impl.resources.Present(outError);
}

void ImGuiVulkanShell::RebuildFontAtlas()
{
    // ImGui 1.92의 Vulkan backend는 RendererHasTextures 경로에서 폰트 아틀라스
    // 변경을 NewFrame/RenderDrawData 사이에 갱신한다. 별도 device-object 파괴는
    // 오히려 이전 프레임 descriptor set 수명을 깨므로 여기서는 예약만 맡긴다.
}

uint64_t ImGuiVulkanShell::RegisterTexture(Texture* texture)
{
    Impl& impl = *m_impl;
    if (!impl.active || !impl.frameOpen) return GetFallbackTextureId();
    if (nullptr == texture) return GetFallbackTextureId();

    std::string error;
    const RHITextureEntry entry = impl.textureCache.GetOrUpload(texture, error);
    if (!entry.IsValid()) return GetFallbackTextureId();

    const uint64_t assetId = static_cast<uint64_t>(texture->m_assetId.m_ID_Data);
    const auto found = impl.textureSets.find(assetId);
    if (found != impl.textureSets.end())
    {
        found->second.lastUsedFrame = impl.frameIndex;
        return ToImTextureId(found->second.descriptorSet);
    }

    const VkDescriptorSet descriptor = impl.AddTexture(entry.handle);
    if (VK_NULL_HANDLE == descriptor) return GetFallbackTextureId();
    impl.textureSets.emplace(assetId,
        Impl::TextureSetEntry{ descriptor, impl.frameIndex });
    return ToImTextureId(descriptor);
}

uint64_t ImGuiVulkanShell::OpenSharedTexture(void* /*sharedHandle*/)
{
    // DXGI shared handle은 Vulkan descriptor가 아니다. Vulkan scene path는 공통
    // SubmitCpuRgbaFrame 브리지를 사용하며 이 호출은 합법 폴백으로 닫는다.
    return GetFallbackTextureId();
}

void ImGuiVulkanShell::SubmitCpuRgbaFrame(uint64_t key, uint32_t width,
    uint32_t height, const void* rgba, uint32_t rowPitch)
{
    Impl& impl = *m_impl;
    if (!impl.active || 0 == key || 0 == width || 0 == height || nullptr == rgba) return;
    const uint32_t tightPitch = width * 4u;
    if (rowPitch < tightPitch) return;

    Impl::CpuFrame frame{};
    frame.width = width;
    frame.height = height;
    frame.rgba.resize(static_cast<size_t>(tightPitch) * height);
    const uint8_t* source = static_cast<const uint8_t*>(rgba);
    for (uint32_t y = 0; y < height; ++y)
    {
        std::memcpy(frame.rgba.data() + static_cast<size_t>(y) * tightPitch,
            source + static_cast<size_t>(y) * rowPitch, tightPitch);
    }

    std::lock_guard<std::mutex> lock(impl.cpuFrameMutex);
    impl.pendingCpuFrames[key] = std::move(frame);
}

uint64_t ImGuiVulkanShell::GetCpuFrameTextureId(uint64_t key)
{
    Impl& impl = *m_impl;
    auto found = impl.cpuFrames.find(key);
    if (found == impl.cpuFrames.end())
    {
        if (!impl.active || !impl.frameOpen || 0 == key) return GetFallbackTextureId();
        Impl::CpuFrameEntry entry{};
        entry.lastUsedFrame = impl.frameIndex;
        found = impl.cpuFrames.emplace(key, std::move(entry)).first;
    }
    found->second.lastUsedFrame = impl.frameIndex;
    if (VK_NULL_HANDLE == found->second.descriptorSet) return GetFallbackTextureId();
    return ToImTextureId(found->second.descriptorSet);
}

uint64_t ImGuiVulkanShell::GetFallbackTextureId() const
{
    return ToImTextureId(m_impl->fallbackSet);
}

void ImGuiVulkanShell::Shutdown()
{
    Impl& impl = *m_impl;
    if (!impl.active) return;
    if (impl.frameOpen)
    {
        impl.resources.AbortFrame();
        impl.frameOpen = false;
    }
    std::string lifecycleError;
    if (!impl.resources.DrainForLifecycle(
            RHILifecycleCommand::BackendShutdown, lifecycleError))
    {
        std::printf("[ImGui] Vulkan shutdown drain 실패: %s\n",
            lifecycleError.c_str());
        std::string abandonError;
        impl.resources.DrainForLifecycle(
            RHILifecycleCommand::UnrecoverableDeviceError, abandonError);
    }
    impl.retireQueue.Drain([&](Impl::RetiredTexture& retired)
        {
            if (VK_NULL_HANDLE != retired.descriptorSet)
                ImGui_ImplVulkan_RemoveTexture(retired.descriptorSet);
            if (retired.handle.IsValid()) impl.resources.ReleaseTexture(retired.handle);
        });
    for (Impl::RetiredTexture& retired : impl.pendingFrameRetirements)
    {
        if (VK_NULL_HANDLE != retired.descriptorSet)
            ImGui_ImplVulkan_RemoveTexture(retired.descriptorSet);
        if (retired.handle.IsValid()) impl.resources.ReleaseTexture(retired.handle);
    }
    impl.pendingFrameRetirements.clear();
    impl.textureCache.SweepGraveyard(~uint64_t{ 0 });

    for (const auto& item : impl.textureSets)
        ImGui_ImplVulkan_RemoveTexture(item.second.descriptorSet);
    impl.textureSets.clear();
    for (auto& item : impl.cpuFrames)
    {
        if (VK_NULL_HANDLE != item.second.descriptorSet)
            ImGui_ImplVulkan_RemoveTexture(item.second.descriptorSet);
        impl.resources.ReleaseTexture(item.second.handle);
    }
    impl.cpuFrames.clear();
    if (VK_NULL_HANDLE != impl.fallbackSet)
    {
        ImGui_ImplVulkan_RemoveTexture(impl.fallbackSet);
        impl.fallbackSet = VK_NULL_HANDLE;
    }
    {
        std::lock_guard<std::mutex> lock(impl.cpuFrameMutex);
        impl.pendingCpuFrames.clear();
    }

    impl.textureCache.Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::GetPlatformIO().Platform_CreateVkSurface = nullptr;
    impl.DestroyBackBufferViews();
    impl.resources.Shutdown();
    impl.frameIndex = 0;
    impl.active = false;
}

#endif
