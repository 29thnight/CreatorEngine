#include "IsDetect.h"
#include "pch.h"

bool IsDetect::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{

	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	Transform asisTransform = blackBoard.GetValueAsTransform("Asis");
	Mathf::Vector3 asispos = asisTransform.GetWorldPosition();

	Mathf::Vector3 dir = asispos - pos;

	float chaseRange = blackBoard.GetValueAsFloat("eNorChaseRange");
	float outDuration = blackBoard.GetValueAsFloat("eNorChaseOutDuration");

	static float outTime = 0.0f;

	bool isAnime = blackBoard.HasKey("AnimeState");


	if (dir.Length() < chaseRange) {
		outTime = outDuration;
		std::cout << "IsDetect ConditionCheck: in range" << std::endl;
		return true;
	}

	if (isAnime)
	{
		std::string state = blackBoard.GetValueAsString("AnimeState");
		if (state == "Move")
		{
			if (outTime > 0.0f) {
				outTime -= deltatime;
				std::cout << "IsDetect ConditionCheck: out of range, but still in out duration" << "time : " << std::to_string(outTime) << std::endl;
				return true;
			}
			else {
				std::cout << "IsDetect ConditionCheck: out of range, out duration ended" << std::endl;
				return false; // Out of range and out duration ended
			}
		}
	}



	outTime = 0.0f;
	std::cout << "IsDetect ConditionCheck: out of range" << std::endl;

	return false;
}
