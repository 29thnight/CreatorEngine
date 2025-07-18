#include "ChaseAction.h"
#include "pch.h"

NodeStatus ChaseAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	Transform asisTransform = blackBoard.GetValueAsTransform("Asis");
	Mathf::Vector3 asispos = asisTransform.GetWorldPosition();

	Mathf::Vector3 dir = asispos - pos;

	dir.Normalize();
	
	float Speed = blackBoard.GetValueAsFloat("eNorSpeed");

	selfTransform->AddPosition(dir * Speed * deltatime);

	std::cout << "ChaseAction executed. Moving towards target." << std::endl;

	return NodeStatus::Success;
}
