#include "IsAtteck.h"
#include "pch.h"

bool IsAtteck::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos =  selfTransform->GetWorldPosition();

	Transform asisTransform = blackBoard.GetValueAsTransform("Asis");
	Mathf::Vector3 asispos = asisTransform.GetWorldPosition();

	Mathf::Vector3 dir = asispos - pos;

	float atkRange = blackBoard.GetValueAsFloat("eNorAtkRange");
	
	if (dir.Length() < atkRange) {
		return true;
	}
		

	return false;
}
