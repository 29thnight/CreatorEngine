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

        // 2. Animator�� ���� GameObject�� ã���ϴ�.
        GameObject* ownerObject = animator->GetOwner();
        if (!ownerObject) return;

        //������Ʈ�� �ֻ��� �θ� ã��
		while (ownerObject->m_parentIndex != 0) //�ֻ��� �θ� ����Ʈ�̹Ƿ�
        {
            ownerObject = GameObject::FindIndex(ownerObject->m_parentIndex);
            if (!ownerObject) return;
        }

        // 3. GameObject���� BehaviorTreeComponent�� ã���ϴ�.
        BehaviorTreeComponent* btComponent = ownerObject->GetComponent<BehaviorTreeComponent>();
        if (!btComponent) return;

        // 4. BehaviorTreeComponent���� Blackboard�� �����ɴϴ�.
        BlackBoard* blackboard = btComponent->GetBlackBoard();
        if (!blackboard) return;

        // 5. Blackboard�� "IsAttacking" ���¸� false�� �����Ͽ�, BT�� ���� �ൿ�� ������ �� �ֵ��� �մϴ�.
		//std::cout << "MosterMeleeAni Exit: Setting IsAttacking to false in Blackboard." << std::endl;
        blackboard->SetValueAsBool("IsAttacking", false);
    }
}
