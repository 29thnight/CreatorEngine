#pragma once
#include "../../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

namespace ce::effects
{
struct EffectHandle
{
    std::uint64_t sceneEpoch{}, generation{};
    std::uint32_t index{}; // zero is invalid
    bool operator==(const EffectHandle&) const = default;
};
struct EffectTransform
{
    ce::fixed_array<float,16> world{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
};
struct EffectTick { std::uint64_t sceneEpoch{}, tick{}; double seconds{}; };
inline constexpr std::size_t EffectPendingBurstLimit = 4096;
struct EffectBurst
{
    std::uint64_t sequence{}, tick{};
    EffectHandle handle;
    EffectTransform transform;
    std::uint32_t count{};
};
class EffectBurstReceipt
{
public:
    std::uint64_t Acknowledged() const noexcept { return through_.load(std::memory_order_acquire); }
private:
    friend struct EffectFrame;
    void Acknowledge(std::uint64_t through) noexcept
    {
        auto previous = through_.load(std::memory_order_relaxed);
        while (previous < through && !through_.compare_exchange_weak(previous,through,std::memory_order_release,std::memory_order_relaxed)) {}
    }
    std::atomic<std::uint64_t> through_{};
};
struct EffectInstanceSnapshot
{
    EffectHandle handle;
    std::string definitionId;
    EffectTransform transform;
    // Cumulative event count, not latest-wins spawn state. Renderer coalescing
    // cannot lose accepted bursts. This does not count simulated GPU particles.
    std::uint64_t burstTotal{};
};
struct EffectFrame
{
    // SceneManager context identity, distinct from renderer sceneEpoch.
    std::uint64_t sceneEpoch{}, tick{}, revision{};
    double simulationSeconds{};
    ce::dynamic_array<EffectInstanceSnapshot> instances;
    ce::dynamic_array<EffectBurst> bursts;
    std::uint64_t burstSequenceThrough{};
    own::shared_owner<EffectBurstReceipt> burstReceipt;
    // Only a consumer that has accepted the corresponding simulation submission
    // may acknowledge. Merely declaring/recording the HDR boundary never does.
    void AcknowledgeSubmittedBursts() const noexcept
    {
        if (burstReceipt) burstReceipt->Acknowledge(burstSequenceThrough);
    }
};
using EffectFrameOwner = own::shared_owner<const EffectFrame>;
struct EffectBridgeCounters
{
    std::atomic<std::uint64_t> ticks{}, snapshots{}, declarations{}, records{};
};
}
