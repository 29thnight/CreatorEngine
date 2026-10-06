#pragma once
#include "VulkanDeviceResources.h"
#include "VulkanEncoder.h"
#include "../IRHIGpuProfiler.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <map>
#include <vector>
#include <cstdio>
#include <exception>

// One diagnostic submission. Query ownership never borrows the interactive
// profiler's ring slot. Destruction waits for device completion on abort too.
class VulkanCaptureGpuProfiler final : public IRHIGpuProfiler
{
public:
    explicit VulkanCaptureGpuProfiler(VulkanDeviceResources& resources) : resources_(resources) {}
    ~VulkanCaptureGpuProfiler()
    {
        if (pool_ != VK_NULL_HANDLE)
        {
            std::string error;
            bool safe = resources_.IsInitialized() &&
                resources_.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error);
            if (!safe && GetRHISubmissionThread().GetOwnerStats(&resources_).faulted)
            {
                safe = resources_.DrainForLifecycle(RHILifecycleCommand::UnrecoverableDeviceError, error);
            }
            if (!safe)
            {
                std::fprintf(stderr, "Fatal Vulkan capture teardown invariant: query pool still owned without "
                    "verified GPU idle or device loss: %s\n", error.c_str());
                std::fflush(stderr);
                std::terminate();
            }
            ReleaseAfterIdle();
        }
    }

    /// 소유자가 실제 GPU-idle 또는 확인된 장치 손실을 증명한 뒤에만 호출한다.
    void ReleaseAfterIdle()
    {
        if (pool_ != VK_NULL_HANDLE)
        {
            if (resources_.GetDevice() == VK_NULL_HANDLE)
            {
                std::fputs("Fatal Vulkan capture teardown invariant: query pool outlived its native device\n", stderr);
                std::fflush(stderr);
                std::terminate();
            }
            VulkanApi::vkDestroyQueryPool(resources_.GetDevice(), pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }

    bool Initialize(std::string& error)
    {
        if (pool_ != VK_NULL_HANDLE || !resources_.IsInitialized())
        {
            error = "Vulkan capture requires an initialized device and no previous query pool";
            return false;
        }
        VkPhysicalDeviceProperties properties{};
        VulkanApi::vkGetPhysicalDeviceProperties(resources_.GetPhysicalDevice(), &properties);
        period_ = properties.limits.timestampPeriod;
        uint32_t count = 0;
        VulkanApi::vkGetPhysicalDeviceQueueFamilyProperties(resources_.GetPhysicalDevice(), &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        VulkanApi::vkGetPhysicalDeviceQueueFamilyProperties(resources_.GetPhysicalDevice(), &count, families.data());
        bits_ = families.at(resources_.GetQueueFamily()).timestampValidBits;
        if (!bits_ || period_ <= 0.f) { error = "Vulkan queue timestamps unsupported"; return false; }
        VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kCapacity * 2;
        if (VulkanApi::vkCreateQueryPool(resources_.GetDevice(), &info, nullptr, &pool_) != VK_SUCCESS)
        { error = "Vulkan capture query pool creation failed"; return false; }
        used_.store(0);
        failed_.store(false);
        return true;
    }
    uint32_t BeginPass(RHIEncoder& encoder, const std::string& name) override
    {
        auto* vk = dynamic_cast<VulkanEncoder*>(&encoder);
        const uint32_t slot = used_.fetch_add(1, std::memory_order_relaxed);
        if (!vk || pool_ == VK_NULL_HANDLE || slot >= kCapacity)
        { failed_.store(true); return kInvalidSlot; }
        names_[slot] = name;
        const auto cmd = vk->GetCommandBuffer();
        // A freshly acquired pass encoder has no open rendering instance.
        // Each worker owns disjoint queries, so resets cannot race GPU writes.
        VulkanApi::vkCmdResetQueryPool(cmd, pool_, slot * 2, 2);
        VulkanApi::vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, slot * 2);
        return slot;
    }
    void EndPass(RHIEncoder& encoder, uint32_t slot) override
    {
        auto* vk = dynamic_cast<VulkanEncoder*>(&encoder);
        if (!vk || slot == kInvalidSlot) { failed_.store(true); return; }
        VulkanApi::vkCmdWriteTimestamp(vk->GetCommandBuffer(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                      pool_, slot * 2 + 1);
    }
    struct Timing { std::string name; double milliseconds; };
    uint32_t SliceCount() const { return used_.load(); }
    bool Collect(std::vector<Timing>& passes, double& spanMs, double& busyMs, std::string& error)
    {
        if (pool_ == VK_NULL_HANDLE || !resources_.IsInitialized())
        {
            error = "Vulkan capture query pool/device unavailable";
            return false;
        }
        if (!resources_.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error))
        {
            return false;
        }
        if (resources_.GetLastLifecycleResult().command == RHILifecycleCommand::UnrecoverableDeviceError)
        {
            error = "Vulkan capture has device-loss proof, not completed timestamp data";
            return false;
        }
        const uint32_t count = used_.load();
        if (!count || count > kCapacity || failed_.load())
        { error = "Vulkan capture timestamp coverage incomplete"; return false; }
        std::vector<uint64_t> ticks(count * 2);
        if (VulkanApi::vkGetQueryPoolResults(resources_.GetDevice(), pool_, 0, count * 2,
                ticks.size() * sizeof(uint64_t), ticks.data(), sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        { error = "Vulkan capture timestamps not complete"; return false; }
        const uint64_t mask = bits_ == 64 ? ~uint64_t{0} : (uint64_t{1} << bits_) - 1;
        std::vector<std::pair<uint64_t, uint64_t>> intervals;
        std::map<std::string, double> totals;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint64_t begin = ticks[i * 2] & mask, end = ticks[i * 2 + 1] & mask;
            if (end < begin) { error = "Vulkan capture timestamp wrapped/reversed"; return false; }
            totals[names_[i]] += static_cast<double>(end - begin) * period_ / 1e6;
            intervals.emplace_back(begin, end);
        }
        std::sort(intervals.begin(), intervals.end());
        uint64_t begin = intervals.front().first, end = intervals.front().second;
        const uint64_t first = begin;
        uint64_t busy = 0;
        for (const auto& interval : intervals)
        {
            if (interval.first > end) { busy += end - begin; begin = interval.first; }
            end = (std::max)(end, interval.second);
        }
        busy += end - begin;
        spanMs = static_cast<double>(end - first) * period_ / 1e6;
        busyMs = static_cast<double>(busy) * period_ / 1e6;
        for (const auto& [name, milliseconds] : totals) passes.push_back({name, milliseconds});
        return true;
    }
private:
    static constexpr uint32_t kCapacity = 512;
    VulkanDeviceResources& resources_;
    VkQueryPool pool_{VK_NULL_HANDLE};
    std::atomic<uint32_t> used_{0};
    std::atomic<bool> failed_{false};
    std::array<std::string, kCapacity> names_;
    float period_{};
    uint32_t bits_{};
};
