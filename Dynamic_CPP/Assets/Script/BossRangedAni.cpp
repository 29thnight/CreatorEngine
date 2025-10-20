#include "BossRangedAni.h"
#include "pch.h"
#include "AnimationController.h"
#include "Animator.h"
#include "TBoss1.h"
void BossRangedAni::Enter()
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

void BossRangedAni::Update(float deltaTime)
{
}

void BossRangedAni::Exit()
{
    if (!animator) return;

    // �ִϸ������� ������(Boss GameObject)���� TBoss1 ��ũ��Ʈ�� �����ɴϴ�.
    TBoss1* bossScript = ownerObj->GetComponent<TBoss1>();

    // ��ũ��Ʈ�� ��ȿ�ϸ�, ���� �׼��� �������� �˸��� �ݹ� �Լ��� ȣ���մϴ�.
    if (bossScript != nullptr)
    {
        bossScript->OnAttackActionFinished();
    }
}
