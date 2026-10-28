#pragma once
#include "../../Utility_Framework/Foundation/StandardLibrary.h"
#include "EffectFrame.h"

struct IRenderFeatureContributor;

// Core-only value/owner boundary. No Effect package concrete types cross it.
namespace ce::effects
{
class IEffectSystem
{
public:
    virtual ~IEffectSystem() = default;
    virtual bool Initialize(std::string& error) = 0;
    // Must also accept partial initialization and run before destruction.
    virtual void Shutdown() noexcept = 0;
    virtual bool SetSceneEpoch(std::uint64_t) { return true; }
    virtual bool CommitTick(const EffectTick&) { return true; }
    virtual EffectFrameOwner CaptureFrame() const { return {}; }
    virtual EffectHandle RegisterInstance(std::string, const EffectTransform&) { return {}; }
    virtual bool UpdateInstance(EffectHandle, const EffectTransform&) { return false; }
    virtual bool Burst(EffectHandle, std::uint32_t) { return false; }
    virtual bool UnregisterInstance(EffectHandle) { return false; }
    virtual own::shared_owner<IRenderFeatureContributor> CreateRenderContributor() { return {}; }
};

// GT alone accesses active; RT only copies the independently owned contributor
// under the mutex. Contributors must never retain a borrowed IEffectSystem*.
struct EffectBridge
{
    IEffectSystem* active{};
    std::mutex contributorMutex;
    own::shared_owner<IRenderFeatureContributor> contributor;
};
inline EffectBridge& Bridge() { static EffectBridge value; return value; }
inline bool SetEffectSceneEpoch(std::uint64_t epoch) { return !Bridge().active || Bridge().active->SetSceneEpoch(epoch); }
inline bool CommitEffectTick(const EffectTick& tick) { return !Bridge().active || Bridge().active->CommitTick(tick); }
inline EffectFrameOwner CaptureEffectFrame() { return Bridge().active ? Bridge().active->CaptureFrame() : EffectFrameOwner{}; }
inline own::shared_owner<IRenderFeatureContributor> CaptureEffectContributor()
{
    std::lock_guard lock(Bridge().contributorMutex);
    return Bridge().contributor;
}

using Factory = std::unique_ptr<IEffectSystem> (*)();

// All callers live in one host image (RenderEngine/Effect are static libraries).
// Registration occurs during image initialization, before runtime threads start;
// unregister occurs after the host session has shut down. Not a hot-load API.
inline Factory& RegisteredFactory() noexcept
{
    static Factory factory{};
    return factory;
}

inline bool RegisterFactory(Factory factory) noexcept
{
    if (!factory || RegisteredFactory()) return false;
    RegisteredFactory() = factory;
    return true;
}

inline bool UnregisterFactory(Factory factory) noexcept
{
    if (!factory || RegisteredFactory() != factory) return false;
    RegisteredFactory() = nullptr;
    return true;
}

// One session per host Run. No registration is a supported, allocation-free
// path. A failed/throwing Initialize still receives Shutdown through this owner.
class Session final
{
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() { Reset(); }

    bool Initialize(std::string& error)
    {
        if (system_) { error = "Effect session already initialized"; return false; }
        if (Bridge().active) { error = "Another effect session is active"; return false; }
        const auto factory = RegisteredFactory();
        if (!factory) return true;
        system_ = factory();
        if (!system_) { error = "Effect factory returned null"; return false; }
        if (!system_->Initialize(error)) { Reset(); return false; }
        auto contributor = system_->CreateRenderContributor();
        {
            std::lock_guard lock(Bridge().contributorMutex);
            Bridge().contributor = std::move(contributor);
        }
        Bridge().active = system_.get();
        return true;
    }

    IEffectSystem* Get() const noexcept { return system_.get(); }
    void Reset() noexcept
    {
        if (!system_) return;
        if (Bridge().active == system_.get())
        {
            Bridge().active = nullptr;
            std::lock_guard lock(Bridge().contributorMutex);
            Bridge().contributor.reset();
        }
        system_->Shutdown();
        system_.reset();
    }

private:
    std::unique_ptr<IEffectSystem> system_;
};
}
