#pragma once
#include "Audio/PlaybackService.h"
#include "EntityHandle.h"
#include <functional>
#include <mutex>
#include <string_view>
#include <vector>

class Scene;
class Entity;
class SoundComponent;
class AudioListenerComponent;

// Scene-owned non-owning source/listener registry. The Host owns playback.
class SoundSystem final
{
public:
    using AssetResolver = std::function<wave::ClipKey(std::string_view)>;
    void Bind(Scene& scene, wave::PlaybackService* playback, AssetResolver resolver = {});
    void Activate();
    void EndWorld();
    void Register(SoundComponent* sound);
    void Unregister(SoundComponent* sound);
    void Register(AudioListenerComponent* listener);
    void Unregister(AudioListenerComponent* listener);
    void OwnerRemoved(std::uint64_t ownerId);
    void TrackAttached(Entity& owner, wave::PlaybackScope scope, wave::PlaybackHandle playback);
    void Update(float tick, bool recordLifecycle = true);
    void LateUpdate(float tick);
    void RefreshClipKeys();
    void RefreshListener();
    [[nodiscard]] std::vector<std::string> ClipKeys() const;
    [[nodiscard]] wave::ClipKey ResolveAsset(std::string_view text, wave::SoundSourceKind kind) const;
    [[nodiscard]] wave::PlaybackService* Playback() const noexcept { return m_playback; }
    [[nodiscard]] wave::PlaybackScope WorldScope() const noexcept { return m_world; }
    [[nodiscard]] wave::PlaybackScope PreviewScope() const noexcept { return m_preview; }
    [[nodiscard]] size_t GetCount() const noexcept { return m_sounds.size(); }

private:
    void UpdateListener();
    struct AttachedPlayback
    {
        EntityHandle entity;
        std::uint64_t ownerId{};
        wave::PlaybackScope scope;
        wave::PlaybackHandle playback;
    };
    std::vector<AttachedPlayback> m_attached;
    Scene* m_scene{};
    wave::PlaybackService* m_playback{};
    AssetResolver m_resolver;
    wave::PlaybackScope m_world;
    wave::PlaybackScope m_preview;
    std::vector<SoundComponent*> m_sounds;
    std::vector<AudioListenerComponent*> m_listeners;
    mutable std::mutex m_clipMutex;
    std::vector<std::string> m_clipKeys;
    bool m_warnedFallback{};
    bool m_warnedMultiple{};
};
