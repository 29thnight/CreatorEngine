#include "MosterMeleeAni.h"
#include "pch.h"
#include "BehaviorTreeComponent.h"
#include "AnimationController.h"
#include "Animator.h"

void MosterMeleeAni::Enter()
{
}

void MosterMeleeAni::Update(float deltaTime)
{
   // std::cout << "attacking" << std::endl;
}

void MosterMeleeAni::Exit()
{
   // std::cout << "attacking Exit" << std::endl;
    AnimationController* controller = m_ownerController;
    if (controller)
    {
        Animator* animator = controller->GetOwner();

        // 2. Animator가 속한 GameObject를 찾습니다.
        GameObject* ownerObject = animator->GetOwner();
        if (!ownerObject) return;

        //오브젝트의 최상위 부모를 찾음
		while (ownerObject->m_parentIndex != 0) //최상위 부모가 씬루트이므로
        {
            ownerObject = GameObject::FindIndex(ownerObject->m_parentIndex);
            if (!ownerObject) return;
        }

        // 3. GameObject에서 BehaviorTreeComponent를 찾습니다.
        BehaviorTreeComponent* btComponent = ownerObject->GetComponent<BehaviorTreeComponent>();
        if (!btComponent) return;

        // 4. BehaviorTreeComponent에서 Blackboard를 가져옵니다.
        BlackBoard* blackboard = btComponent->GetBlackBoard();
        if (!blackboard) return;

        // 5. Blackboard에 "IsAttacking" 상태를 false로 설정하여, BT가 다음 행동을 결정할 수 있도록 합니다.
		//std::cout << "MosterMeleeAni Exit: Setting IsAttacking to false in Blackboard." << std::endl;
        blackboard->SetValueAsBool("IsAttacking", false);
    }
}
