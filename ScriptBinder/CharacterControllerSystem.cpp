#include "CharacterControllerSystem.h"
#include "LifecycleTrace.h"
#include "CharacterControllerComponent.h"
#include "GameObject.h"
#include <algorithm>

void CharacterControllerSystem::Register(CharacterControllerComponent* controller)
{
    if (nullptr == controller) return;

    if (std::ranges::find(m_controllers, controller) != m_controllers.end()) return;
    m_controllers.push_back(controller);
}

void CharacterControllerSystem::Unregister(CharacterControllerComponent* controller)
{
    if (nullptr == controller) return;

    // swap-and-pop — AnimatorSystem::Unregister와 같은 규약(단일 조밀 벡터라
    // 순서 보존은 불필요하고, erase는 O(n) 시프트라 씬 전환마다 비용이 쌓인다).
    for (size_t i = 0; i < m_controllers.size(); ++i)
    {
        if (m_controllers[i] != controller) continue;
        m_controllers[i] = m_controllers.back();
        m_controllers.pop_back();
        return;
    }
}

void CharacterControllerSystem::FixedUpdate(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다.
    for (CharacterControllerComponent* controller : m_controllers)
    {
        if (nullptr == controller) continue;

        Entity* owner = controller->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!controller->IsEnabled()) continue;
        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        // 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다(같은 문자열을 써야
        // 대조가 성립하므로 Lifecycle::Trace::TypeNameOf 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::FixedUpdate, Lifecycle::Trace::TypeNameOf(controller),
            owner->m_name.ToString().c_str(), controller->GetInstanceID());

        controller->OnFixedUpdate(tick);
    }
}

void CharacterControllerSystem::LateUpdate(float tick)
{
    for (CharacterControllerComponent* controller : m_controllers)
    {
        if (nullptr == controller) continue;

        Entity* owner = controller->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!controller->IsEnabled()) continue;
        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        // 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다(같은 문자열을 써야
        // 대조가 성립하므로 Lifecycle::Trace::TypeNameOf 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::LateUpdate, Lifecycle::Trace::TypeNameOf(controller),
            owner->m_name.ToString().c_str(), controller->GetInstanceID());

        controller->OnLateUpdate(tick);
    }
}
