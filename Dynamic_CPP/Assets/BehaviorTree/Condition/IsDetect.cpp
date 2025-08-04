#include "IsDetect.h"
#include "pch.h"

bool IsDetect::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	std::cout << "IsDetect ConditionCheck: Checking if entity is in chase range." << std::endl;

	bool isP1 = blackBoard.HasKey("Player1");
	bool isP2 = blackBoard.HasKey("Player2");
	bool isAsis = blackBoard.HasKey("Asis");

	if (!isP1 && !isP2) {
		std::cout << "IsAtteck ConditionCheck: No player found." << std::endl;
	}

	//self
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	//player1
	GameObject* Player1 = nullptr;
	Transform* player1Transform = nullptr;
	Mathf::Vector3 p1dir;

	if (isP1) {
		Player1 = blackBoard.GetValueAsGameObject("Player1");
		if (Player1 != nullptr) {
			player1Transform = Player1->GetComponent<Transform>();
			Mathf::Vector3 p1pos = player1Transform->GetWorldPosition();
			p1dir = p1pos - pos;
		}
		else {
			p1dir = Mathf::Vector3(0.0f, 0.0f, 0.0f); // Handle case where Player1 is not found
		}
	}

	//player2
	GameObject* Player2 = nullptr;
	Transform* player2Transform = nullptr;
	Mathf::Vector3 p2dir;

	if (isP2) {
		Player2 = blackBoard.GetValueAsGameObject("Player2");
		if (Player2 != nullptr) {
			player2Transform = Player2->GetComponent<Transform>();
			Mathf::Vector3 p2pos = player2Transform->GetWorldPosition();
			p2dir = p2pos - pos;
		}
		else {
			p2dir = Mathf::Vector3(0.0f, 0.0f, 0.0f); // Handle case where Player2 is not found
		}
	}

	//asis
	Transform asisTransform = blackBoard.GetValueAsTransform("Asis");
	Mathf::Vector3 asispos = asisTransform.GetWorldPosition();

	

	float chaseRange = blackBoard.GetValueAsFloat("eChaseRange");
	float outDuration = blackBoard.GetValueAsFloat("eChaseOutDuration");

	static float outTime = 0.0f;

	bool isAnime = blackBoard.HasKey("AnimeState");


	if (dir.Length() < chaseRange) {
		outTime = outDuration;
		//std::cout << "IsDetect ConditionCheck: in range" << std::endl;
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
	//std::cout << "IsDetect ConditionCheck: out of range" << std::endl;

	return false;
}
