#include "BossMeleeAni.h"
#include "pch.h"
#include "AnimationController.h"
#include "Animator.h"
#include "TBoss1.h"
void BossMeleeAni::Enter()
{
    AnimationController* controller = m_ownerController;
    if (controller)
    {
        animator = controller->GetOwner();
    }

    if (animator)
    {
        animator->SetParameter("CanTransitionToAttack", false);
        GameObject* model = animator->GetOwner();
        if (model)
        {
            ownerObj = GameObject::FindIndex(model->m_parentIndex);
        }
    }
}

void BossMeleeAni::Update(float deltaTime)
{
}

void BossMeleeAni::Exit()
{
    if (!animator) return;

    // 애니메이터의 소유자(Boss GameObject)에서 TBoss1 스크립트를 가져옵니다.
    TBoss1* bossScript = ownerObj->GetComponent<TBoss1>();

    // 스크립트가 유효하면, 공격 액션이 끝났음을 알리는 콜백 함수를 호출합니다.
    if (bossScript != nullptr)
    {
        bossScript->OnAttackActionFinished();
    }
}
