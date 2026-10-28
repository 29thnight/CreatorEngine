#include "EffectState.h"
#include "EffectDefinition.h"

namespace ce::effects
{
namespace
{
bool Valid(const EffectTransform& value)
{
    return std::all_of(value.world.begin(), value.world.end(), [](float x) { return std::isfinite(x); });
}
}
EffectState::EffectState() : counters_(own::make_shared<EffectBridgeCounters>()),
    sceneEpochToken_(own::make_shared<std::atomic<std::uint64_t>>(0)) {}
void EffectState::RequireOwner() const
{
    if (std::this_thread::get_id() != owner_) throw std::logic_error("Effect state requires its game-thread owner");
}
void EffectState::Publish()
{
    if (revision_ == UINT64_MAX) throw std::overflow_error("Effect snapshot identity exhausted");
    auto frame = own::make_shared<EffectFrame>();
    frame->sceneEpoch = epoch_; frame->tick = tick_; frame->revision = ++revision_; frame->simulationSeconds = seconds_;
    for (const auto& slot : slots_) if (slot.alive) frame->instances.push_back(slot.value);
    PruneAcknowledged();
    frame->bursts.assign(bursts_.begin(),bursts_.end());
    frame->burstSequenceThrough = burstSequence_;
    frame->burstReceipt = burstReceipt_;
    frame_ = std::move(frame);
    ++counters_->snapshots;
}
void EffectState::PruneAcknowledged()
{
    const auto acknowledged = burstReceipt_ ? burstReceipt_->Acknowledged() : 0;
    while (!bursts_.empty() && bursts_.front().sequence <= acknowledged) bursts_.pop_front();
}
bool EffectState::SetSceneEpoch(std::uint64_t epoch)
{
    RequireOwner();
    if (stopped_ || !epoch || epoch < epoch_) return false;
    if (epoch == epoch_) return true;
    epoch_ = epoch; tick_ = 0; seconds_ = 0; slots_.clear(); bursts_.clear(); burstSequence_ = 0;
    sceneEpochToken_->store(epoch,std::memory_order_release);
    burstReceipt_ = own::make_shared<EffectBurstReceipt>(); Publish(); return true;
}
bool EffectState::CommitTick(const EffectTick& tick)
{
    RequireOwner();
    if (stopped_ || tick.sceneEpoch != epoch_ || !epoch_ || tick_ == UINT64_MAX || tick.tick != tick_ + 1 ||
        !std::isfinite(tick.seconds) || tick.seconds <= 0 || !std::isfinite(seconds_ + tick.seconds)) return false;
    tick_ = tick.tick; seconds_ += tick.seconds; ++counters_->ticks; Publish(); return true;
}
EffectState::Slot* EffectState::Resolve(EffectHandle handle)
{
    if (stopped_ || !epoch_ || handle.sceneEpoch != epoch_ || !handle.index || handle.index > slots_.size()) return nullptr;
    auto& slot = slots_[handle.index - 1];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}
EffectHandle EffectState::RegisterInstance(std::string definition, const EffectTransform& transform)
{
    RequireOwner();
    if (stopped_ || !epoch_ || !IsEffectDefinitionId(definition) || !Valid(transform)) return {};
    std::size_t index = 0;
    while (index < slots_.size() && (slots_[index].alive || slots_[index].generation == UINT64_MAX)) ++index;
    if (index == UINT32_MAX) return {};
    if (index == slots_.size()) slots_.emplace_back();
    auto& slot = slots_[index]; slot.alive = true;
    slot.value = {{epoch_,slot.generation,static_cast<std::uint32_t>(index + 1)},std::move(definition),transform,0};
    Publish(); return slot.value.handle;
}
bool EffectState::UpdateInstance(EffectHandle handle, const EffectTransform& transform)
{
    RequireOwner(); auto* slot = Resolve(handle);
    if (!slot || !Valid(transform)) return false;
    slot->value.transform = transform; Publish(); return true;
}
bool EffectState::Burst(EffectHandle handle, std::uint32_t count)
{
    RequireOwner(); auto* slot = Resolve(handle);
    if (!slot || count > UINT64_MAX - slot->value.burstTotal) return false;
    if (!count) return true;
    PruneAcknowledged();
    if (bursts_.size() >= EffectPendingBurstLimit || burstSequence_ == UINT64_MAX) return false;
    bursts_.push_back({++burstSequence_,tick_,handle,slot->value.transform,count});
    slot->value.burstTotal += count; Publish(); return true;
}
bool EffectState::UnregisterInstance(EffectHandle handle)
{
    RequireOwner(); auto* slot = Resolve(handle);
    if (!slot) return false;
    slot->alive = false;
    if (slot->generation != UINT64_MAX) ++slot->generation;
    ce::erase_if(bursts_,[&](const EffectBurst& burst) { return burst.handle == handle; });
    slot->value = {}; Publish(); return true;
}
EffectFrameOwner EffectState::CaptureFrame() const { RequireOwner(); return frame_; }
void EffectState::Shutdown()
{
    RequireOwner(); stopped_ = true; slots_.clear(); bursts_.clear(); burstReceipt_.reset(); frame_.reset();
    sceneEpochToken_->store(0,std::memory_order_release);
}
}
