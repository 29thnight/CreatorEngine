#include "../RenderEngine/Effects/IEffectSystem.h"
#include "EffectState.h"
#include "EffectRenderContributor.h"
#include <stdio.h>
#include <stdlib.h>

namespace
{
class EffectSystem final : public ce::effects::IEffectSystem
{
public:
    bool Initialize(std::string&) override
    {
        std::puts("[effect.package] initialized");
        return true;
    }
    void Shutdown() noexcept override
    {
        const auto counters = state_.Counters();
        std::printf("[effect.bridge] ticks=%llu snapshots=%llu hdrDeclarations=%llu hdrRecords=%llu\n",
            static_cast<unsigned long long>(counters->ticks.load()),
            static_cast<unsigned long long>(counters->snapshots.load()),
            static_cast<unsigned long long>(counters->declarations.load()),
            static_cast<unsigned long long>(counters->records.load()));
        state_.Shutdown();
        std::puts("[effect.package] shutdown");
    }
    bool SetSceneEpoch(std::uint64_t epoch) override { return state_.SetSceneEpoch(epoch); }
    bool CommitTick(const ce::effects::EffectTick& tick) override { return state_.CommitTick(tick); }
    ce::effects::EffectFrameOwner CaptureFrame() const override { return state_.CaptureFrame(); }
    ce::effects::EffectHandle RegisterInstance(std::string id, const ce::effects::EffectTransform& transform) override
    { return state_.RegisterInstance(std::move(id),transform); }
    bool UpdateInstance(ce::effects::EffectHandle handle, const ce::effects::EffectTransform& transform) override
    { return state_.UpdateInstance(handle,transform); }
    bool Burst(ce::effects::EffectHandle handle, std::uint32_t count) override { return state_.Burst(handle,count); }
    bool UnregisterInstance(ce::effects::EffectHandle handle) override { return state_.UnregisterInstance(handle); }
    own::shared_owner<IRenderFeatureContributor> CreateRenderContributor() override
    { return ce::effects::MakeEffectRenderContributor(state_.Counters(),state_.SceneEpochToken()); }
private:
    ce::effects::EffectState state_;
};

std::unique_ptr<ce::effects::IEffectSystem> CreateEffectSystem()
{
    return std::make_unique<EffectSystem>();
}

struct Registration final
{
    Registration()
    {
        if (!ce::effects::RegisterFactory(&CreateEffectSystem)) std::abort();
    }
    ~Registration()
    {
        if (!ce::effects::UnregisterFactory(&CreateEffectSystem)) std::abort();
    }
} registration;
}

// EffectPackage.targets roots this archive member in each host DLL. Merely
// building a static archive does not otherwise retain its static constructor.
extern "C" void ce_effect_package_anchor() {}
