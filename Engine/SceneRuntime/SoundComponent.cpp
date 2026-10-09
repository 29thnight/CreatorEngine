#include "SoundComponent.h"
#include "SoundSystem.h"
#include "Entity.h"
#include "Scene.h"
#include "Transform.h"
#include <algorithm>

SoundSystem* SoundComponent::System() const
{
    return GetOwner() && GetOwner()->GetScene() ? &GetOwner()->GetScene()->Sounds() : nullptr;
}

SoundComponentSettings SoundComponent::ReadSettings() const
{
    std::lock_guard lock(m_editorMutex);
    if (m_pendingSettings)
    {
        return *m_pendingSettings;
    }
    SoundComponentSettings settings;
    settings.clipKey = clipKey;
    settings.soundPresetKey = soundPresetKey;
    settings.soundGraphKey = soundGraphKey;
    settings.sourceKind = sourceKind;
    settings.overridePresetSettings = overridePresetSettings;
    settings.bus = bus;
    settings.volume = volume;
    settings.pitch = pitch;
    settings.priority = priority;
    settings.concurrencyGroup = concurrencyGroup;
    settings.preemptSameClip = preemptSameClip;
    settings.allowVirtualization = allowVirtualization;
    settings.spatialBlend = spatialBlend;
    settings.minDistance = minDistance;
    settings.maxDistance = maxDistance;
    settings.reverbLevel = reverbLevel;
    settings.reverbIndex = reverbIndex;
    settings.reverbBus = reverbBus;
    settings.rolloff = rolloff;
    settings.localRolloffCurve = localRolloffCurve;
    settings.loop = loop;
    settings.playOnStart = playOnStart;
    settings.spatial = spatial;
    settings.useReverbSend = useReverbSend;
    settings.ownerDestroyedPolicy = ownerDestroyedPolicy;
    return settings;
}

void SoundComponent::WriteSettings(const SoundComponentSettings& settings)
{
    clipKey = settings.clipKey;
    soundPresetKey = settings.soundPresetKey;
    soundGraphKey = settings.soundGraphKey;
    sourceKind = settings.sourceKind;
    overridePresetSettings = settings.overridePresetSettings;
    bus = settings.bus;
    volume = settings.volume;
    pitch = settings.pitch;
    priority = settings.priority;
    concurrencyGroup = settings.concurrencyGroup;
    preemptSameClip = settings.preemptSameClip;
    allowVirtualization = settings.allowVirtualization;
    spatialBlend = settings.spatialBlend;
    minDistance = settings.minDistance;
    maxDistance = settings.maxDistance;
    reverbLevel = settings.reverbLevel;
    reverbIndex = settings.reverbIndex;
    reverbBus = settings.reverbBus;
    rolloff = settings.rolloff;
    localRolloffCurve = settings.localRolloffCurve;
    loop = settings.loop;
    playOnStart = settings.playOnStart;
    spatial = settings.spatial;
    useReverbSend = settings.useReverbSend;
    ownerDestroyedPolicy = settings.ownerDestroyedPolicy;
}

void SoundComponent::ApplySettings(const SoundComponentSettings& settings)
{
    SoundComponentSettings previous;
    {
        std::lock_guard lock(m_editorMutex);
        previous.sourceKind = sourceKind;
        previous.clipKey = clipKey;
        previous.soundPresetKey = soundPresetKey;
        previous.soundGraphKey = soundGraphKey;
        previous.overridePresetSettings = overridePresetSettings;
        WriteSettings(settings);
        m_pendingSettings.reset();
    }
    SettingsChanged(previous);
}

void SoundComponent::SettingsChanged(const SoundComponentSettings& previous)
{
    const auto current = ReadSettings();
    const bool sourceChanged = current.sourceKind != previous.sourceKind || current.clipKey != previous.clipKey ||
        current.soundPresetKey != previous.soundPresetKey || current.soundGraphKey != previous.soundGraphKey ||
        (current.sourceKind == wave::SoundSourceKind::Preset &&
            current.overridePresetSettings != previous.overridePresetSettings);
    if (sourceChanged)
    {
        if (auto* system = System(); system && system->Playback())
        {
            const auto state = system->Playback()->State(m_playback);
            if (state != wave::PlaybackState::Stopped)
            {
                (void)PlayInstance();
                system->Playback()->SetPaused(m_playback, state == wave::PlaybackState::Paused);
            }
            const auto previewState = system->Playback()->State(m_previewPlayback);
            if (previewState != wave::PlaybackState::Stopped)
            {
                system->Playback()->Stop(m_previewPlayback);
                m_previewPlayback = system->Playback()->Play(system->PreviewScope(), Request(current));
                system->Playback()->SetPaused(m_previewPlayback, previewState == wave::PlaybackState::Paused);
            }
        }
    }
    PushSettings();
}

