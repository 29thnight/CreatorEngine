#include "../../../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "VulkanCommandBufferPool.h"
#include "VulkanDeviceResources.h"
#include "VulkanEncoder.h"
#include "VulkanRecordingResources.h"
#include "VulkanCommandTranslator.h"

#include <stdint.h>
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

using namespace VulkanApi;

bool VulkanCommandBufferPool::Initialize(VulkanDeviceResources& resources,
    uint32_t workerCount, uint32_t frameCount, std::string& outError)
{
    if (VK_NULL_HANDLE == resources.GetDevice() || UINT32_MAX == resources.GetQueueFamily() ||
        0 == workerCount || 0 == frameCount)
    {
        outError = "Vulkan 병렬 command pool 인자가 잘못됐다";
        return false;
    }

    workerCount = (std::min)(workerCount, kMaxWorkers);
    m_resources = &resources;
    m_device = resources.GetDevice();
    m_workerCount = workerCount;
    m_frameCount = frameCount;
    m_frameIndex = 0;
    m_slots.resize(frameCount);

    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        m_slots[frame].resize(workerCount);
        for (uint32_t worker = 0; worker < workerCount; ++worker)
        {
            Slot& slot = m_slots[frame][worker];
            VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            poolInfo.queueFamilyIndex = resources.GetQueueFamily();
            poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &slot.pool);
            if (VK_SUCCESS != result)
            {
                outError = "Vulkan worker command pool 생성 실패 — " + ResultToString(result);
                Shutdown();
                return false;
            }

            VkCommandBufferAllocateInfo allocInfo{
                VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocInfo.commandPool = slot.pool;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandBufferCount = 1;
            result = vkAllocateCommandBuffers(m_device, &allocInfo, &slot.buffer);
            if (VK_SUCCESS != result)
            {
                outError = "Vulkan worker command buffer 할당 실패 — " + ResultToString(result);
                Shutdown();
                return false;
            }
        }
    }

    return true;
}

void VulkanCommandBufferPool::RetireEncoder(Slot& slot, bool destroy)
{
    if (!slot.encoder) return;
    if (0 != slot.encoder->GetUnimplementedCount())
    {
        m_encoderUnimplemented.fetch_add(
            slot.encoder->GetUnimplementedCount(), std::memory_order_relaxed);
        m_lastUnimplemented.store(
            slot.encoder->GetLastUnimplemented(), std::memory_order_relaxed);
    }
    slot.encoder->EndRenderTargets();
    slot.encoder->ClearUnimplemented();
    if (destroy)
    {
        slot.encoder.reset();
    }
}

void VulkanCommandBufferPool::Shutdown()
{
    m_translationPools.clear();
    m_translator.reset();
    m_translatorQueue = {};
    m_translationQueue = {};
    // CPU 기록은 owner의 RunParallel 반환으로 끝났다. GPU/제출 drain은 호출부가 맡는다.
    for (auto& frame : m_slots)
    {
        for (Slot& slot : frame)
        {
            RetireEncoder(slot, true);
            if (VK_NULL_HANDLE != m_device && VK_NULL_HANDLE != slot.pool)
                vkDestroyCommandPool(m_device, slot.pool, nullptr);
            slot.pool = VK_NULL_HANDLE;
            slot.buffer = VK_NULL_HANDLE;
            slot.opened = false;
        }
    }
    m_slots.clear();
    m_recordingResources.reset();
    m_translationDescriptors.Shutdown(m_device);
    m_translationDescriptorRecording = 0;
    m_resources = nullptr;
    m_device = VK_NULL_HANDLE;
    m_workerCount = 0;
    m_frameCount = 0;
    m_frameIndex = 0;
}

void VulkanCommandBufferPool::BeginFrame(uint32_t frameIndex)
{
    if (m_slots.empty()) return;
    m_frameIndex = frameIndex % m_frameCount;
    for (Slot& slot : m_slots[m_frameIndex])
    {
        RetireEncoder(slot);
        slot.opened = false;
    }
}

