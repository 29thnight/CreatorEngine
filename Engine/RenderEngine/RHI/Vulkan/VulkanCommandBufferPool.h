#pragma once
#include "VulkanLoader.h"
#include "../RHIParallelCommandPool.h"

#include <atomic>
#include <memory>
#include <vector>

class VulkanDeviceResources;
class VulkanEncoder;

/// 기록 구간마다 독립 VkCommandPool/VkCommandBuffer를 소유한다. 물리 스레드는
/// 공용 job_scheduler가 소유하며, 구간 번호와 물리 워커 번호는 독립적이다.
/// Vulkan command pool의 host access는 externally synchronized이므로 한 worker가
/// 자기 pool만 기록하고 owner thread가 worker 실행 전후에 reset/end한다.
class VulkanCommandBufferPool final : public IRHIParallelCommandPool
{
public:
    explicit VulkanCommandBufferPool(job_scheduler& scheduler = ce::get_job_scheduler())
        : IRHIParallelCommandPool(scheduler) {}
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
    uint32_t DrainEncoderDrops(std::string& outLast) override;
    bool HasRecorded(uint32_t worker) const override;
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

    void RetireEncoder(Slot& slot);
    uint32_t GetCurrentFrameSlot() const override { return m_frameIndex; }
    bool PrepareRecordedCommands(uint32_t frameSlot,
        RHICompletionPoint& outCompletion, std::string& outError) override;
    bool SubmitRecordedCommands(uint32_t frameSlot,
        std::span<const uint32_t> workerOrder, RHICompletionPoint completion,
        std::string& outError) override;

    VulkanDeviceResources* m_resources{ nullptr };
    VkDevice m_device{ VK_NULL_HANDLE };
    std::vector<std::vector<Slot>> m_slots;
    uint32_t m_workerCount{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };

    std::atomic<uint32_t> m_encoderUnimplemented{ 0 };
    std::atomic<const char*> m_lastUnimplemented{ nullptr };
};

