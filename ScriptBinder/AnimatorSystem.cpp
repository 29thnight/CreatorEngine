#include "AnimatorSystem.h"
#include "Animator.h"
#include "AnimationController.h"
#include "ConditionParameter.h"
#include "GameObject.h"
#include <algorithm>

void AnimatorSystem::Register(Animator* animator)
{
    if (nullptr == animator) return;

    if (std::ranges::find(m_animators, animator) != m_animators.end()) return;
    m_animators.push_back(animator);
}

void AnimatorSystem::Unregister(Animator* animator)
{
    if (nullptr == animator) return;

    // swap-and-pop — SystemSchedule_Internal::RemoveFromList과 같은 규약
    // (같은 축의 여러 리스트에 걸쳐 있지 않은 단일 조밀 벡터라 순서 보존은
    // 애초에 불필요하고, erase는 O(n) 시프트라 씬 전환마다 비용이 쌓인다).
    for (size_t i = 0; i < m_animators.size(); ++i)
    {
        if (m_animators[i] != animator) continue;
        m_animators[i] = m_animators.back();
        m_animators.pop_back();
        return;
    }
}

void AnimatorSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다 — Animator가 더 이상
    // m_schedule.UpdateList()를 거치지 않으므로 그 가드도 함께 옮겨왔다.
    for (Animator* animator : m_animators)
    {
        if (nullptr == animator) continue;

        GameObject* owner = animator->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!animator->IsEnabled()) continue;

        // ── 이하 옛 Animator::Update 본문 그대로(트랙 C3 이관) ──
        if (animator->m_animationControllers.empty()) continue;

        for (auto& animationController : animator->m_animationControllers)
        {
            animationController->Update(tick);
        }

        std::unique_lock lock(animator->m_paramMutex);
        for (auto& param : animator->Parameters)
        {
            if (param->vType == ValueType::Trigger)
            {
                param->ResetTrigger();
            }
        }
    }
}
