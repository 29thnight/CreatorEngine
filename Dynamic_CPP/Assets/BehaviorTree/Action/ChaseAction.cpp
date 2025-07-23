#include "ChaseAction.h"
#include "pch.h"

NodeStatus ChaseAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	Transform asisTransform = blackBoard.GetValueAsTransform("Asis");
	Mathf::Vector3 asispos = asisTransform.GetWorldPosition();

	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();

	Mathf::Vector3 dir = asispos - pos;
	Mathf::Vector2 dir2D = { dir.x, dir.z }; // Assuming y is up, we only care about x and z for 2D direction

	dir2D.Normalize();
	
	float Speed = blackBoard.GetValueAsFloat("eNorSpeed");

	bool isAnime = blackBoard.HasKey("AnimeState");
	

	//selfTransform->AddPosition(dir * Speed * deltatime);
	if (movement) {
		movement->Move(dir2D * Speed * deltatime);
		//std::cout << "ChaseAction executed. Moving towards target." << std::endl;
		if (isAnime)
		{
			std::string state = blackBoard.GetValueAsString("AnimeState");
			if (state == "Move")
			{
				//std::cout << "Chase action already in progress." << std::endl;
				//return NodeStatus::Running; // Continue running if already in chase state
			}
			else
			{
				blackBoard.SetValueAsString("AnimeState", "Move");
				//std::cout << "Switching to Move state." << std::endl;
			}
		}
	}


	return NodeStatus::Success;
}
