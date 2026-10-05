#include "AudioListenerComponent.h"
#include "Entity.h"
#include "Scene.h"
#include "Transform.h"

void AudioListenerComponent::OnAddedToScene()
{
    if (GetOwner() && GetOwner()->GetScene())
    {
        GetOwner()->GetScene()->Sounds().Register(this);
    }
}

void AudioListenerComponent::OnRemovingFromScene()
{
    if (GetOwner() && GetOwner()->GetScene())
    {
        GetOwner()->GetScene()->Sounds().Unregister(this);
    }
}

wave::ListenerState AudioListenerComponent::CaptureListener() const
{
    wave::ListenerState listener;
    listener.velocity = velocity;
    if (GetOwner())
    {
        if (const auto* transform = GetOwner()->GetComponent<Transform>())
        {
            if (auto* scene = GetOwner()->GetScene(); scene &&
                !scene->EnsureResolved(scene->HandleOf(GetOwner()->m_index)))
            {
                return listener;
            }
            listener.position = transform->GetWorldPosition();
            const auto rotation = math::normalize(transform->GetWorldQuaternion());
            listener.forward = math::rotate(math::vector3{ 0.0f, 0.0f, 1.0f }, rotation);
            listener.up = math::rotate(math::vector3{ 0.0f, 1.0f, 0.0f }, rotation);
        }
    }
    return listener;
}

AudioListenerComponent::Settings AudioListenerComponent::ReadSettings() const
{
    std::lock_guard lock(m_settingsMutex);
    return m_pending.value_or(Settings{ active, velocity });
}

void AudioListenerComponent::QueueSettings(Settings settings)
{
    std::lock_guard lock(m_settingsMutex);
    m_pending = settings;
}

void AudioListenerComponent::ApplyPendingSettings()
{
    std::lock_guard lock(m_settingsMutex);
    if (m_pending)
    {
        active = m_pending->active;
        velocity = m_pending->velocity;
        m_pending.reset();
    }
}

void AudioListenerComponent::SetActive(bool value)
{
    std::lock_guard lock(m_settingsMutex);
    active = value;
    if (m_pending)
    {
        m_pending->active = value;
    }
}
