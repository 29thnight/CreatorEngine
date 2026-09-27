#include "AnimatorSystem.h"
#include "LifecycleTrace.h"
#include "Animator.h"
#include "AnimationController.h"
#include "ConditionParameter.h"
#include "Entity.h"
#include <algorithm>
#include <stdexcept>

void AnimatorSystem::RequireOwnerThread() const
{
    if (std::this_thread::get_id() != m_ownerThreadId)
        throw std::logic_error("AnimatorSystem registration and Update require the owner thread");
}

AnimInstanceHandle AnimatorSystem::CreateInstance()
{
    std::lock_guard lock(m_instanceMutex);
    if (m_freeInstanceRecords.empty())
    {
        auto page = std::make_unique<AnimInstance[]>(kInstancePageSize);
        auto* records = page.get();
        m_freeInstanceRecords.reserve((m_instancePages.size() + 1) * kInstancePageSize);
        m_instancePages.push_back(std::move(page));
        for (std::size_t index = kInstancePageSize; index > 0; --index)
            m_freeInstanceRecords.push_back(records + index - 1);
    }

    std::uint32_t slotIndex;
    const bool reusedSlot = !m_freeInstanceSlots.empty();
    if (!reusedSlot)
    {
        slotIndex = static_cast<std::uint32_t>(m_instanceSlots.size());
        m_instanceSlots.emplace_back();
    }
    else
    {
        slotIndex = m_freeInstanceSlots.back();
    }
    AnimInstance* instance = m_freeInstanceRecords.back();
    try
    {
        m_instances.push_back(instance);
    }
    catch (...)
    {
        if (!reusedSlot) m_instanceSlots.pop_back();
        throw;
    }
    m_freeInstanceRecords.pop_back();
    if (reusedSlot) m_freeInstanceSlots.pop_back();
    auto& slot = m_instanceSlots[slotIndex];
    slot.denseIndex = static_cast<std::uint32_t>(m_instances.size() - 1);
    slot.occupied = true;
    instance->slot = slotIndex;
    return { slotIndex, slot.generation };
}

void AnimatorSystem::DestroyInstance(AnimInstanceHandle handle)
{
    std::lock_guard lock(m_instanceMutex);
    if (handle.slot >= m_instanceSlots.size()) return;
    auto& slot = m_instanceSlots[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return;
    const std::size_t index = slot.denseIndex;
    AnimInstance* retired = m_instances[index];
    if (index != m_instances.size() - 1)
    {
        m_instances[index] = m_instances.back();
        m_instanceSlots[m_instances[index]->slot].denseIndex = static_cast<std::uint32_t>(index);
    }
    m_instances.pop_back();
    // Recycle the page record itself only after releasing every owned buffer.
    // The next handle sees defaults while survivors keep their original address.
    *retired = AnimInstance{};
    m_freeInstanceRecords.push_back(retired);
    slot.occupied = false;
    if (++slot.generation == 0) ++slot.generation;
    m_freeInstanceSlots.push_back(handle.slot);
}

AnimInstance* AnimatorSystem::ResolveInstance(AnimInstanceHandle handle) noexcept
{
    std::lock_guard lock(m_instanceMutex);
    if (handle.slot >= m_instanceSlots.size()) return nullptr;
    const auto& slot = m_instanceSlots[handle.slot];
    if (!slot.occupied || slot.generation != handle.generation) return nullptr;
    return m_instances[slot.denseIndex];
}

void AnimatorSystem::Register(Animator* animator)
{
    if (nullptr == animator) return;
    RequireOwnerThread();

    if (std::ranges::find(m_animators, animator) != m_animators.end()) return;
    m_animators.push_back(animator);
}

void AnimatorSystem::Unregister(Animator* animator)
{
    if (nullptr == animator) return;
    RequireOwnerThread();

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
    RequireOwnerThread();
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다 — Animator가 더 이상
    // m_schedule.UpdateList()를 거치지 않으므로 그 가드도 함께 옮겨왔다.
    for (Animator* animator : m_animators)
    {
        if (nullptr == animator) continue;

        Entity* owner = animator->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!animator->IsEnabled()) continue;
        // C3 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도 함께 옮긴다.
        // 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다(같은 문자열을 써야
        // 대조가 성립하므로 Lifecycle::Trace::TypeNameOf 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(animator),
            owner->m_name.ToString().c_str(), animator->GetInstanceID());

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
