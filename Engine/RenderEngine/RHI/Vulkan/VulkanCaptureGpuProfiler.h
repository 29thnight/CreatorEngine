#pragma once
#include "VulkanDeviceResources.h"
#include "VulkanEncoder.h"
#include "../IRHIGpuProfiler.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <map>
#include <vector>

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
            resources_.WaitForGpu();
            VulkanApi::vkDestroyQueryPool(resources_.GetDevice(), pool_, nullptr);
        }
    }
    bool Initialize(std::string& error)
    {
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
        resources_.WaitForGpu();
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
