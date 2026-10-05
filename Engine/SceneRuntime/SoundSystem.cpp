#include "SoundSystem.h"
#include "AudioListenerComponent.h"
#include "CameraComponent.h"
#include "LifecycleTrace.h"
#include "SoundComponent.h"
#include "Entity.h"
#include "Scene.h"
#include "Transform.h"
#include <algorithm>

void SoundSystem::Bind(Scene& scene, wave::PlaybackService* playback, AssetResolver resolver)
{
    EndWorld();
    m_scene = &scene;
    m_playback = playback;
    m_resolver = std::move(resolver);
    RefreshClipKeys();
}

void SoundSystem::Activate()
{
    if (m_playback)
    {
        if (!m_playback->IsScopeAlive(m_world))
        {
            m_world = m_playback->CreateScope(wave::ScopeKind::World);
        }
        if (!m_playback->IsScopeAlive(m_preview))
        {
            m_preview = m_playback->CreateScope(wave::ScopeKind::EditorPreview);
        }
        UpdateListener();
    }
}

void SoundSystem::EndWorld()
{
    if (m_playback)
    {
        m_playback->EndScope(m_world);
        m_playback->EndScope(m_preview);
    }
    for (const auto& attached : m_attached)
    {
        if (m_playback)
        {
            m_playback->OwnerDestroyed(attached.scope, attached.ownerId);
        }
    }
    m_attached.clear();
    m_world = {};
    m_preview = {};
    m_warnedFallback = false;
    m_warnedMultiple = false;
}

void SoundSystem::Register(SoundComponent* sound)
{
    if (sound && std::ranges::find(m_sounds, sound) == m_sounds.end())
    {
        m_sounds.push_back(sound);
    }
}

void SoundSystem::Unregister(SoundComponent* sound)
{
    std::erase(m_sounds, sound);
}

void SoundSystem::Register(AudioListenerComponent* listener)
{
    if (listener && std::ranges::find(m_listeners, listener) == m_listeners.end())
    {
        m_listeners.push_back(listener);
    }
}

void SoundSystem::Unregister(AudioListenerComponent* listener)
{
    std::erase(m_listeners, listener);
}

void SoundSystem::OwnerRemoved(std::uint64_t ownerId)
{
    if (m_playback)
    {
        m_playback->OwnerDestroyed(m_world, ownerId);
        m_playback->OwnerDestroyed(m_preview, ownerId);
    }
}

wave::ClipKey SoundSystem::ResolveAsset(std::string_view text, wave::SoundSourceKind kind) const
{
    Uuid::Uuid16 guid;
    if (Uuid::TryParse(text, guid) && !guid.IsNil())
    {
        return wave::ClipKey::FromGuid(guid);
    }
    if (kind == wave::SoundSourceKind::Clip && m_resolver)
    {
        return m_resolver(text);
    }
    if (!text.empty())
    {
        Debug::PrintLog(spdlog::level::err, "[audio.asset.invalid] Expected a GUID: " + std::string(text));
    }
    return {};
}

void SoundSystem::RefreshClipKeys()
{
    std::vector<std::string> keys;
    if (m_playback)
    {
        for (const auto& key : m_playback->Audio().ListClipKeys())
        {
            if (key.IsGuid())
            {
                keys.push_back(key.Text());
            }
        }
    }
    std::ranges::sort(keys);
    std::lock_guard lock(m_clipMutex);
    m_clipKeys = std::move(keys);
}

std::vector<std::string> SoundSystem::ClipKeys() const
{
    std::lock_guard lock(m_clipMutex);
    return m_clipKeys;
}

void SoundSystem::RefreshListener()
{
    UpdateListener();
}

void SoundSystem::UpdateListener()
{
    if (!m_playback || !m_scene || !m_playback->IsScopeAlive(m_world))
    {
        return;
    }
    AudioListenerComponent* selected = nullptr;
    std::size_t count = 0u;
    for (auto* listener : m_listeners)
    {
        if (listener)
        {
            listener->ApplyPendingSettings();
        }
        if (!listener || !listener->active || !listener->IsEnabled() ||
            !listener->GetOwner() || listener->GetOwner()->IsDestroyMark())
        {
            continue;
        }
        ++count;
        if (!selected || listener->GetInstanceID() < selected->GetInstanceID())
        {
            selected = listener;
        }
    }
    if (count > 1u && !m_warnedMultiple)
    {
        Debug::PrintLog(spdlog::level::warn,
            "[audio.listener.multiple] Lowest component instance ID selected; enable one listener per world");
        m_warnedMultiple = true;
    }
    if (selected)
    {
        m_playback->Audio().SetListener(selected->CaptureListener());
        return;
    }
    wave::ListenerState state;
    if (auto* camera = m_scene->Cameras().GetPrimaryCamera())
    {
        if (camera->GetOwner())
        {
            (void)m_scene->EnsureResolved(m_scene->HandleOf(camera->GetOwner()->m_index));
        }
        const auto snapshot = camera->CaptureFrameSnapshot();
        state.position = snapshot.eyePosition;
        state.forward = snapshot.forward;
        state.up = snapshot.up;
    }
    if (!m_warnedFallback)
    {
        Debug::PrintLog(spdlog::level::warn,
            "[audio.listener.legacy-fallback] Add an AudioListenerComponent; using the primary camera or origin");
        m_warnedFallback = true;
    }
    m_playback->Audio().SetListener(state);
}

void SoundSystem::TrackAttached(Entity& owner, wave::PlaybackScope scope, wave::PlaybackHandle playback)
{
    if (m_scene && playback.IsValid())
    {
        m_attached.push_back({ m_scene->HandleOf(owner.m_index), owner.GetInstanceID(), scope, playback });
    }
}

void SoundSystem::Update(float tick, bool recordLifecycle)
{
    if (m_playback && m_scene)
    {
        std::erase_if(m_attached, [&](const AttachedPlayback& attached)
        {
            if (!m_playback->IsAlive(attached.playback))
            {
                return true;
            }
            const auto* owner = m_scene->Resolve(attached.entity);
            if (!owner || owner->IsDestroyMark() || owner->GetScene() != m_scene)
            {
                m_playback->OwnerDestroyed(attached.scope, attached.ownerId);
                return true;
            }
            if (const auto* transform = owner->GetComponent<Transform>(); transform &&
                m_scene->EnsureResolved(attached.entity))
            {
                m_playback->SetTransform(attached.playback, transform->GetWorldPosition(), {});
            }
            return false;
        });
    }
    UpdateListener();
    for (SoundComponent* sound : m_sounds)
    {
        if (!sound || !sound->GetOwner() || sound->GetOwner()->IsDestroyMark())
        {
            continue;
        }
        if (recordLifecycle && sound->IsEnabled())
        {
            LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(sound),
                sound->GetOwner()->m_name.ToString().c_str(), sound->GetInstanceID());
        }
        sound->TickUpdate(tick);
    }
}

void SoundSystem::LateUpdate(float tick)
{
    for (SoundComponent* sound : m_sounds)
    {
        if (!sound || !sound->GetOwner() || sound->GetOwner()->IsDestroyMark() || !sound->IsEnabled())
        {
            continue;
        }
        LIFECYCLE_TRACE(Lifecycle::Phase::LateUpdate, Lifecycle::Trace::TypeNameOf(sound),
            sound->GetOwner()->m_name.ToString().c_str(), sound->GetInstanceID());
        sound->TickLateUpdate(tick);
    }
}
