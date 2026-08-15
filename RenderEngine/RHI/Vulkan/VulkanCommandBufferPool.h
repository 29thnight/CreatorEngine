#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "../RHIParallelCommandPool.h"

#include <condition_variable>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class VulkanDeviceResources;
class VulkanEncoder;

/// 워커마다 독립 VkCommandPool/VkCommandBuffer를 소유하는 G-3 Vulkan 구현.
/// Vulkan command pool의 host access는 externally synchronized이므로 한 worker가
/// 자기 pool만 기록하고 owner thread가 worker 실행 전후에 reset/end한다.
class VulkanCommandBufferPool final : public IRHIParallelCommandPool
{
public:
    ~VulkanCommandBufferPool() override { Shutdown(); }

    bool Initialize(VulkanDeviceResources& resources, uint32_t workerCount,
        uint32_t frameCount, std::string& outError);
    void Shutdown();

    bool IsInitialized() const override { return !m_slots.empty(); }
    uint32_t GetWorkerCount() const override { return m_workerCount; }
    void BeginFrame(uint32_t frameIndex) override;
    bool Prepare(std::string& outError) override;
    bool OpenWorker(uint32_t worker, std::string& outError) override;
    RHIEncoder& AcquireEncoder(uint32_t worker) override;
    bool CloseAll(std::string& outError) override;
    bool HasRecorded(uint32_t worker) const override;
    void RunParallel(const std::function<void(uint32_t)>& job,
        uint32_t workerCount) override;
    bool Submit(std::span<const uint32_t> workerOrder,
        std::string& outError) override;

    uint32_t GetEncoderUnimplementedCount() const
    {
        return m_encoderUnimplemented.load(std::memory_order_relaxed);
    }
    const char* GetEncoderLastUnimplemented() const
    {
        return m_lastUnimplemented.load(std::memory_order_relaxed);
    }

private:
    struct Slot
    {
        VkCommandPool pool{ VK_NULL_HANDLE };
        VkCommandBuffer buffer{ VK_NULL_HANDLE };
        std::unique_ptr<VulkanEncoder> encoder;
        bool opened{ false };
    };

    void WorkerLoop(uint32_t worker);
    void RetireEncoder(Slot& slot);

    VulkanDeviceResources* m_resources{ nullptr };
    VkDevice m_device{ VK_NULL_HANDLE };
    std::vector<std::vector<Slot>> m_slots;
    uint32_t m_workerCount{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };

    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wakeSignal;
    std::condition_variable m_doneSignal;
    const std::function<void(uint32_t)>* m_job{ nullptr };
    uint32_t m_jobWorkerCount{ 0 };
    uint64_t m_generation{ 0 };
    uint32_t m_pending{ 0 };
    bool m_stopping{ false };

    std::atomic<uint32_t> m_encoderUnimplemented{ 0 };
    std::atomic<const char*> m_lastUnimplemented{ nullptr };
};

#endif
