#pragma once
#include "../Utility_Framework/Foundation/EngineStorage.h"
#include "../RenderEngine/Effects/EffectFrame.h"

namespace ce::effects
{
// GT-owned authoring/command state. All published frames are fresh immutable
// copies. Render threads never access this object or resolve live handles.
class EffectState
{
public:
    EffectState();
    bool SetSceneEpoch(std::uint64_t epoch);
    bool CommitTick(const EffectTick& tick);
    EffectHandle RegisterInstance(std::string definition, const EffectTransform& transform);
    bool UpdateInstance(EffectHandle handle, const EffectTransform& transform);
    bool Burst(EffectHandle handle, std::uint32_t count);
    bool UnregisterInstance(EffectHandle handle);
    EffectFrameOwner CaptureFrame() const;
    void Shutdown();
    own::shared_owner<EffectBridgeCounters> Counters() const { return counters_; }
    own::shared_owner<const std::atomic<std::uint64_t>> SceneEpochToken() const { return sceneEpochToken_; }
private:
    struct Slot { EffectInstanceSnapshot value; std::uint64_t generation{1}; bool alive{}; };
    void RequireOwner() const;
    Slot* Resolve(EffectHandle handle);
    void Publish();
    void PruneAcknowledged();
    std::thread::id owner_{std::this_thread::get_id()};
    bool stopped_{};
    std::uint64_t epoch_{}, tick_{}, revision_{};
    double seconds_{};
    ce::dynamic_array<Slot> slots_;
    ce::segmented_deque<EffectBurst> bursts_;
    std::uint64_t burstSequence_{};
    own::shared_owner<EffectBurstReceipt> burstReceipt_;
    EffectFrameOwner frame_;
    own::shared_owner<EffectBridgeCounters> counters_;
    own::shared_owner<std::atomic<std::uint64_t>> sceneEpochToken_;
};
}
