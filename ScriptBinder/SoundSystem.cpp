#include "SoundSystem.h"
#include "SoundComponent.h"
#include "GameObject.h"
#include <algorithm>

void SoundSystem::Register(SoundComponent* sound)
{
    if (nullptr == sound) return;

    if (std::ranges::find(m_sounds, sound) != m_sounds.end()) return;
    m_sounds.push_back(sound);
}

void SoundSystem::Unregister(SoundComponent* sound)
{
    if (nullptr == sound) return;

    // swap-and-pop — AnimatorSystem::Unregister와 같은 규약(단일 조밀 벡터라
    // 순서 보존은 불필요하고, erase는 O(n) 시프트라 씬 전환마다 비용이 쌓인다).
    for (size_t i = 0; i < m_sounds.size(); ++i)
    {
        if (m_sounds[i] != sound) continue;
        m_sounds[i] = m_sounds.back();
        m_sounds.pop_back();
        return;
    }
}

void SoundSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다.
    for (SoundComponent* sound : m_sounds)
    {
        if (nullptr == sound) continue;

        GameObject* owner = sound->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!sound->IsEnabled()) continue;

        sound->TickUpdate(tick);
    }
}

void SoundSystem::LateUpdate(float tick)
{
    for (SoundComponent* sound : m_sounds)
    {
        if (nullptr == sound) continue;

        GameObject* owner = sound->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!sound->IsEnabled()) continue;

        sound->TickLateUpdate(tick);
    }
}
