#include "../../Engine/Utility_Framework/StandardLibrary.h"
#include <iostream>

// Test visibility only; declarations and implementation bodies are unchanged.
#define private public
#include "../../Engine/RenderEngine/RHI/Vulkan/VulkanDeviceResources.h"
#include "../../Engine/RenderEngine/RHI/Vulkan/VulkanRecordingResources.h"
#include "../../Engine/RenderEngine/RHI/Vulkan/VulkanSoftwareCommandValidator.h"
#include "../../Engine/RenderEngine/RHI/Vulkan/VulkanCommandBufferPool.h"
#include "../../Engine/RenderEngine/RHI/Vulkan/VulkanCaptureGpuProfiler.h"
#include "../../Engine/RenderEngine/RHI/RHISoftwareRecorder.h"
#include "../../Engine/RenderEngine/RHI/IRHICommandTranslator.h"
#undef private

namespace
{
    std::atomic<uint64_t> checks{};
    std::atomic<uint64_t> heapAllocations{};
    thread_local bool count_allocations{};
    std::atomic<uint64_t> nextHandle{32};
    std::atomic<uint64_t> completedFence{};
    std::atomic<uint64_t> submittedFence{};
    std::atomic<uint64_t> draws{}, dispatches{}, queryWrites{}, queryResets{};
    std::atomic<uint64_t> poolCreates{}, poolDestroys{}, poolResets{}, descriptorAllocations{};
    std::atomic<uint64_t> destroyedImages{}, destroyedViews{}, destroyedBuffers{}, destroyedPipelines{};
    std::atomic<bool> failPoolCreate{}, failDescriptorAllocate{};
    std::mutex replayMutex;
    std::condition_variable replayWake;
    bool pauseReplay{}, replayEntered{};
    std::array<uint64_t, 65536> bufferBytes{};
    std::array<void*, 65536> memoryPointers{};
    std::array<uint64_t, 65536> memoryBytes{};
    std::array<std::atomic<uint32_t>, 65536> targetDispatches{};
    std::array<uint64_t, 8> submittedTargets{};
    std::atomic<uint32_t> submittedCount{};