bool VulkanCommandBufferPool::CreateTranslationPool(own::shared_owner<VulkanCommandBufferPool>& result,
                                                    std::string& outError)
{
    if (result || !m_resources || !m_resources->GetGraphicsQueueIdentity().IsValid() || m_slots.empty() ||
        m_recordingResources)
    {
        outError = "Vulkan translation fork requires an initialized producer and empty output.";
        return false;
    }
    for (const auto& slot : m_slots[m_frameIndex])
    {
        if (slot.opened)
        {
            outError = "Vulkan cannot fork targets already recorded by the producer.";
            return false;
        }
    }
    m_translationPools.resize(m_frameCount);
    auto& cached = m_translationPools[m_frameIndex];
    auto frozen = cached && cached.use_count() == 1 ? cached : own::make_shared<VulkanCommandBufferPool>();
    if (!frozen->IsInitialized())
    {
        if (!frozen->Initialize(*m_resources, m_workerCount, 1, outError))
        {
            return false;
        }
        const auto budget = m_resources->m_descriptorRecycler.GetPoolBudget();
        if (!frozen->m_translationDescriptors.Initialize(m_device, 1, outError, budget ? budget->m_limit : 0, budget))
        {
            return false;
        }
        frozen->m_recordingResources = own::make_shared<VulkanRecordingResources>();
    }
    // Exclusive cached ownership proves both prior CPU replay and GPU retirement.
    frozen->BeginFrame(0);
    frozen->m_recordingResources->Reset();
    frozen->m_translationDescriptors.AbortRecording(frozen->m_translationDescriptorRecording);
    frozen->m_translationDescriptorRecording = m_resources->GetCurrentUploadRecordingId();
    if (!frozen->m_translationDescriptors.BeginRecording(m_device, frozen->m_translationDescriptorRecording,
                                                         outError) ||
        !frozen->m_recordingResources->Capture(*m_resources, outError))
    {
        return false;
    }
    cached = frozen;
    frozen->m_translationQueue = m_resources->GetGraphicsQueueIdentity();
    result = std::move(frozen);
    return true;
}

own::shared_owner<IRHICommandTranslator> VulkanCommandBufferPool::GetTranslator(RHIQueueIdentity queue)
{
    if (!m_recordingResources || !m_resources || !queue.IsValid() || queue != m_translationQueue ||
        queue != m_resources->GetGraphicsQueueIdentity())
    {
        return {};
    }
    if (!m_translator || m_translatorQueue != queue)
    {
        m_translator = own::make_shared<VulkanCommandTranslator>(*this, *m_resources, queue);
        m_translatorQueue = queue;
    }
    return m_translator;
}

bool VulkanCommandBufferPool::GetTranslationRecording(uint64_t& recordingId, uint64_t& descriptorVersion) const
{
    if (m_recordingResources && (!m_resources || !m_translationQueue.IsValid() ||
                                 m_translationQueue != m_resources->GetGraphicsQueueIdentity()))
    {
        recordingId = 0;
        descriptorVersion = 0;
        // A frozen source exists, but is invalid. Returning false would let the
        // common translator substitute the current producer's recording IDs.
        return true;
    }
    recordingId = m_recordingResources ? m_recordingResources->GetUploadRecordingId() : 0;
    descriptorVersion = m_recordingResources ? m_recordingResources->GetDescriptorVersion() : 0;
    return m_recordingResources != nullptr;
}

bool VulkanCommandBufferPool::Prepare(std::string& outError)
{
    if (nullptr == m_resources)
    {
        outError = "Vulkan 병렬 풀이 디바이스 서비스와 연결되지 않았다";
        return false;
    }
    return m_resources->FlushCommandList(outError);
}

bool VulkanCommandBufferPool::OpenWorker(uint32_t worker, std::string& outError)
{
    if (m_slots.empty() || worker >= m_workerCount)
    {
        outError = "Vulkan worker 번호가 범위를 벗어났다";
        return false;
    }

    Slot& slot = m_slots[m_frameIndex][worker];
    if (slot.opened) return true;
    VkResult result = vkResetCommandPool(m_device, slot.pool, 0);
    if (VK_SUCCESS != result)
    {
        outError = "Vulkan worker command pool reset 실패 — " + ResultToString(result);
        return false;
    }

    VkCommandBufferBeginInfo begin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    result = vkBeginCommandBuffer(slot.buffer, &begin);
    if (VK_SUCCESS != result)
    {
        outError = "Vulkan worker command buffer 시작 실패 — " + ResultToString(result);
        return false;
    }
    slot.opened = true;
    return true;
}

RHIEncoder& VulkanCommandBufferPool::AcquireEncoder(uint32_t worker)
{
    if (worker >= m_workerCount)
        throw std::out_of_range("Vulkan 병렬 encoder worker 범위 초과");

    Slot& slot = m_slots[m_frameIndex][worker];
    RetireEncoder(slot);
    if (!slot.encoder)
    {
        slot.encoder = own::make_unique<VulkanEncoder>(VK_NULL_HANDLE, nullptr);
    }
    slot.encoder->Rebind(
        slot.buffer, m_resources->m_pipelineCache, &m_resources->m_resourceTable,
        m_recordingResources ? nullptr : &m_resources->m_renderTargetTables[m_resources->m_frameIndex], m_device,
        m_recordingResources ? &m_translationDescriptors : &m_resources->m_descriptorRecycler,
        &m_resources->m_bindingTable, &m_resources->m_samplerTable, m_resources->GetIndirectDrawCapabilities(),
        m_resources->m_viewportLimits, m_resources->GetMeshShaderCapabilities());
    slot.encoder->SetRecordingResources(m_recordingResources.operator->());
    return *slot.encoder;
}

