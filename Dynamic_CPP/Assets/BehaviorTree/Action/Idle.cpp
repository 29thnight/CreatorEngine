#include "Idle.h"
#include "pch.h"

NodeStatus Idle::Tick(float deltatime, BlackBoard& blackBoard)
{
	bool isP1 = blackBoard.HasKey("Player1");
	bool isP2 = blackBoard.HasKey("Player2");
	bool isAsis = blackBoard.HasKey("Asis");

	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();
	bool isState = blackBoard.HasKey("State");
	// Perform idle behavior, such as waiting or doing nothing
	// This is a placeholder for actual idle logic
	if (movement)
	{
		movement->Move(Mathf::Vector2(0.0f, 0.0f)); // Stop movement during idle
		std::cout << "Idle action executed. Stopping movement." << std::endl;
	}
	// Return success to indicate that the idle action completed successfully
	// In a real implementation, you might check conditions or perform actions here

	//idle state Retargeting 
	//init Target;
	GameObject* Target = nullptr;
	std::cout << "Idle action: Starting retargeting process." << std::endl;
	blackBoard.SetValueAsGameObject("ClosedTarget","");

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



	if (isP1&&isP2)
	{
		if (p1dir.Length() < p2dir.Length()) {
			Target = Player1;
			std::cout << "Idle action: Target set to Player1." << std::endl;
		}
		else {
			Target = Player2;
			std::cout << "Idle action: Target set to Player2." << std::endl;
		}
	}

	//asis
	bool useAsis = blackBoard.HasKey("eAsis");
	bool asisTarget = false;
	if (useAsis)
	{
		asisTarget = blackBoard.GetValueAsBool("eAsis");
	}
	GameObject* asis = nullptr;
	Transform* asisTransform = nullptr;
	Mathf::Vector3 asisdir;
	if (isAsis) {
		asis = blackBoard.GetValueAsGameObject("Asis");
		if (asis != nullptr) {
			asisTransform = asis->GetComponent<Transform>();
			Mathf::Vector3 asispos = asisTransform->GetWorldPosition();
			asisdir = asispos - pos;
		}
		else {
			asisdir = Mathf::Vector3(0.0f, 0.0f, 0.0f); // Handle case where Asis is not found
		}
	}
	
	if (asisTarget) {
		if (Target == Player1) 
		{
			if (asisdir.Length() < p1dir.Length()) {
				std::cout << "Idle action: Target set to Asis based on Player1 distance." << std::endl;
				Target = asis;
			}
		}
		else if(Target == Player2)
		{
			if (asisdir.Length() < p2dir.Length()) {
				std::cout << "Idle action: Target set to Asis based on Player2 distance." << std::endl;
				Target = asis;
			}
		}
		else 
		{
			std::cout << "Idle action: Target set to Asis as no player targets are closer." << std::endl;
			Target = asis;
		}
	}
		
	if (Target)
	{
		std::cout << "Idle action: Target is set to " << Target->ToString() << std::endl;
		blackBoard.SetValueAsGameObject("ClosedTarget", Target->ToString());
	}
	else
	{
		std::cout << "Idle action: No valid target found. Setting ClosedTarget to empty." << std::endl;
		blackBoard.SetValueAsGameObject("ClosedTarget", "");
	}

	//end idle state Retargeting 
	
	auto animation = m_owner->GetComponent<Animator>();
	
	if (isState)
	{
		std::string state = blackBoard.GetValueAsString("State");
		if (state == "Idle")
		{
			std::cout << "Idle action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in idle state
			//dead test code
			//blackBoard.SetValueAsInt("CurrHP", 0); // Set current HP to 0 for testing dead state
			//
		}
		else
		{
			if (animation)
			{
				animation->SetParameter("IdleOn", true);
			}
			blackBoard.SetValueAsString("State", "Idle");
			std::cout << "Switching to Idle state." << std::endl;
		}
	}
	else
	{
		blackBoard.SetValueAsString("State", "Idle");
		std::cout << "Setting Idle state for the first time." << std::endl;
	}

	//std::cout << "Idle action executed." << std::endl;

	return NodeStatus::Success;
}
