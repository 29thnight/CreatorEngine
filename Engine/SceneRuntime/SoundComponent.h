#pragma once
#include "Component.h"
#include "SoundDefinition.h"
#include "Audio/PlaybackService.h"
#include <chrono>
#include <mutex>
#include <optional>

// Value snapshot used by Inspector. Serialized component field names stay unchanged.
struct SoundComponentSettings final
{
    std::string clipKey;
    std::string soundPresetKey;
    std::string soundGraphKey;
    wave::SoundSourceKind sourceKind{ wave::SoundSourceKind::Clip };
    bool overridePresetSettings{ false };
    ChannelType bus{ ChannelType::SFX };
    float volume{ 1.0f };
    float pitch{ 1.0f };
    int priority{ 128 };
    std::uint32_t concurrencyGroup{};
    bool preemptSameClip{ false };
    bool allowVirtualization{ true };
    float spatialBlend{ 1.0f };
    float minDistance{ 1.0f };
    float maxDistance{ 50.0f };
    float reverbLevel{ 0.0f };
    int reverbIndex{ 0 }; // Legacy input: slots 0..3 migrate to the named Room send.
    std::string reverbBus{ "Room" };
    Rolloff rolloff{ Rolloff::Inverse };
    std::vector<CurvePoint> localRolloffCurve;
    bool loop{ false };
    bool playOnStart{ false };
    bool spatial{ false };
    bool useReverbSend{ false };
    wave::OwnerDestroyedPolicy ownerDestroyedPolicy{ wave::OwnerDestroyedPolicy::Stop };
};

class [[reflgen::reflect]] SoundComponent : public meta::identity<SoundComponent, Component>
{
public:
    void OnBeginSimulation() override;
    void OnEndSimulation() override;
    void OnUninitializing() override;
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void TickUpdate(float tick);
    void TickLateUpdate(float tick);

    [[reflgen::reflect]]
    void Play();

    [[reflgen::reflect]]
    void Stop();

    [[reflgen::reflect]]
    void Pause(bool pause);

    [[reflgen::reflect, creator::read_only_in_inspector]]
    bool IsPlaying();

    [[reflgen::reflect]]
    void PlayOneShot();

    [[nodiscard]] wave::PlaybackHandle PlayInstance(bool oneShot = false);
    [[nodiscard]] wave::PlaybackHandle CurrentPlayback() const noexcept { return m_playback; }
    [[nodiscard]] SoundComponentSettings ReadSettings() const;
    void ApplySettings(const SoundComponentSettings& settings);
    void SetVolume(float value);
    void SetPitch(float value);
    void SetClipKey(std::string value);

    // Inspector runs on the presentation thread. These publish values only;
    // SoundSystem consumes them on the game thread before it talks to audio.
    void QueueSettings(SoundComponentSettings settings);
    void MarkPreviewVisible();
    enum class PreviewCommand { Play, OneShot, Stop, Pause, Resume };
    void QueuePreview(PreviewCommand command, std::string clipOverride = {});
    bool EditorSet();

    std::string clipKey;
    std::string soundPresetKey;
    std::string soundGraphKey;
    wave::SoundSourceKind sourceKind{ wave::SoundSourceKind::Clip };
    bool overridePresetSettings{ false };
    ChannelType bus{ ChannelType::SFX };
    float volume{ 1.0f };
    float pitch{ 1.0f };
    int priority{ 128 };
    std::uint32_t concurrencyGroup{};
    bool preemptSameClip{ false };
    bool allowVirtualization{ true };
    float spatialBlend{ 1.0f };
    float minDistance{ 1.0f };
    float maxDistance{ 50.0f };
    float reverbLevel{ 0.0f };
    int reverbIndex{ 0 }; // Legacy input: slots 0..3 migrate to the named Room send.
    std::string reverbBus{ "Room" };
    Rolloff rolloff{ Rolloff::Inverse };
    std::vector<CurvePoint> localRolloffCurve;
    bool loop{ false };
    bool playOnStart{ false };
    bool spatial{ false };
    bool useReverbSend{ false };
    wave::OwnerDestroyedPolicy ownerDestroyedPolicy{ wave::OwnerDestroyedPolicy::Stop };

    [[reflgen::ignore]]
    math::vector3 position{ 0.0f, 0.0f, 0.0f };

    math::vector3 velocity{ 0.0f, 0.0f, 0.0f };

private:
    [[nodiscard]] class SoundSystem* System() const;
    [[nodiscard]] wave::PlaybackRequest Request(const SoundComponentSettings& settings) const;
    void PushSettings();
    void SettingsChanged(const SoundComponentSettings& previous);
    void DrainEditorCommands();
    void StopTracked();
    void WriteSettings(const SoundComponentSettings& settings);
    struct QueuedPreview
    {
        PreviewCommand command;
        std::string clip;
    };

    [[reflgen::ignore]]
    wave::PlaybackHandle m_playback;

    [[reflgen::ignore]]
    std::vector<wave::PlaybackHandle> m_oneShots;

    [[reflgen::ignore]]
    wave::PlaybackHandle m_previewPlayback;

    [[reflgen::ignore]]
    std::vector<wave::PlaybackHandle> m_previews;

    [[reflgen::ignore]]
    mutable std::mutex m_editorMutex;

    [[reflgen::ignore]]
    std::optional<SoundComponentSettings> m_pendingSettings;

    [[reflgen::ignore]]
    std::vector<QueuedPreview> m_previewCommands;

    [[reflgen::ignore]]
    std::chrono::steady_clock::time_point m_previewVisibleUntil;
};
