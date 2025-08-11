#include "AtteckAction.h"
#include "pch.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	GameObject* target = blackBoard.GetValueAsGameObject("Target");
	Transform* targetTransform = target->GetComponent<Transform>();
	Mathf::Vector3 targetPos = targetTransform->GetWorldPosition();
	Mathf::Vector3 dir = targetPos - pos;
	dir.Normalize(); // Normalize direction vector

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

				blackBoard.SetValueAsInt("AttackCount", 1); // Set Enemy attack count to 1
				std::cout << "Attack completed." << std::endl;
				blackBoard.SetValueAsString("State", "AtteckDelay");
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
			blackBoard.SetValueAsVector3("AttackDirection", dir); // Set attack delay
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

void AtteckAction::PerformSweepAttackTest(BlackBoard& blackBoard)
{
	std::cout << "=========================================" << std::endl;
	std::cout << "### " << m_owner->GetHashedName().ToString() << ": ĸ�� ���� ���� �׽�Ʈ ���� ###" << std::endl;

	Transform* selfTransform = m_owner->GetComponent<Transform>();

	// --- 1. ĳ���Ϳ� ���� ��ġ ���� ---
	// ���� ���ӿ����� �ִϸ��̼� ������ Transform�� ����ؾ� �մϴ�.
	// ���⼭�� ĳ������ ��ġ�� ������ ������� ������ ���� ������ ����ϴ�.
	DirectX::SimpleMath::Vector3 attackStartPosition = selfTransform->GetWorldPosition() + selfTransform->GetUp() * 1.5f + selfTransform->GetForward() * 1.0f;
	DirectX::SimpleMath::Vector3 attackEndPosition = selfTransform->GetWorldPosition() + selfTransform->GetUp() * 0.8f + selfTransform->GetForward() * 1.5f;

	// --- 2. ���� �Է�(SweepInput) ������ �غ� ---
	SweepInput sweepInput;
	sweepInput.startPosition = attackStartPosition;
	sweepInput.startRotation = selfTransform->GetWorldQuaternion();
	sweepInput.direction = attackEndPosition - attackStartPosition;
	sweepInput.distance = sweepInput.direction.Length();
	sweepInput.direction.Normalize();

	// �����忡�� Ÿ���� ���̾ �������ų�, �� ���̾ �ϵ��ڵ��մϴ�.
	// ���⼭�� 2�� ���̾ 'Enemy'��� �����մϴ�.
	const int ENEMY_LAYER = 2;
	sweepInput.layerMask = (1 << ENEMY_LAYER);

	// --- 3. Į�� ���� ����(Geometry) ���� ---
	const float swordRadius = 0.15f;
	const float swordHalfHeight = 0.7f;

	// --- 4. PhysicX ����� ���� �Լ� ȣ�� ---
	std::cout << "���� ���� (��� ���̾�: " << ENEMY_LAYER << ")" << std::endl;
	SweepOutput sweepResult = PhysicsManagers->CapsuleSweep(sweepInput, swordRadius, swordHalfHeight);

	// --- 5. ��� �м� �� ��� ---
	if (!sweepResult.touches.empty())
	{
		std::cout << "[����] " << sweepResult.touches.size() << "���� �浹�� �����߽��ϴ�!" << std::endl;
		for (const auto& hit : sweepResult.touches)
		{
			std::cout << "  - �浹 ��ü ID: " << hit.hitObjectID << std::endl;
			// ���⿡ ������ ó��, �ǰ� �̺�Ʈ ���� ���� ������ �߰��� �� �ֽ��ϴ�.
		}
	}
	else
	{
		std::cout << "[����] �ƹ��͵� �������� �ʾҽ��ϴ�." << std::endl;
	}
	std::cout << "=========================================" << std::endl;
}