    void require(bool value, const char* message)
    {
        checks.fetch_add(1);
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
    template<class T>
    T handle(uint64_t value)
    {
        return reinterpret_cast<T>(static_cast<uintptr_t>(value));
    }
    template<class T>
    uint64_t number(T value)
    {
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
    }
    template<class T>
    T make_handle()
    {
        return handle<T>(nextHandle.fetch_add(1));
    }

    void install_native_boundary()
    {
        using namespace VulkanApi;
        vkDeviceWaitIdle = [](VkDevice) { return VK_SUCCESS; };
        vkDestroyDevice = [](VkDevice, const VkAllocationCallbacks*) {};
        vkDestroyInstance = [](VkInstance, const VkAllocationCallbacks*) {};
        vkDestroySemaphore = [](VkDevice, VkSemaphore, const VkAllocationCallbacks*) {};
        vkGetPhysicalDeviceProperties = [](VkPhysicalDevice, VkPhysicalDeviceProperties* result)
        {
            *result = {};
            result->limits.minUniformBufferOffsetAlignment = 256;
            result->limits.minStorageBufferOffsetAlignment = 16;
            result->limits.optimalBufferCopyOffsetAlignment = 16;
            result->limits.timestampPeriod = 1;
        };
        vkGetPhysicalDeviceMemoryProperties = [](VkPhysicalDevice, VkPhysicalDeviceMemoryProperties* result)
        {
            *result = {};
            result->memoryTypeCount = 1;
            result->memoryTypes[0].propertyFlags =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            result->memoryHeapCount = 1;
            result->memoryHeaps[0].size = 1ull << 30;
        };
        vkGetPhysicalDeviceQueueFamilyProperties =
            [](VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* result)
        {
            if (!result)
            {
                *count = 1;
                return;
            }
            *result = {};
            result->timestampValidBits = 64;
            *count = 1;
        };
        vkCreateBuffer = [](VkDevice, const VkBufferCreateInfo* info, const VkAllocationCallbacks*, VkBuffer* result)
        {
            *result = make_handle<VkBuffer>();
            bufferBytes[number(*result)] = info->size;
            return VK_SUCCESS;
        };
        vkGetBufferMemoryRequirements = [](VkDevice, VkBuffer buffer, VkMemoryRequirements* result)
        { *result = {bufferBytes[number(buffer)], 256, 1}; };
        vkAllocateMemory =
            [](VkDevice, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks*, VkDeviceMemory* result)
        {
            *result = make_handle<VkDeviceMemory>();
            memoryBytes[number(*result)] = info->allocationSize;
            memoryPointers[number(*result)] = std::malloc(static_cast<size_t>(info->allocationSize));
            return memoryPointers[number(*result)] ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
        };
        vkBindBufferMemory = [](VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; };
        vkMapMemory =
            [](VkDevice, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize, VkMemoryMapFlags, void** result)
        {
            *result = static_cast<std::byte*>(memoryPointers[number(memory)]) + offset;
            return VK_SUCCESS;
        };
        vkUnmapMemory = [](VkDevice, VkDeviceMemory) {};
        vkFreeMemory = [](VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*)
        {
            std::free(memoryPointers[number(memory)]);
            memoryPointers[number(memory)] = nullptr;
        };
        vkDestroyBuffer = [](VkDevice, VkBuffer, const VkAllocationCallbacks*) { ++destroyedBuffers; };
        vkDestroyImage = [](VkDevice, VkImage, const VkAllocationCallbacks*) { ++destroyedImages; };
        vkDestroyImageView = [](VkDevice, VkImageView, const VkAllocationCallbacks*) { ++destroyedViews; };
        vkCreateImageView =
            [](VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* result)
        {
            *result = make_handle<VkImageView>();
            return VK_SUCCESS;
        };
        vkCreateDescriptorPool =
            [](VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* result)
        {
            if (failPoolCreate.exchange(false))
            {
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            *result = make_handle<VkDescriptorPool>();
            ++poolCreates;
            return VK_SUCCESS;
        };
        vkDestroyDescriptorPool = [](VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) { ++poolDestroys; };
        vkResetDescriptorPool = [](VkDevice, VkDescriptorPool, VkDescriptorPoolResetFlags)
        {
            ++poolResets;
            return VK_SUCCESS;
        };
        vkAllocateDescriptorSets = [](VkDevice, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* result)
        {
            if (failDescriptorAllocate.load())
            {
                return VK_ERROR_OUT_OF_POOL_MEMORY;
            }
            for (uint32_t index = 0; index < info->descriptorSetCount; ++index)
            {
                result[index] = make_handle<VkDescriptorSet>();
            }
            descriptorAllocations += info->descriptorSetCount;
            return VK_SUCCESS;
        };
        vkUpdateDescriptorSets =
            [](VkDevice, uint32_t count, const VkWriteDescriptorSet* writes, uint32_t, const VkCopyDescriptorSet*)
        {
            for (uint32_t index = 0; index < count; ++index)
            {
                require(writes[index].descriptorCount == 1 && writes[index].dstSet, "invalid descriptor write replay");
            }
        };
        vkCreateSampler = [](VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* result)
        {
            *result = make_handle<VkSampler>();
            return VK_SUCCESS;
        };
        vkDestroySampler = [](VkDevice, VkSampler, const VkAllocationCallbacks*) {};
        vkDestroyPipeline = [](VkDevice, VkPipeline, const VkAllocationCallbacks*) { ++destroyedPipelines; };
        vkDestroyPipelineLayout = [](VkDevice, VkPipelineLayout, const VkAllocationCallbacks*) {};
        vkDestroyDescriptorSetLayout = [](VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) {};
        vkCreateCommandPool =
            [](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* result)
        {
            *result = make_handle<VkCommandPool>();
            return VK_SUCCESS;
        };
        vkDestroyCommandPool = [](VkDevice, VkCommandPool, const VkAllocationCallbacks*) {};
        vkAllocateCommandBuffers = [](VkDevice, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* result)
        {
            for (uint32_t index = 0; index < info->commandBufferCount; ++index)
            {
                result[index] = make_handle<VkCommandBuffer>();
            }
            return VK_SUCCESS;
        };
        vkResetCommandPool = [](VkDevice, VkCommandPool, VkCommandPoolResetFlags) { return VK_SUCCESS; };
        vkBeginCommandBuffer = [](VkCommandBuffer command, const VkCommandBufferBeginInfo*)
        {
            std::unique_lock lock(replayMutex);
            if (pauseReplay)
            {
                replayEntered = true;
                replayWake.notify_all();
                require(replayWake.wait_for(lock, std::chrono::seconds(5), [] { return !pauseReplay; }),
                        "producer failed to release native replay gate");
            }
            targetDispatches[number(command)] = 0;
            return VK_SUCCESS;
        };
        vkEndCommandBuffer = [](VkCommandBuffer) { return VK_SUCCESS; };
        vkCmdBindPipeline = [](VkCommandBuffer, VkPipelineBindPoint, VkPipeline) {};
        vkCmdSetViewport = [](VkCommandBuffer, uint32_t, uint32_t, const VkViewport*) {};
        vkCmdSetScissor = [](VkCommandBuffer, uint32_t, uint32_t, const VkRect2D*) {};
        vkCmdSetPrimitiveTopology = [](VkCommandBuffer, VkPrimitiveTopology) {};
        vkCmdBindVertexBuffers = [](VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*) {};
        vkCmdBindIndexBuffer = [](VkCommandBuffer, VkBuffer, VkDeviceSize, VkIndexType) {};
        vkCmdBindDescriptorSets = [](VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t,
                                     const VkDescriptorSet*, uint32_t, const uint32_t*) {};
        vkCmdBeginRendering = [](VkCommandBuffer, const VkRenderingInfo*) {};
        vkCmdEndRendering = [](VkCommandBuffer) {};
        vkCmdClearAttachments = [](VkCommandBuffer, uint32_t, const VkClearAttachment*, uint32_t,
                                   const VkClearRect*) {};
        vkCmdPipelineBarrier2 = [](VkCommandBuffer, const VkDependencyInfo*) {};
        vkCmdCopyBuffer = [](VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy*) {};
        vkCmdCopyImage = [](VkCommandBuffer, VkImage, VkImageLayout, VkImage, VkImageLayout, uint32_t,
                            const VkImageCopy*) {};
        vkCmdClearColorImage = [](VkCommandBuffer, VkImage, VkImageLayout, const VkClearColorValue*, uint32_t,
                                  const VkImageSubresourceRange*) {};
        vkCmdCopyImageToBuffer = [](VkCommandBuffer, VkImage, VkImageLayout, VkBuffer, uint32_t,
                                    const VkBufferImageCopy*) {};
        vkCmdDraw = [](VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t) { ++draws; };
        vkCmdDrawIndexed = [](VkCommandBuffer, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) { ++draws; };
        vkCmdDrawIndirect = [](VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t) { ++draws; };
        vkCmdDrawIndexedIndirect = [](VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t) { ++draws; };
        vkCmdDispatch = [](VkCommandBuffer command, uint32_t, uint32_t, uint32_t)
        {
            ++dispatches;
            ++targetDispatches[number(command)];
        };
        vkCmdDispatchIndirect = [](VkCommandBuffer command, VkBuffer, VkDeviceSize)
        {
            ++dispatches;
            ++targetDispatches[number(command)];
        };
        vkCmdDrawMeshTasksEXT = [](VkCommandBuffer, uint32_t, uint32_t, uint32_t) { ++draws; };
        vkCmdDrawMeshTasksIndirectEXT = [](VkCommandBuffer, VkBuffer, VkDeviceSize, uint32_t, uint32_t) { ++draws; };
        vkQueueSubmit2 = [](VkQueue, uint32_t count, const VkSubmitInfo2* submits, VkFence)
        {
            require(GetRHISubmissionThread().IsCurrentThread(), "Vulkan queue escaped RHI submission owner");
            require(count == 1 && submits->commandBufferInfoCount <= 8, "invalid submitted targets");
            submittedCount = submits->commandBufferInfoCount;
            for (uint32_t index = 0; index < submits->commandBufferInfoCount; ++index)
            {
                submittedTargets[index] = number(submits->pCommandBufferInfos[index].commandBuffer);
            }
            submittedFence = submits->pSignalSemaphoreInfos[0].value;
            return VK_SUCCESS;
        };
        vkCreateQueryPool =
            [](VkDevice, const VkQueryPoolCreateInfo*, const VkAllocationCallbacks*, VkQueryPool* result)
        {
            *result = make_handle<VkQueryPool>();
            return VK_SUCCESS;
        };
        vkDestroyQueryPool = [](VkDevice, VkQueryPool, const VkAllocationCallbacks*) {};
        vkCmdResetQueryPool = [](VkCommandBuffer, VkQueryPool, uint32_t, uint32_t count)
        {
            require(count == 2, "timestamp reset lost paired query count");
            ++queryResets;
        };
        vkCmdWriteTimestamp = [](VkCommandBuffer, VkPipelineStageFlagBits, VkQueryPool, uint32_t) { ++queryWrites; };
    }
} // namespace

[[gnu::noinline]] void* operator new(std::size_t bytes)
{
    if (count_allocations)
    {
        ++heapAllocations;
    }
    if (void* result = std::malloc(bytes ? bytes : 1))
    {
        return result;
    }
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
[[gnu::noinline]] void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace VulkanApi
{
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr{};
#define DEFINE_VULKAN_ENTRY(name) PFN_##name name{};
    VK_GLOBAL_FUNCTIONS(DEFINE_VULKAN_ENTRY)
    VK_INSTANCE_FUNCTIONS(DEFINE_VULKAN_ENTRY)
    VK_DEVICE_FUNCTIONS(DEFINE_VULKAN_ENTRY)
    VK_OPTIONAL_DEVICE_FUNCTIONS(DEFINE_VULKAN_ENTRY)
#undef DEFINE_VULKAN_ENTRY
    std::string ResultToString(VkResult value)
    {
        return std::to_string(value);
    }
    bool LoadLoader(std::string&)
    {
        return true;
    }
} // namespace VulkanApi

// Mock only physical device/WSI boundaries. Admission and native submit methods
// below are extracted verbatim from VulkanDeviceResources.cpp by the runner.
VulkanDeviceResources::~VulkanDeviceResources() = default;
bool VulkanDeviceResources::CreateInstance(bool, std::string&)
{
    m_instance = make_handle<VkInstance>();
    return true;
}
bool VulkanDeviceResources::PickPhysicalDevice(std::string&)
{
    m_physicalDevice = make_handle<VkPhysicalDevice>();
    return true;
}
bool VulkanDeviceResources::CreateDevice(std::string&)
{
    m_device = make_handle<VkDevice>();
    m_queue = make_handle<VkQueue>();
    m_queueFamily = 0;
    return true;
}
bool VulkanDeviceResources::CreateFrameResources(std::string&)
{
    m_timeline = make_handle<VkSemaphore>();
    return true;
}
void VulkanDeviceResources::DestroySwapChain() {}
void VulkanDeviceResources::RefreshUploadBudget() {}
void VulkanDeviceResources::RefreshPersistentMemoryBudgets() {}
bool VulkanDeviceResources::FlushCommandList(std::string&)
{
    return true;
}
uint64_t VulkanDeviceResources::GetCompletedFenceValue() const
{
    return completedFence.load();
}
void VulkanDeviceResources::AccumulateEncoderDiagnostics() {}
bool VulkanDeviceResources::DrainForLifecycle(RHILifecycleCommand, std::string&)
{
    return true;
}
using namespace VulkanApi;
#include "vulkan_device_boundary.inc"

namespace
{
    struct DeviceFixture
    {
        VulkanDeviceResources device;
        VulkanPipelineCache pipelines;
        VulkanCommandBufferPool producer;
        RHIPipelineHandle graphics, compute, mesh;
        RHIPipelineLayoutHandle layout;
        RHIBufferHandle arguments;
        RHITextureHandle image;
        RHIRenderTargetBinding targets;
        std::string error;
        explicit DeviceFixture(uint32_t workers = 8)
        {
            device.m_device = handle<VkDevice>(1);
            device.m_deviceIncarnation = AllocateRHIDeviceIncarnation();
            device.m_physicalDevice = handle<VkPhysicalDevice>(2);
            device.m_queue = handle<VkQueue>(3);
            device.m_timeline = handle<VkSemaphore>(4);
            device.m_queueFamily = 0;
            device.m_frameOpen = true;
            device.m_viewportLimits = {4096, 4096, -8192.f, 8192.f};
            device.m_indirectDrawCapabilities = {true, true, true};
            device.m_meshShaderCapabilities = {true, true, 65535, 65535, 65535, 1ull << 30};
            device.m_currentRecordingId = 41;
            device.m_nextRecordingId = 42;
            RHIUploadSegmentPolicy policy;
            policy.regularSegmentBytes = 4096;
            policy.largeThreshold = 2048;
            policy.standbyRegularSegments = 0;
            policy.trimDelayCollects = 0;
            require(device.m_uploadAllocator.Initialize(device.GetDevice(), device.GetPhysicalDevice(),
                                                        device.m_resourceTable, policy, error),
                    "upload initialization failed");
            device.m_uploadAllocator.BeginRecording(41);
            require(device.m_descriptorRecycler.Initialize(device.GetDevice(), 1, error, 1000000),
                    "descriptor initialization failed");
            require(device.m_descriptorRecycler.BeginRecording(device.GetDevice(), 41, error),
                    "descriptor begin failed");
            pipelines.Initialize(device.GetDevice());
            device.m_pipelineCache = &pipelines;
            VulkanPipelineLayoutEntry entry;
            entry.layout = make_handle<VkPipelineLayout>();
            entry.setLayout = make_handle<VkDescriptorSetLayout>();
            entry.paramSlots.push_back({0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, false});
            entry.paramSlots.push_back({1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, false});
            entry.paramSlots.push_back({100, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, false});
            entry.paramSlots.push_back({101, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, false});
            entry.paramSlots.push_back({300, 1, VK_DESCRIPTOR_TYPE_SAMPLER, false});
            pipelines.m_layouts.push_back(std::move(entry));
            layout = {RHIHandleBits::Encode(0, 0)};
            const auto add = [&](VulkanPipelineKind kind)
            {
                const auto& l = pipelines.m_layouts[0];
                auto value =
                    pipelines.PublishPipeline({make_handle<VkPipeline>(), l.layout, l.setLayout, layout, kind});
                pipelines.m_pipelineByHash.emplace(value.id, value);
                return value;
            };
            graphics = add(VulkanPipelineKind::Graphics);
            compute = add(VulkanPipelineKind::Compute);
            mesh = add(VulkanPipelineKind::Mesh);
            VulkanBufferEntry buffer;
            buffer.buffer = make_handle<VkBuffer>();
            buffer.bytes = 4096;
            buffer.allowIndirectArguments = true;
            buffer.allowUnorderedAccess = true;
            arguments = device.m_resourceTable.AddBuffer(buffer);
            VulkanImageEntry texture;
            texture.image = make_handle<VkImage>();
            texture.view = make_handle<VkImageView>();
            texture.width = 64;
            texture.height = 64;
            texture.format = RHIFormat::RGBA8Unorm;
            texture.depthOrArraySize = 6;
            image = device.m_resourceTable.AddImage(texture);
            VulkanRenderTargetBinding target;
            target.colorCount = 1;
            target.colorViews[0] = make_handle<VkImageView>();
            target.width = 64;
            target.height = 64;
            device.m_renderTargetTables[0].Own(target.colorViews[0]);
            targets = {device.m_renderTargetTables[0].Add(target), 1, false};
            require(producer.Initialize(device, workers, 3, error), "producer pool initialization failed");
            producer.BeginFrame(0);
        }
        ~DeviceFixture()
        {
            producer.Shutdown();
            device.m_encoder.reset();
            device.m_uploadAllocator.Shutdown(device.GetDevice());
            device.m_descriptorRecycler.Shutdown(device.GetDevice());
            for (auto& table : device.m_renderTargetTables)
            {
                table.Reset(device.GetDevice());
            }
            device.m_samplerTable.Shutdown(device.GetDevice());
            device.m_resourceTable.Shutdown(device.GetDevice());
            pipelines.Shutdown();
        }
        RHIBufferSlice Upload()
        {
            RHIUploadRequest request{64, RHIUploadUsage::ConstantBuffer, 1};
            RHIBufferSlice output;
            require(
                device.m_uploadAllocator.ReserveBatch(device.m_currentRecordingId, {&request, 1}, {&output, 1}, error),
                "upload reserve failed");
            std::memset(output.cpuAddress, 0x5a, 64);
            return output;
        }
    };

    void verify_device_incarnations()
    {
        VulkanDeviceResources first, second;
        std::string error;
        require(first.Initialize(64, 64, false, error) && second.Initialize(64, 64, false, error),
                "device identity initialization failed");
        const auto old = first.GetGraphicsQueueIdentity();
        const auto other = second.GetGraphicsQueueIdentity();
        require(old.IsValid() && other.IsValid() && old != other, "foreign devices share a queue identity");
        first.Shutdown();
        require(!first.GetGraphicsQueueIdentity().IsValid(), "shutdown retained a valid native queue identity");
        require(first.Initialize(64, 64, false, error), "device reinitialization failed");
        require(first.GetGraphicsQueueIdentity().IsValid() && first.GetGraphicsQueueIdentity() != old &&
                    first.GetGraphicsQueueIdentity() != other,
                "reinitialized device reused its former queue identity");
        first.Shutdown();
        second.Shutdown();
    }

    void verify_validator()
    {
        DeviceFixture fixture;
        VulkanSoftwareCommandValidator validator(fixture.device);
        RHISoftwareCommandList list(1 << 20, 1 << 20);
        RHISoftwareEncoder software(list, validator);
        software.SetPipeline(RHIBindPoint::Graphics, fixture.graphics);
        auto before = list.GetCommandCount();
        require(!software.DrawIndirect(fixture.arguments, 0), "draw outside raster was accepted");
        require(list.GetCommandCount() == before, "false bool operation appended a command");
        require(!software.BeginRasterWithoutAttachments(64, 64), "unsupported attachment-free raster was accepted");
        require(!software.DispatchMeshIndirectBatch(fixture.arguments, 0, 1, {}),
                "unsupported mesh batch was accepted");
        software.BindRenderTargets(fixture.targets);
        require(software.DrawIndirect(fixture.arguments, 0), "valid indirect draw rejected");
        before = list.GetCommandCount();
        require(!software.DrawIndirect(fixture.arguments, 4096) && !software.DrawIndirect(fixture.arguments, 1),
                "invalid indirect bounds accepted");
        require(list.GetCommandCount() == before, "failed draw changed command count");
        software.SetPipeline(RHIBindPoint::Compute, fixture.compute);
        require(software.DrawIndirect(fixture.arguments, 0),
                "interleaved compute binding erased the graphics pipeline");
        require(!software.DispatchIndirect(fixture.arguments, 0), "compute dispatch inside raster was accepted");
        software.UavBarrierBuffers({&fixture.arguments, 1});
        require(software.DispatchIndirect(fixture.arguments, 0), "UAV barrier failed to close raster state");
        software.SetPipeline(RHIBindPoint::Graphics, fixture.mesh);
        software.BindRenderTargets(fixture.targets);
        require(software.DispatchMesh(1, 2, 3), "valid mesh dispatch rejected");
        require(!software.DispatchMesh(65536, 1, 1), "mesh limits ignored");
        software.ClearRenderTargets(fixture.targets, std::array<float, 4>{}.data());
        require(software.DispatchMeshIndirect(fixture.arguments, 0), "clear incorrectly ended raster scope");
        require(software.SetViewport(-2, -2, 4096, 4096), "valid viewport rejected");
        require(!software.SetViewport(0, 0, 4097, 32), "viewport device limit ignored");
        require(!software.SetViewport(std::numeric_limits<float>::infinity(), 0, 1, 1), "nonfinite viewport accepted");
        require(!software.SetScissor(UINT32_MAX, 0, 1, 1), "scissor overflow accepted");
        const auto upload = fixture.Upload();
        RHIBufferSlice copy{fixture.arguments, 0, upload.size, nullptr};
        require(software.CopyBuffer(copy, upload), "valid copy rejected");
        software.SetPipeline(RHIBindPoint::Compute, fixture.compute);
        require(software.DispatchIndirect(fixture.arguments, 0), "successful copy did not close raster");
        require(list.Seal(fixture.error), "validator recording did not seal");
        require(fixture.producer.OpenWorker(0, fixture.error), "native target open failed");
        auto& native = fixture.producer.AcquireEncoder(0);
        RHICommandTranslationStats stats;
        RHICommandFailure failure;
        require(list.Replay(native, stats, failure), "accepted bool command differed on native replay");
        require(fixture.producer.CloseAll(fixture.error), "native target close failed");
        require(fixture.producer.DrainEncoderDrops(fixture.error) == 0, "valid native replay dropped commands");
    }

    void verify_snapshot_and_leases()
    {
        DeviceFixture fixture;
        const auto upload = fixture.Upload();
        RHIBindingDesc desc;
        desc.dim = RHIBindingDesc::Dim::Buffer;
        desc.bufferResource = fixture.arguments;
        desc.structureByteStride = 4;
        desc.numElements = 8;
        const auto binding = fixture.device.m_bindingTable.Add({&desc, 1});
        const auto cube = fixture.device.m_resourceTable.GetOrCreateCubeView(fixture.device.GetDevice(), fixture.image,
                                                                             RHIFormat::Unknown, 0, 1, 0);
        require(cube != VK_NULL_HANDLE, "initial cube view creation failed");
        VulkanRecordingResources snapshot;
        require(snapshot.Capture(fixture.device, fixture.error), "snapshot capture failed");
        const auto viewsAtCapture = destroyedViews.load();
        require(snapshot.ResolveCubeView(fixture.image, RHIFormat::Unknown, 0, 1, 0) == cube,
                "cube metadata was not frozen");
        require(!fixture.device.m_resourceTable.GetOrCreateCubeView(fixture.device.GetDevice(), fixture.image,
                                                                    RHIFormat::Unknown, 0, 1, 6) &&
                    destroyedViews == viewsAtCapture,
                "incompatible cube request replaced a leased view");
        const auto originalPipeline = snapshot.Resolve(fixture.graphics);
        const auto originalBuffer = snapshot.Resolve(fixture.arguments);
        const auto originalImage = snapshot.Resolve(fixture.image);
        const auto originalTarget = snapshot.ResolveTargets(fixture.targets.backend);
        const auto imagesBefore = destroyedImages.load(), buffersBefore = destroyedBuffers.load();
        const auto viewsBefore = destroyedViews.load(), pipelinesBefore = destroyedPipelines.load();
        fixture.device.m_bindingTable.Reset();
        desc.numElements = 1;
        fixture.device.m_bindingTable.Add({&desc, 1});
        fixture.device.m_renderTargetTables[0].Reset(fixture.device.GetDevice());
        fixture.device.m_resourceTable.Release(fixture.device.GetDevice(), fixture.arguments);
        fixture.device.m_resourceTable.Release(fixture.device.GetDevice(), fixture.image);
        require(fixture.pipelines.InvalidatePipeline(fixture.graphics, {7}), "pipeline invalidation failed");
        fixture.pipelines.CollectRetiredPipelines({7});
        fixture.device.m_resourceTable.CollectRecordingLeases(fixture.device.GetDevice());
        require(destroyedImages == imagesBefore && destroyedBuffers == buffersBefore && destroyedViews == viewsBefore &&
                    destroyedPipelines == pipelinesBefore,
                "frozen native lease was destroyed by owner mutation");
        require(snapshot.Resolve(fixture.graphics).pipeline == originalPipeline.pipeline,
                "pipeline snapshot lost generation");
        require(snapshot.Resolve(fixture.arguments).buffer == originalBuffer.buffer, "buffer snapshot changed");
        require(snapshot.Resolve(fixture.image).image == originalImage.image, "image snapshot changed");
        require(snapshot.ResolveTargets(fixture.targets.backend).colorViews[0] == originalTarget.colorViews[0],
                "render target snapshot changed");
        require(snapshot.Resolve(binding).size() == 1 && snapshot.Resolve(binding)[0].numElements == 8,
                "binding snapshot reread mutable table");
        RHIUploadRequest request{64, RHIUploadUsage::ConstantBuffer, 1};
        RHIBufferSlice next;
        require(!fixture.device.m_uploadAllocator.ReserveBatch(41, {&request, 1}, {&next, 1}, fixture.error),
                "sealed upload recording accepted mutation");
        fixture.device.m_uploadAllocator.AbortRecording(41);
        fixture.device.m_uploadAllocator.BeginRecording(42);
        require(fixture.device.m_uploadAllocator.ReserveBatch(42, {&request, 1}, {&next, 1}, fixture.error),
                "new upload recording rejected");
        require(next.buffer != upload.buffer && *static_cast<const uint8_t*>(upload.cpuAddress) == 0x5a,
                "rejected pinned upload was reused");
        snapshot.Reset();
        fixture.device.m_resourceTable.CollectRecordingLeases(fixture.device.GetDevice());
        fixture.pipelines.CollectRetiredPipelines({7});
        fixture.device.m_renderTargetTables[0].Reset(fixture.device.GetDevice());
        require(destroyedImages == imagesBefore + 1 && destroyedBuffers == buffersBefore + 1 &&
                    destroyedPipelines == pipelinesBefore + 1 && destroyedViews >= viewsBefore + 2,
                "unpin failed to retire native objects");
    }

    void verify_descriptor_budget()
    {
        const auto device = handle<VkDevice>(1);
        std::string error;
        VulkanDescriptorPoolRecycler source, frozen;
        require(source.Initialize(device, 1, error, 1000000), "source descriptor init failed");
        auto budget = source.GetPoolBudget();
        const auto initial = budget->m_allocated.load();
        require(initial != 0, "descriptor budget did not account native pool");
        failPoolCreate = true;
        require(!frozen.Initialize(device, 1, error, 1000000, budget), "injected pool failure ignored");
        require(budget->m_allocated == initial, "failed native pool leaked descriptor budget");
        require(frozen.Initialize(device, 1, error, 1000000, budget), "partial initialization retry failed");
        require(budget->m_allocated == initial * 2, "private pools did not share device budget");
        frozen.Shutdown(device);
        require(budget->m_allocated == initial, "private pool teardown lost budget credit");
        VulkanDescriptorPoolRecycler limited;
        auto tiny = std::make_shared<VulkanDescriptorPoolBudget>();
        tiny->m_limit = static_cast<uint32_t>(initial);
        require(limited.Initialize(device, 1, error, tiny->m_limit, tiny), "single exact budget pool rejected");
        require(!frozen.Initialize(device, 1, error, tiny->m_limit, tiny), "device-global descriptor limit exceeded");
        require(tiny->m_allocated == initial, "budget rejection changed credits");
        limited.Shutdown(device);
        source.Shutdown(device);
        require(tiny->m_allocated == 0 && budget->m_allocated == 0, "descriptor budget not returned on shutdown");
    }

    void verify_warm_storage()
    {
        DeviceFixture fixture;
        fixture.Upload();
        RHIBindingDesc view;
        view.dim = RHIBindingDesc::Dim::Buffer;
        view.bufferResource = fixture.arguments;
        view.numElements = 16;
        view.structureByteStride = 4;
        fixture.device.m_bindingTable.Add({&view, 1});
        VulkanRecordingResources snapshot;
        require(snapshot.Capture(fixture.device, fixture.error), "warm snapshot setup failed");
        snapshot.Reset();
        const auto before = heapAllocations.load();
        count_allocations = true;
        for (uint32_t index = 0; index < 128; ++index)
        {
            fixture.device.m_bindingTable.Reset();
            fixture.device.m_bindingTable.Add({&view, 1});
            require(snapshot.Capture(fixture.device, fixture.error), "warm snapshot capture failed");
            snapshot.Reset();
        }
        count_allocations = false;
        require(heapAllocations == before, "warm immutable snapshot/binding reuse allocated");
    }

    void verify_translation_and_submission(uint32_t workers)
    {
        completedFence = 0;
        submittedCount = 0;
        submittedFence = 0;
        DeviceFixture fixture(workers);
        auto& thread = GetRHISubmissionThread();
        require(thread.AcquireClient(&fixture.device, fixture.error), "RHI owner registration failed");
        const uint64_t generation = thread.GetOwnerGeneration(&fixture.device);
        const auto queue = fixture.device.GetGraphicsQueueIdentity();
        VulkanSoftwareCommandValidator validator(fixture.device);
        RHISoftwareRecorder recorder(validator);
        require(recorder.BeginRecording({queue, workers, 4 << 20, 4 << 20}, fixture.error),
                "software recorder begin failed");
        const auto upload = fixture.Upload();
        RHIBindingDesc bufferView;
        bufferView.dim = RHIBindingDesc::Dim::Buffer;
        bufferView.bufferResource = fixture.arguments;
        bufferView.structureByteStride = 4;
        bufferView.numElements = 8;
        const auto buffers = fixture.device.m_bindingTable.Add({&bufferView, 1});
        RHIBindingDesc imageView;
        imageView.resource = fixture.image;
        const auto images = fixture.device.m_bindingTable.Add({&imageView, 1});
        const RHISamplerDesc samplerDesc{};
        const auto samplers = fixture.device.m_samplerTable.Add(fixture.device.GetDevice(), {&samplerDesc, 1});
        std::array<uint32_t, 8> order{};
        for (uint32_t index = 0; index < workers; ++index)
        {
            order[index] = workers - index - 1;
            auto& encoder = recorder.AcquireEncoder(index);
            encoder.SetPipeline(RHIBindPoint::Compute, fixture.compute);
            encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, upload);
            encoder.SetBindings(RHIBindPoint::Compute, 2, buffers);
            encoder.SetBindings(RHIBindPoint::Compute, 3, images);
            encoder.SetSamplers(RHIBindPoint::Compute, 4, samplers);
            for (uint32_t command = 0; command < 300 + index; ++command)
            {
                encoder.Dispatch(1, 1, 1);
            }
        }
        RHISoftwareBatchDesc desc;
        desc.lifetimeToken = std::make_shared<uint8_t>();
        desc.frameId = 7;
        desc.backendGeneration = generation;
        desc.queue = queue;
        desc.uploadRecordingId = fixture.device.GetCurrentUploadRecordingId();
        desc.descriptorVersion = fixture.device.GetDescriptorVersionToken();
        RHISoftwareBatch batch;
        require(recorder.FinishRecording({order.data(), workers}, desc, batch, fixture.error), "software seal failed");
        std::shared_ptr<VulkanCommandBufferPool> frozen;
        require(fixture.producer.CreateTranslationPool(frozen, fixture.error), "frozen pool creation failed");
        uint64_t recording{}, version{};
        require(frozen->GetTranslationRecording(recording, version) && recording == desc.uploadRecordingId &&
                    version == desc.descriptorVersion,
                "frozen recording identity mismatch");
        const auto address = frozen.get();
        RHISubmissionTicket ticket;
        auto translator = frozen->GetTranslator(queue);
        std::weak_ptr<const void> lifetime = desc.lifetimeToken;
        desc.lifetimeToken.reset();
        {
            const std::lock_guard lock(replayMutex);
            pauseReplay = true;
            replayEntered = false;
        }
        require(thread.EnqueueSoftwareBatch(&fixture.device, fixture.device, frozen, translator, std::move(batch),
                                            ticket, fixture.error),
                "software admission failed");
        {
            std::unique_lock lock(replayMutex);
            require(replayWake.wait_for(lock, std::chrono::seconds(5), [] { return replayEntered; }),
                    "translation never reached the native replay boundary");
        }
        require(fixture.device.GetCurrentUploadRecordingId() != recording, "admission failed to advance producer");
        fixture.device.m_bindingTable.Reset();
        fixture.device.m_renderTargetTables[0].Reset(fixture.device.GetDevice());
        fixture.device.m_resourceTable.Release(fixture.device.GetDevice(), fixture.arguments);
        fixture.device.m_resourceTable.Release(fixture.device.GetDevice(), fixture.image);
        {
            const std::lock_guard lock(replayMutex);
            pauseReplay = false;
        }
        replayWake.notify_all();
        require(thread.Wait(ticket, fixture.error), "actual RHI software translation failed");
        require(!lifetime.expired(), "CPU replay released the graph owner before GPU completion");
        require(thread.GetOwnerStats(&fixture.device).pendingRetirements != 0,
                "CPU completion incorrectly retired an incomplete GPU lease");
        require(submittedCount == workers && submittedFence == ticket.GetIdentity().completion.value,
                "software submission lost native completion identity");
        for (uint32_t index = 0; index < workers; ++index)
        {
            const auto command = frozen->m_slots[0][order[index]].buffer;
            require(submittedTargets[index] == number(command), "native target order changed");
            require(targetDispatches[number(command)] == 300 + order[index], "parallel target replay lost commands");
        }
        std::shared_ptr<VulkanCommandBufferPool> whilePending;
        require(fixture.producer.CreateTranslationPool(whilePending, fixture.error),
                "second frozen pool creation failed");
        require(whilePending.get() != address, "pending CPU/GPU pool was reused");
        whilePending.reset();
        completedFence = ticket.GetIdentity().completion.value;
        require(thread.Drain(&fixture.device, fixture.error), "CPU/GPU fixture retirement failed");
        frozen.reset();
        translator.reset();
        ticket = {};
        thread.CollectCompletedLifetimes(&fixture.device);
        require(thread.GetOwnerStats(&fixture.device).pendingRetirements == 0,
                "submission lifetime remained after completion");
        require(lifetime.expired(), "GPU retirement retained the completed graph owner");
        const auto reusable = fixture.producer.m_translationPools[0].get();
        std::shared_ptr<VulkanCommandBufferPool> reused;
        require(fixture.producer.CreateTranslationPool(reused, fixture.error) && reused.get() == reusable,
                "exclusive completed translation pool was not reused");
        thread.ReleaseClient(&fixture.device);
    }

    void verify_fork_and_descriptor_failure()
    {
        DeviceFixture fixture(1);
        const auto upload = fixture.Upload();
        const auto budget = fixture.device.m_descriptorRecycler.GetPoolBudget();
        const auto credits = budget->m_allocated.load();
        std::shared_ptr<VulkanCommandBufferPool> frozen;
        failPoolCreate = true;
        require(!fixture.producer.CreateTranslationPool(frozen, fixture.error) && !frozen,
                "failed private descriptor initialization published a partial fork");
        require(budget->m_allocated == credits, "failed frozen fork leaked device descriptor credits");
        require(fixture.producer.CreateTranslationPool(frozen, fixture.error), "frozen fork retry failed");
        const auto identity = fixture.device.GetGraphicsQueueIdentity();
        const RHIQueueIdentity foreign{AllocateRHIDeviceIncarnation(), 1, RHIQueueKind::Graphics};
        auto oldTranslator = frozen->GetTranslator(identity);
        require(!frozen->GetTranslator(foreign) && oldTranslator, "frozen pool accepted a foreign device queue");
        fixture.device.m_deviceIncarnation = AllocateRHIDeviceIncarnation();
        uint64_t recording{}, version{};
        require(!frozen->GetTranslator(identity) && !frozen->GetTranslator(fixture.device.GetGraphicsQueueIdentity()) &&
                    frozen->GetTranslationRecording(recording, version) && !recording && !version,
                "stale frozen pool accepted a new device incarnation");
        VulkanSoftwareCommandValidator validator(fixture.device);
        RHISoftwareRecorder recorder(validator);
        require(recorder.BeginRecording({identity, 1, 1 << 20, 1 << 20}, fixture.error),
                "stale identity recorder setup failed");
        auto& staleEncoder = recorder.AcquireEncoder(0);
        staleEncoder.SetPipeline(RHIBindPoint::Compute, fixture.compute);
        staleEncoder.Dispatch(1, 1, 1);
        RHISoftwareBatchDesc staleDesc;
        staleDesc.backendGeneration = 1;
        staleDesc.queue = identity;
        staleDesc.uploadRecordingId = fixture.device.GetCurrentUploadRecordingId();
        staleDesc.descriptorVersion = fixture.device.GetDescriptorVersionToken();
        staleDesc.lifetimeToken = std::make_shared<uint8_t>();
        RHISoftwareBatch stale;
        const uint32_t order = 0;
        require(recorder.FinishRecording({&order, 1}, staleDesc, stale, fixture.error),
                "stale identity fixture seal failed");
        RHITranslatedBatch translated;
        RHICommandFailure failure;
        require(!oldTranslator->Translate(std::move(stale), {}, translated, failure) && !translated.IsValid() &&
                    !frozen->HasRecorded(0),
                "held translator silently used current producer IDs for a stale frozen device");
        fixture.device.m_deviceIncarnation = identity.deviceGeneration;
        require(frozen->OpenWorker(0, fixture.error), "descriptor failure target open failed");
        auto& encoder = frozen->AcquireEncoder(0);
        encoder.SetPipeline(RHIBindPoint::Compute, fixture.compute);
        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, upload);
        const auto before = dispatches.load();
        failDescriptorAllocate = true;
        encoder.Dispatch(1, 1, 1);
        failDescriptorAllocate = false;
        require(dispatches == before && frozen->DrainEncoderDrops(fixture.error) != 0,
                "native descriptor allocation failure emitted a successful dispatch");
        require(frozen->CloseAll(fixture.error), "descriptor failure target close failed");
    }

    void verify_last_segment_error()
    {
        DeviceFixture fixture;
        fixture.Upload();
        std::shared_ptr<VulkanCommandBufferPool> frozen;
        require(fixture.producer.CreateTranslationPool(frozen, fixture.error), "failure pool capture failed");
        const auto queue = fixture.device.GetGraphicsQueueIdentity();
        VulkanSoftwareCommandValidator validator(fixture.device);
        RHISoftwareRecorder recorder(validator);
        require(recorder.BeginRecording({queue, 1, 1 << 20, 1 << 20}, fixture.error), "failure recorder begin failed");
        auto& encoder = recorder.AcquireEncoder(0);
        encoder.CopyTexture(fixture.image, fixture.image, 0, 0); // explicitly unsupported native void operation
        RHISoftwareBatchDesc desc;
        desc.lifetimeToken = std::make_shared<uint8_t>();
        desc.backendGeneration = 1;
        desc.queue = queue;
        frozen->GetTranslationRecording(desc.uploadRecordingId, desc.descriptorVersion);
        RHISoftwareBatch batch;
        const uint32_t order = 0;
        require(recorder.FinishRecording({&order, 1}, desc, batch, fixture.error), "failure batch seal failed");
        RHITranslatedBatch translated;
        RHICommandFailure failure;
        require(!frozen->GetTranslator(queue)->Translate(std::move(batch), {}, translated, failure),
                "last native unsupported command was silently accepted");
        require(!translated.IsValid() && failure.message.find("CopyTexture") != std::string::npos,
                "unsupported command failure provenance lost");
    }

    void verify_software_timestamps()
    {
        DeviceFixture fixture;
        VulkanCaptureGpuProfiler profiler(fixture.device);
        require(profiler.Initialize(fixture.error), "query fixture initialization failed");
        VulkanSoftwareCommandValidator validator(fixture.device);
        RHISoftwareCommandList list(1 << 20, 1 << 20);
        RHISoftwareEncoder software(list, validator);
        const auto writesBefore = queryWrites.load();
        const auto slot = profiler.BeginPass(software, "immutable query");
        profiler.EndPass(software, slot);
        require(queryWrites == writesBefore && list.GetCommandCount() == 2,
                "software timestamp emitted native command during recording");
        require(list.Seal(fixture.error) && fixture.producer.OpenWorker(0, fixture.error), "query replay setup failed");
        auto& encoder = fixture.producer.AcquireEncoder(0);
        RHICommandTranslationStats stats;
        RHICommandFailure failure;
        require(list.Replay(encoder, stats, failure), "software timestamp replay failed");
        require(queryWrites == writesBefore + 2 && queryResets != 0, "deferred Vulkan query pairing lost");
        require(fixture.producer.CloseAll(fixture.error), "query target close failed");
        profiler.ReleaseAfterIdle();
    }
} // namespace

int main()
{
    try
    {
        install_native_boundary();
        ce::get_job_scheduler().start(8);
        verify_device_incarnations();
        verify_validator();
        verify_snapshot_and_leases();
        verify_descriptor_budget();
        verify_warm_storage();
        for (uint32_t workers : {1u, 4u, 8u})
        {
            verify_translation_and_submission(workers);
        }
        verify_fork_and_descriptor_failure();
        verify_last_segment_error();
        verify_software_timestamps();
        ce::get_job_scheduler().shutdown();
        std::cout << "{\"checks\":" << checks.load() << ",\"warm_snapshot_binding_heap_allocations\":0,"
                  << "\"native_gpu_executed\":false,\"actual_scheduler_workers\":8,"
                  << "\"translated_target_counts\":[1,4,8],\"native_api_boundary\":\"mock\"}\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "VULKAN_SOFTWARE_FAILURE: " << error.what() << '\n';
        ce::get_job_scheduler().shutdown();
        return 1;
    }
}