void SoundComponent::SetVolume(float value)
{
    auto settings = ReadSettings();
    settings.volume = value;
    settings.overridePresetSettings = true;
    ApplySettings(settings);
}

void SoundComponent::SetPitch(float value)
{
    auto settings = ReadSettings();
    settings.pitch = value;
    settings.overridePresetSettings = true;
    ApplySettings(settings);
}

void SoundComponent::SetClipKey(std::string value)
{
    auto settings = ReadSettings();
    settings.clipKey = std::move(value);
    settings.sourceKind = wave::SoundSourceKind::Clip;
    ApplySettings(settings);
}

void SoundComponent::QueueSettings(SoundComponentSettings settings)
{
    std::lock_guard lock(m_editorMutex);
    m_pendingSettings = std::move(settings);
}

void SoundComponent::MarkPreviewVisible()
{
    std::lock_guard lock(m_editorMutex);
    m_previewVisibleUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
}

void SoundComponent::QueuePreview(PreviewCommand command, std::string clipOverride)
{
    std::lock_guard lock(m_editorMutex);
    m_previewVisibleUntil = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    // Preserve stop even when repeated preview clicks saturate the mailbox.
    if (command == PreviewCommand::Stop)
    {
        m_previewCommands.clear();
    }
    if (m_previewCommands.size() < 64u)
    {
        m_previewCommands.push_back({ command, std::move(clipOverride) });
    }
}

void SoundComponent::OnAddedToScene()
{
    if (auto* system = System())
    {
        system->Register(this);
    }
}

void SoundComponent::OnRemovingFromScene()
{
    if (auto* system = System())
    {
        system->OwnerRemoved(GetInstanceID());
        system->Unregister(this);
    }
    m_playback = {};
    m_previewPlayback = {};
    m_oneShots.clear();
    m_previews.clear();
    std::lock_guard lock(m_editorMutex);
    m_previewCommands.clear();
    m_pendingSettings.reset();
}

void SoundComponent::OnBeginSimulation()
{
    if (ReadSettings().playOnStart)
    {
        Play();
    }
}

void SoundComponent::OnEndSimulation()
{
    if (auto* system = System())
    {
        system->OwnerRemoved(GetInstanceID());
    }
    m_playback = {};
    m_oneShots.clear();
}

void SoundComponent::OnUninitializing()
{
    Stop();
}

wave::PlaybackRequest SoundComponent::Request(const SoundComponentSettings& settings) const
{
    wave::PlaybackRequest request;
    request.source.kind = settings.sourceKind;
    const auto& key = settings.sourceKind == wave::SoundSourceKind::Preset ? settings.soundPresetKey :
        (settings.sourceKind == wave::SoundSourceKind::Graph ? settings.soundGraphKey : settings.clipKey);
    if (auto* system = System())
    {
        request.source.asset = system->ResolveAsset(key, settings.sourceKind);
    }
    auto& voice = request.settings;
    switch (settings.bus)
    {
    case ChannelType::BGM: voice.bus = wave::Buses::BGM; break;
    case ChannelType::SFX: voice.bus = wave::Buses::SFX; break;
    case ChannelType::PLAYER: voice.bus = wave::Buses::Player; break;
    case ChannelType::MONSTER: voice.bus = wave::Buses::Monster; break;
    case ChannelType::UI: voice.bus = wave::Buses::UI; break;
    default: voice.bus = wave::Buses::SFX; break;
    }
    voice.volume = settings.volume;
    voice.pitch = settings.pitch;
    voice.priority = settings.priority;
    voice.concurrencyGroup = { settings.concurrencyGroup };
    voice.preemptSameClip = settings.preemptSameClip;
    voice.allowVirtualization = settings.allowVirtualization;
    voice.loop = settings.loop;
    voice.persistent = settings.loop;
    voice.spatialBlend = settings.spatial ? settings.spatialBlend : 0.0f;
    voice.minimumDistance = settings.minDistance;
    voice.maximumDistance = settings.maxDistance;
    voice.position = position;
    voice.velocity = velocity;
    voice.rolloff = static_cast<wave::RolloffKind>(settings.rolloff);
    for (const auto& point : settings.localRolloffCurve)
    {
        voice.customRolloff.push_back({ point.distance, point.gain });
    }
    // The old four effect slots had no authored effect objects. All valid
    // legacy indices migrate explicitly to the tested named Room effect bus.
    voice.useReverbSend = settings.useReverbSend;
    voice.reverbSendDecibels = settings.reverbLevel;
    voice.reverbBus = wave::Buses::Room;
    if (settings.useReverbSend && ((!settings.reverbBus.empty() && settings.reverbBus != "Room") ||
        settings.reverbIndex < 0 || settings.reverbIndex > 3))
    {
        Debug::PrintLog(spdlog::level::err, "[audio.reverb.migration] Unsupported legacy slot or named send: " + settings.reverbBus);
        request.source.asset = {};
    }
    voice.ownerId = GetInstanceID();
    request.ownerPolicy = settings.ownerDestroyedPolicy;
    request.overridePresetSettings = settings.overridePresetSettings;
    return request;
}