uint32_t VulkanCommandBufferPool::DrainEncoderDrops(std::string& outLast)
{
    // Translation checks diagnostics before CloseAll. Include the last segment
    // of every target after worker join, without issuing another native command.
    if (!m_slots.empty())
    {
        for (auto& slot : m_slots[m_frameIndex])
        {
            if (slot.encoder && slot.encoder->GetUnimplementedCount())
            {
                m_encoderUnimplemented.fetch_add(slot.encoder->GetUnimplementedCount(), std::memory_order_relaxed);
                m_lastUnimplemented.store(slot.encoder->GetLastUnimplemented(), std::memory_order_relaxed);
                slot.encoder->ClearUnimplemented();
            }
        }
    }
    const uint32_t count = m_encoderUnimplemented.exchange(0, std::memory_order_relaxed);
    const char* last = m_lastUnimplemented.load(std::memory_order_relaxed);
    outLast = (0 != count && nullptr != last) ? last : std::string{};
    return count;
}

bool VulkanCommandBufferPool::CloseAll(std::string& outError)
{
    if (m_slots.empty()) return true;
    for (Slot& slot : m_slots[m_frameIndex])
    {
        if (!slot.opened) continue;
        RetireEncoder(slot);
        const VkResult result = vkEndCommandBuffer(slot.buffer);
        if (VK_SUCCESS != result)
        {
            outError = "Vulkan worker command buffer 종료 실패 — " + ResultToString(result);
            return false;
        }
    }
    return true;
}

bool VulkanCommandBufferPool::HasRecorded(uint32_t worker) const
{
    return !m_slots.empty() && worker < m_workerCount &&
        m_slots[m_frameIndex][worker].opened;
}

uint64_t VulkanCommandBufferPool::GetCurrentRecordingId() const
{
    return m_recordingResources ? m_recordingResources->GetUploadRecordingId()
           : m_resources        ? m_resources->GetCurrentUploadRecordingId()
                                : 0;
}

void VulkanCommandBufferPool::RejectPreparedCommands(uint64_t recordingId, RHICompletionPoint completion)
{
    if (m_resources)
    {
        m_resources->RejectNeverEnqueuedRecording(recordingId, completion);
    }
}

void VulkanCommandBufferPool::AcceptPreparedCommands(RHICompletionPoint completion, const RHISubmissionTicket& ticket)
{
    if (m_resources)
    {
        m_resources->AcceptParallelSubmission(completion, ticket);
    }
}

bool VulkanCommandBufferPool::PrepareRecordedCommands(uint32_t frameSlot,
    RHICompletionPoint& outCompletion, std::string& outError)
{
    if (nullptr == m_resources || frameSlot >= m_slots.size())
    {
        outError = "Vulkan 기록 batch 제출 준비 대상이 잘못됐다";
        return false;
    }
    if (m_recordingResources &&
        m_resources->GetCurrentUploadRecordingId() != m_recordingResources->GetUploadRecordingId())
    {
        outError = "Vulkan frozen recording is no longer the producer admission recording.";
        return false;
    }
    return m_resources->PrepareParallelSubmission(outCompletion, outError);
}

bool VulkanCommandBufferPool::SubmitRecordedCommands(uint32_t frameSlot,
    std::span<const uint32_t> workerOrder, RHICompletionPoint completion,
    std::string& outError)
{
    if (nullptr == m_resources)
    {
        outError = "Vulkan 병렬 제출 coordinator가 없다";
        return false;
    }

    if (frameSlot >= m_slots.size())
    {
        outError = "Vulkan 기록 batch frame slot이 범위를 벗어났다";
        return false;
    }

    ce::fixed_array<VkCommandBuffer, kMaxWorkers> buffers{};
    size_t count = 0;
    if (workerOrder.size() > buffers.size())
    {
        outError = "Vulkan submitted target count exceeds the command pool bound.";
        return false;
    }
    for (uint32_t worker : workerOrder)
    {
        if (worker < m_workerCount && m_slots[frameSlot][worker].opened)
        {
            buffers[count++] = m_slots[frameSlot][worker].buffer;
        }
    }
    return m_resources->SubmitParallelCommandBuffers(std::span(buffers.data(), count), completion, outError);
}
