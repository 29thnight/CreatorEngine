#ifndef DYNAMICCPP_EXPORTS
#include "VulkanCommandBufferPool.h"
#include "VulkanDeviceResources.h"
#include "VulkanEncoder.h"

#include <algorithm>
#include <stdexcept>

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

    m_stopping = false;
    m_threads.reserve(workerCount - 1);
    for (uint32_t worker = 1; worker < workerCount; ++worker)
        m_threads.emplace_back(&VulkanCommandBufferPool::WorkerLoop, this, worker);
    return true;
}

void VulkanCommandBufferPool::WorkerLoop(uint32_t worker)
{
    uint64_t seen = 0;
    for (;;)
    {
        const std::function<void(uint32_t)>* job = nullptr;
        {
            std::unique_lock lock(m_mutex);
            m_wakeSignal.wait(lock, [&]
            {
                return m_stopping || (m_generation != seen && worker < m_jobWorkerCount);
            });
            if (m_stopping) return;
            seen = m_generation;
            job = m_job;
        }

        if (nullptr != job) (*job)(worker);

        {
            std::lock_guard guard(m_mutex);
            if (0 != m_pending && 0 == --m_pending) m_doneSignal.notify_one();
        }
    }
}

void VulkanCommandBufferPool::RunParallel(
    const std::function<void(uint32_t)>& job, uint32_t workerCount)
{
    if (0 == workerCount) return;
    workerCount = (std::min)(workerCount, m_workerCount);
    if (1 == workerCount || m_threads.empty())
    {
        job(0);
        return;
    }

    {
        std::lock_guard guard(m_mutex);
        m_job = &job;
        m_jobWorkerCount = workerCount;
        m_pending = workerCount - 1;
        ++m_generation;
    }
    m_wakeSignal.notify_all();
    job(0);

    {
        std::unique_lock lock(m_mutex);
        m_doneSignal.wait(lock, [&] { return 0 == m_pending; });
        m_job = nullptr;
        m_jobWorkerCount = 0;
    }
}

void VulkanCommandBufferPool::RetireEncoder(Slot& slot)
{
    if (!slot.encoder) return;
    if (0 != slot.encoder->GetUnimplementedCount())
    {
        m_encoderUnimplemented.fetch_add(
            slot.encoder->GetUnimplementedCount(), std::memory_order_relaxed);
        m_lastUnimplemented.store(
            slot.encoder->GetLastUnimplemented(), std::memory_order_relaxed);
    }
    slot.encoder.reset();
}

void VulkanCommandBufferPool::Shutdown()
{
    if (!m_threads.empty())
    {
        {
            std::lock_guard guard(m_mutex);
            m_stopping = true;
            ++m_generation;
        }
        m_wakeSignal.notify_all();
        for (std::thread& thread : m_threads)
            if (thread.joinable()) thread.join();
        m_threads.clear();
    }

    for (auto& frame : m_slots)
    {
        for (Slot& slot : frame)
        {
            RetireEncoder(slot);
            if (VK_NULL_HANDLE != m_device && VK_NULL_HANDLE != slot.pool)
                vkDestroyCommandPool(m_device, slot.pool, nullptr);
            slot.pool = VK_NULL_HANDLE;
            slot.buffer = VK_NULL_HANDLE;
            slot.opened = false;
        }
    }
    m_slots.clear();
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
    slot.encoder = std::make_unique<VulkanEncoder>(
        slot.buffer, m_resources->m_pipelineCache, &m_resources->m_resourceTable,
        &m_resources->m_renderTargetTable, m_device,
        &m_resources->m_descriptorRecycler, &m_resources->m_bindingTable,
        &m_resources->m_samplerTable);
    return *slot.encoder;
}

bool VulkanCommandBufferPool::CloseAll(std::string& outError)
{
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

bool VulkanCommandBufferPool::PrepareRecordedCommands(uint32_t frameSlot,
    RHICompletionPoint& outCompletion, std::string& outError)
{
    if (nullptr == m_resources || frameSlot >= m_slots.size())
    {
        outError = "Vulkan 기록 batch 제출 준비 대상이 잘못됐다";
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

    std::vector<VkCommandBuffer> buffers;
    buffers.reserve(workerOrder.size());
    for (uint32_t worker : workerOrder)
    {
        if (worker < m_workerCount && m_slots[frameSlot][worker].opened)
            buffers.push_back(m_slots[frameSlot][worker].buffer);
    }
    return m_resources->SubmitParallelCommandBuffers(buffers, completion, outError);
}

#endif
