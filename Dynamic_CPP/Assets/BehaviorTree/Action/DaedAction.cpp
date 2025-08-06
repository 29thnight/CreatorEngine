#include "DaedAction.h"
#include "pch.h"

NodeStatus DaedAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	// Example action: Print a message to the console
	
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Quaternion rotation = selfTransform->GetWorldQuaternion();
	Mathf::Quaternion targetRotation = Mathf::Quaternion().CreateFromYawPitchRoll(0.0f, 90.0f, 0.0f); // Example target rotation

	Mathf::Quaternion newRotation = Mathf::Quaternion::Slerp(rotation, targetRotation, deltatime * 0.1f); // Smoothly interpolate towards the target rotation

	if (rotation != newRotation)
	{
		selfTransform->SetRotation(newRotation);
		std::cout << "DaedAction: Rotating to new orientation." << std::endl;
	}
	else
	{
		std::cout << "DaedAction: Already at target orientation." << std::endl;
	}

	bool isAnime = blackBoard.HasKey("AnimeState");
	if (isAnime)
	{
		std::string state = blackBoard.GetValueAsString("AnimeState");
		if (state == "Daed")
		{
			
			std::cout << "Daed action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in daed state
		}
		else
		{
			
			blackBoard.SetValueAsString("AnimeState", "Daed");
			std::cout << "Switching to Daed state." << std::endl;
		}
	}

	

	std::cout << "DaedAction executed!" << m_owner->GetHashedName().ToString() << std::endl;
	//애니메이션 행동  mowner eney-scropt -> isdead =true  // scrpit에서 죽이기
	
	// You can access and modify the blackboard data here if needed
	// For example, you might want to set a value in the blackboard
	// blackBoard.SetValue("SomeKey", "SomeValue");
	// Return success or failure based on your action logic

	return NodeStatus::Success;
}
