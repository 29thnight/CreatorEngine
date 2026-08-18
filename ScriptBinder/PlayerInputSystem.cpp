#include "PlayerInputSystem.h"
#include "PlayerInput.h"
#include "GameObject.h"
#include "LifecycleTrace.h"
#include <algorithm>

void PlayerInputSystem::Register(PlayerInputComponent* component)
{
    if (nullptr == component) return;

    if (std::ranges::find(m_inputs, component) != m_inputs.end()) return;
    m_inputs.push_back(component);
}

void PlayerInputSystem::Unregister(PlayerInputComponent* component)
{
    if (nullptr == component) return;

    // swap-and-pop — 다른 시스템들과 같은 규약.
    for (size_t i = 0; i < m_inputs.size(); ++i)
    {
        if (m_inputs[i] != component) continue;
        m_inputs[i] = m_inputs.back();
        m_inputs.pop_back();
        return;
    }
}

void PlayerInputSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/비활성
    // 스킵)를 이 시스템이 대신 적용한다 — 같은 순서를 지킨다.
    for (PlayerInputComponent* input : m_inputs)
    {
        if (nullptr == input) continue;

        GameObject* owner = input->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!input->IsEnabled()) continue;

        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(input),
            owner->m_name.ToString().c_str(), input->GetInstanceID());

        input->TickInput(tick);
    }
}