wave::PlaybackHandle SoundComponent::PlayInstance(bool oneShot)
{
    auto* system = System();
    if (!system || !system->Playback() || !system->Playback()->IsScopeAlive(system->WorldScope()))
    {
        return {};
    }
    system->RefreshListener();
    if (!EditorSet())
    {
        return {};
    }
    auto settings = ReadSettings();
    auto request = Request(settings);
    if (settings.sourceKind == wave::SoundSourceKind::Clip && request.source.asset.IsGuid() &&
        settings.clipKey != request.source.asset.Text())
    {
        settings.clipKey = request.source.asset.Text();
        std::lock_guard lock(m_editorMutex);
        WriteSettings(settings);
    }
    if (oneShot)
    {
        request.settings.loop = false;
        request.settings.persistent = false;
        request.loopOverride = false;
    }
    else
    {
        system->Playback()->Stop(m_playback);
    }
    const auto playback = system->Playback()->Play(system->WorldScope(), request);
    if (oneShot)
    {
        m_oneShots.push_back(playback);
    }
    else
    {
        m_playback = playback;
    }
    return playback;
}

void SoundComponent::Play()
{
    (void)PlayInstance();
}

void SoundComponent::PlayOneShot()
{
    (void)PlayInstance(true);
}

void SoundComponent::StopTracked()
{
    if (auto* system = System(); system && system->Playback())
    {
        auto& playback = *system->Playback();
        playback.Stop(m_playback);
        playback.Stop(m_previewPlayback);
        for (const auto handle : m_oneShots)
        {
            playback.Stop(handle);
        }
        for (const auto handle : m_previews)
        {
            playback.Stop(handle);
        }
    }
    m_playback = {};
    m_previewPlayback = {};
    m_oneShots.clear();
    m_previews.clear();
}

void SoundComponent::Stop()
{
    StopTracked();
}

void SoundComponent::Pause(bool pause)
{
    if (auto* system = System(); system && system->Playback())
    {
        auto& playback = *system->Playback();
        playback.SetPaused(m_playback, pause);
        for (const auto handle : m_oneShots)
        {
            playback.SetPaused(handle, pause);
        }
    }
}

bool SoundComponent::IsPlaying()
{
    if (auto* system = System(); system && system->Playback())
    {
        return system->Playback()->IsAlive(m_playback) ||
            std::ranges::any_of(m_oneShots, [&](auto handle) { return system->Playback()->IsAlive(handle); });
    }
    return false;
}

void SoundComponent::PushSettings()
{
    if (auto* system = System(); system && system->Playback())
    {
        const auto settings = ReadSettings();
        system->Playback()->SetOwnerPolicy(m_playback, settings.ownerDestroyedPolicy);
        system->Playback()->SetOwnerPolicy(m_previewPlayback, settings.ownerDestroyedPolicy);
        for (const auto handle : m_oneShots)
        {
            system->Playback()->SetOwnerPolicy(handle, settings.ownerDestroyedPolicy);
        }
        for (const auto handle : m_previews)
        {
            system->Playback()->SetOwnerPolicy(handle, settings.ownerDestroyedPolicy);
        }
        if (settings.sourceKind == wave::SoundSourceKind::Preset && !settings.overridePresetSettings)
        {
            return;
        }
        auto& playback = *system->Playback();
        const std::string previousError = playback.LastError();
        std::string rejectedEdit;
        const auto apply = [&](wave::PlaybackHandle handle, const wave::PlayRequest& values)
        {
            if (!playback.IsAlive(handle))
            {
                return;
            }
            playback.SetSettings(handle, values);
            if (rejectedEdit.empty() && !playback.LastError().empty())
            {
                rejectedEdit = playback.LastError();
            }
        };
        auto request = Request(settings);
        apply(m_playback, request.settings);
        apply(m_previewPlayback, request.settings);
        request.settings.loop = false;
        request.settings.persistent = false;
        for (const auto handle : m_oneShots)
        {
            apply(handle, request.settings);
        }
        for (const auto handle : m_previews)
        {
            apply(handle, request.settings);
        }
        if (!rejectedEdit.empty() && rejectedEdit != previousError)
        {
            // This runs on the game thread after the runtime has kept the
            // affected playback's prior values. The Inspector keeps authored
            // values for the next Play, so rejection must remain visible.
            Debug::PrintLog(spdlog::level::warn, "[audio.settings.rejected] component=" +
                std::to_string(GetInstanceID()) + " " + rejectedEdit);
        }
    }
}

