#include "UITickSystem.h"
#include "UIButton.h"
#include "LifecycleTrace.h"
#include "SpriteSheetComponent.h"
#include "TextComponent.h"
#include "Canvas.h"
#include "ImageComponent.h"
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

void UITickSystem::RegisterButton(UIButton* component)
{
    if (nullptr == component) return;

    if (std::ranges::find(m_buttons, component) != m_buttons.end()) return;
    m_buttons.push_back(component);
}

void UITickSystem::UnregisterButton(UIButton* component)
{
    if (nullptr == component) return;

    for (size_t i = 0; i < m_buttons.size(); ++i)
    {
        if (m_buttons[i] != component) continue;
        m_buttons[i] = m_buttons.back();
        m_buttons.pop_back();
        return;
    }
}

void UITickSystem::RegisterCanvas(Canvas* component)
{
    if (nullptr == component) return;

    if (std::ranges::find(m_canvases, component) != m_canvases.end()) return;
    m_canvases.push_back(component);
}

void UITickSystem::UnregisterCanvas(Canvas* component)
{
    if (nullptr == component) return;

    for (size_t i = 0; i < m_canvases.size(); ++i)
    {
        if (m_canvases[i] != component) continue;
        m_canvases[i] = m_canvases.back();
        m_canvases.pop_back();
        return;
    }
}

void UITickSystem::RegisterImage(ImageComponent* component)
{
    if (nullptr == component) return;

    if (std::ranges::find(m_images, component) != m_images.end()) return;
    m_images.push_back(component);
}

void UITickSystem::UnregisterImage(ImageComponent* component)
{
    if (nullptr == component) return;

    for (size_t i = 0; i < m_images.size(); ++i)
    {
        if (m_images[i] != component) continue;
        m_images[i] = m_images.back();
        m_images.pop_back();
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
        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        // 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다(같은 문자열을 써야
        // 대조가 성립하므로 Lifecycle::Trace::TypeNameOf 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(sheet),
            owner->m_name.ToString().c_str(), sheet->GetInstanceID());

        sheet->TickLayout(tick);
    }

    for (TextComponent* text : m_texts)
    {
        if (nullptr == text) continue;

        GameObject* owner = text->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!text->IsEnabled()) continue;
        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        // 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다(같은 문자열을 써야
        // 대조가 성립하므로 Lifecycle::Trace::TypeNameOf 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(text),
            owner->m_name.ToString().c_str(), text->GetInstanceID());

        text->TickLayout(tick);
    }

    for (UIButton* button : m_buttons)
    {
        if (nullptr == button) continue;

        GameObject* owner = button->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!button->IsEnabled()) continue;

        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(button),
            owner->m_name.ToString().c_str(), button->GetInstanceID());

        button->TickInteraction(tick);
    }

    for (Canvas* canvas : m_canvases)
    {
        if (nullptr == canvas) continue;

        GameObject* owner = canvas->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!canvas->IsEnabled()) continue;

        // 레인 UI — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(canvas),
            owner->m_name.ToString().c_str(), canvas->GetInstanceID());

        canvas->TickCanvasOrder(tick);
    }

    for (ImageComponent* image : m_images)
    {
        if (nullptr == image) continue;

        GameObject* owner = image->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!image->IsEnabled()) continue;

        // 레인 UI — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(image),
            owner->m_name.ToString().c_str(), image->GetInstanceID());

        image->TickLayout(tick);
    }
}
