#include "UITickSystem.h"
#include "SpriteSheetComponent.h"
#include "TextComponent.h"
#include "GameObject.h"
#include <algorithm>

void UITickSystem::RegisterSpriteSheet(SpriteSheetComponent* component)
{
    if (nullptr == component) return;

    if (std::ranges::find(m_spriteSheets, component) != m_spriteSheets.end()) return;
    m_spriteSheets.push_back(component);
}

void UITickSystem::UnregisterSpriteSheet(SpriteSheetComponent* component)
{
    if (nullptr == component) return;

    // swap-and-pop — AnimatorSystem::Unregister·SystemSchedule_Internal::RemoveFromList과
    // 같은 규약(같은 축의 여러 리스트에 걸쳐 있지 않은 단일 조밀 벡터라 순서 보존은
    // 애초에 불필요하고, erase는 O(n) 시프트라 씬 전환마다 비용이 쌓인다).
    for (size_t i = 0; i < m_spriteSheets.size(); ++i)
    {
        if (m_spriteSheets[i] != component) continue;
        m_spriteSheets[i] = m_spriteSheets.back();
        m_spriteSheets.pop_back();
        return;
    }
}

void UITickSystem::RegisterText(TextComponent* component)
{
    if (nullptr == component) return;

    if (std::ranges::find(m_texts, component) != m_texts.end()) return;
    m_texts.push_back(component);
}

void UITickSystem::UnregisterText(TextComponent* component)
{
    if (nullptr == component) return;

    for (size_t i = 0; i < m_texts.size(); ++i)
    {
        if (m_texts[i] != component) continue;
        m_texts[i] = m_texts.back();
        m_texts.pop_back();
        return;
    }
}

void UITickSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다 — 두 컴포넌트가 더 이상
    // m_schedule.UpdateList()를 거치지 않으므로 그 가드도 함께 옮겨왔다.
    for (SpriteSheetComponent* sheet : m_spriteSheets)
    {
        if (nullptr == sheet) continue;

        GameObject* owner = sheet->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!sheet->IsEnabled()) continue;

        sheet->TickLayout(tick);
    }

    for (TextComponent* text : m_texts)
    {
        if (nullptr == text) continue;

        GameObject* owner = text->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!text->IsEnabled()) continue;

        text->TickLayout(tick);
    }
}