void SoundComponent::DrainEditorCommands()
{
    std::vector<QueuedPreview> commands;
    bool changed = false;
    SoundComponentSettings previous;
    {
        std::lock_guard lock(m_editorMutex);
        if (m_pendingSettings)
        {
            previous.sourceKind = sourceKind;
            previous.clipKey = clipKey;
            previous.soundPresetKey = soundPresetKey;
            previous.soundGraphKey = soundGraphKey;
            previous.overridePresetSettings = overridePresetSettings;
            WriteSettings(*m_pendingSettings);
            m_pendingSettings.reset();
            changed = true;
        }
        commands.swap(m_previewCommands);
        if ((m_previewPlayback.IsValid() || !m_previews.empty()) &&
            std::chrono::steady_clock::now() > m_previewVisibleUntil)
        {
            commands.push_back({ PreviewCommand::Stop, {} });
        }
    }
    if (changed)
    {
        SettingsChanged(previous);
    }
    auto* system = System();
    if (!system || !system->Playback())
    {
        return;
    }
    for (const auto& command : commands)
    {
        if (command.command == PreviewCommand::Stop)
        {
            system->Playback()->Stop(m_previewPlayback);
            m_previewPlayback = {};
            for (const auto handle : m_previews)
            {
                system->Playback()->Stop(handle);
            }
            m_previews.clear();
            continue;
        }
        if (command.command == PreviewCommand::Pause || command.command == PreviewCommand::Resume)
        {
            system->Playback()->SetPaused(m_previewPlayback, command.command == PreviewCommand::Pause);
            for (const auto handle : m_previews)
            {
                system->Playback()->SetPaused(handle, command.command == PreviewCommand::Pause);
            }
            continue;
        }
        auto settings = ReadSettings();
        if (!command.clip.empty())
        {
            settings.clipKey = command.clip;
            settings.sourceKind = wave::SoundSourceKind::Clip;
        }
        if (command.command == PreviewCommand::OneShot)
        {
            settings.loop = false;
        }
        else
        {
            system->Playback()->Stop(m_previewPlayback);
        }
        auto request = Request(settings);
        if (command.command == PreviewCommand::OneShot)
        {
            request.loopOverride = false;
        }
        const auto handle = system->Playback()->Play(system->PreviewScope(), request);
        if (command.command == PreviewCommand::OneShot)
        {
            m_previews.push_back(handle);
        }
        else
        {
            m_previewPlayback = handle;
        }
    }
}

bool SoundComponent::EditorSet()
{
    if (GetOwner())
    {
        if (const auto* transform = GetOwner()->GetComponent<Transform>())
        {
            if (auto* scene = GetOwner()->GetScene(); scene &&
                !scene->EnsureResolved(scene->HandleOf(GetOwner()->m_index)))
            {
                return false;
            }
            position = transform->GetWorldPosition();
        }
    }
    return true;
}

void SoundComponent::TickUpdate(float)
{
    EditorSet();
    DrainEditorCommands();
    if (auto* system = System(); system && system->Playback())
    {
        auto& playback = *system->Playback();
        playback.SetTransform(m_playback, position, velocity);
        playback.SetTransform(m_previewPlayback, position, velocity);
        for (const auto handle : m_previews)
        {
            playback.SetTransform(handle, position, velocity);
        }
        for (const auto handle : m_oneShots)
        {
            playback.SetTransform(handle, position, velocity);
        }
        std::erase_if(m_oneShots, [&](auto handle) { return !playback.IsAlive(handle); });
        std::erase_if(m_previews, [&](auto handle) { return !playback.IsAlive(handle); });
    }
}

void SoundComponent::TickLateUpdate(float)
{
    // All attenuation and spatial blending belongs to the audio runtime.
}
