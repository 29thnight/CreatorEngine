#include "BossBurrowAni.h"
#include "pch.h"
#include "AnimationController.h"
#include "Animator.h"
#include "TBoss1.h"
void BossBurrowAni::Enter()
{
    AnimationController* controller = m_ownerController;
    if (controller)
    {
        Animator* animator = controller->GetOwner();

        // 2. Animator�� ���� GameObject�� ã���ϴ�.
        GameObject* ownerObject = animator->GetOwner();
        if (!ownerObject) return;

        //������Ʈ�� �ֻ��� �θ� ã��
        while (ownerObject->m_parentIndex != 0) //�ֻ��� �θ� ����Ʈ�̹Ƿ�
        {
            ownerObject = GameObject::FindIndex(ownerObject->m_parentIndex);
            if (!ownerObject) return;
        }

		m_boss = ownerObject;
        
        TBoss1* boss = m_boss->GetComponent<TBoss1>();
		if (!boss) return;
		boss->m_moveState = TBoss1::EBossMoveState::Burrowing;
       
    }
}

void BossBurrowAni::Update(float deltaTime)
{
}

void BossBurrowAni::Exit()
{
    TBoss1* boss = m_boss->GetComponent<TBoss1>();
    if (boss) {
        boss->SetBurrow();
    }
}
