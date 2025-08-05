#include "AtteckAction.h"
#include "pch.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();
	if (movement)
	{
		Mathf::Vector2 zeroVector(0.0f, 0.0f);
		//std::cout << "CharacterControllerComponent not found. Stopping movement." << std::endl;
		movement->Move(zeroVector); // Stop movement during attack
	}
	
	bool hasState = blackBoard.HasKey("State");
	bool hasAtkDelay = blackBoard.HasKey("AtkDelay");

	float attTime = 1.0f; // Example attack time, can be adjusted or retrieved from blackboard
	float atkDelay = 3.0f; // Example attack delay, attTime+delay ;
	float atkDelayTime;
	if (hasAtkDelay)
	{
		atkDelayTime = blackBoard.GetValueAsFloat("AtkDelay");
	}
	else
	{
		atkDelayTime = atkDelay; // Default attack delay time if not set in blackboard
	}
	 
	static float elapsedTime = 0.0f;
	if (hasState)
	{
		std::string state = blackBoard.GetValueAsString("State");
		if (state == "Atteck")
		{
			atkDelayTime = -deltatime; // Decrease attack delay time
			blackBoard.SetValueAsFloat("AtkDelay", atkDelayTime); // Set attack delay
			if (elapsedTime > attTime)
			{
				elapsedTime = 0.0f; // Reset elapsed time after attack
				//animation->SetParameter("AtteckOn", false); // Reset attack parameter
				//std::cout << "AtteckAction executed!" << m_owner->GetHashedName().ToString() << std::endl;
				std::cout << "Attack completed." << std::endl;
				return NodeStatus::Success; // Attack action completed successfully
			}
			else
			{
				elapsedTime += deltatime; // Increment elapsed time
				//std::cout << "Attacking... Elapsed time: " << elapsedTime << std::endl;
				return NodeStatus::Running; // Continue running while attacking
			}
			//std::cout << "Atteck action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in attack state
		}
		else
		{
			//movement->Move(Mathf::Vector2(0.0f, 0.0f)); // Stop movement during attack
			blackBoard.SetValueAsString("State", "Atteck");
			std::cout << "Switching to Atteck state." << std::endl;
			blackBoard.SetValueAsFloat("AtkDelay", atkDelay); // Set attack delay
			elapsedTime = 0.0f; // Reset elapsed time when switching to attack state
			// Here you can add any additional logic needed when switching to the attack state
			// For example, you might want to play an attack animation or sound
			return NodeStatus::Running; // Start the attack action
		}
	}

	// If the "AnimeState" key does not exist, we can set it to "Atteck" and start the attack action
	return NodeStatus::Success;
}
