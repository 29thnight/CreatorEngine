#include "DaedAction.h"
#include "pch.h"
#include "EffectComponent.h"
#include "DebugLog.h"

NodeStatus DaedAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	// Example action: Print a message to the console
	bool hasState = blackBoard.HasKey("State");
	
	//calculate final rotation
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Quaternion rotation = selfTransform->GetWorldQuaternion();

	auto effcomponent = m_owner->GetComponent<EffectComponent>();


	if (hasState)
	{
		std::string state = blackBoard.GetValueAsString("State");
		if (state == "PreDelete") 
		{
			//LOG("waiting delete");
			return NodeStatus::Success; // BT�� '����'�� ��ȯ�Ͽ� �� �׼��� ����
		}
		else if (state == "Daed")
		{
			float dot = rotation.Dot(finalRotation);
			LOG(dot);
			// ȸ�� �ٻ�ġ Ȯ��
			if (std::abs(dot) > 0.9999f)
			{
				// 1. ȸ���� �������� ���� ��ġ�� ��Ȯ�� �����ݴϴ�.
				selfTransform->SetRotation(finalRotation);
				LOG("DieAction: OnUpdate - Target reached. Finishing.");
				effcomponent->StopEffect(); // Stop the effect component if needed
				blackBoard.SetValueAsString("State", "PreDelete");
				return NodeStatus::Success; // BT�� '����'�� ��ȯ�Ͽ� �� �׼��� ����
			}
			float rotationSpeed = 1.0f; // Adjust this value to control the rotation speed
			// 2. ȸ���� ������ �ʾ����� ȸ�� �ӵ��� ���� ȸ���մϴ�.
			Mathf::Quaternion newRotation = Mathf::Quaternion::Slerp(rotation, finalRotation, rotationSpeed * deltatime);
			selfTransform->SetRotation(newRotation);
			LOG("Daed action already in progress.");
			return NodeStatus::Running; // Continue running if already in daed state
		}
		else
		{
			Mathf::Quaternion targetRotation = Mathf::Quaternion().CreateFromYawPitchRoll(0.0f, 90.0f, 0.0f); // Example target rotation
			finalRotation = XMQuaternionMultiply(targetRotation, rotation); // Combine the current rotation with the target rotation
			Mathf::Quaternion newRotation = Mathf::Quaternion::Slerp(rotation, targetRotation, deltatime); // Smoothly interpolate towards the target rotation

			effcomponent->Apply();


			blackBoard.SetValueAsString("State", "Daed");
			LOG("Switching to Daed state.");

			return NodeStatus::Running; // Start the daed action
		}

	}
	

	LOG("DaedAction executed!" << m_owner->GetHashedName().ToString());
	//�ִϸ��̼� �ൿ  mowner eney-scropt -> isdead =true  // scrpit���� ���̱�
	
	// You can access and modify the blackboard data here if needed
	// For example, you might want to set a value in the blackboard
	// blackBoard.SetValue("SomeKey", "SomeValue");
	// Return success or failure based on your action logic

	return NodeStatus::Success;
}
