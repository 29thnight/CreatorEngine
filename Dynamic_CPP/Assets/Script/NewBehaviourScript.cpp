#include "NewBehaviourScript.h"
#include "BehaviorTreeComponent.h"
#include "Transform.h"
#include "pch.h"

//nodecreate -> tree->CreateNode(key, data);
//key => "Action", "Selector", "Sequence", "Condition", "Decorator", etc.
//funtion ->
// Action => SetAction(std::function<NodeStatus(float, BlackBoard&)func >
// Condition => SetCondition(std::function<bool(BlackBoard&)func >



void NewBehaviourScript::Start()
{
	auto behaviorTree = m_pOwner->GetComponent<BehaviorTreeComponent>();
	behaviorTree->Initialize();

	auto rootNode = behaviorTree->GetRoot();
	{
		auto selectorNode = behaviorTree->CreateNode("Selector");
		{
			auto AttackSequenceNode = behaviorTree->CreateNode("Sequence");
			{// Attack Sequence
				auto AttackConditionNode = behaviorTree->CreateNode("Condition");
				{
					//condition
					static_cast<BT::ConditionNode*>(AttackConditionNode.get())->SetCondition(
						[this](const BlackBoard& bb) { return AttackCondition(bb); }
					);
				}
				static_cast<BT::SequenceNode*>(AttackSequenceNode.get())->AddChild(AttackConditionNode);

				auto AttackActionNode = behaviorTree->CreateNode("Action");
				{// action
					static_cast<BT::ActionNode*>(AttackActionNode.get())->SetAction(
						[this](float dt, BlackBoard& bb) { return AttackAction(dt, bb); }
					);
				}
				static_cast<BT::SequenceNode*>(AttackSequenceNode.get())->AddChild(AttackActionNode);
			}
			static_cast<BT::SelectorNode*>(selectorNode.get())->AddChild(AttackSequenceNode);


			auto ChaseSequenceNode = behaviorTree->CreateNode("Sequence");
			{// Chase Sequence
				auto ChaseConditionNode = behaviorTree->CreateNode("Condition");
				{// condition
					static_cast<BT::ConditionNode*>(ChaseConditionNode.get())->SetCondition(
						[this](const BlackBoard& bb) { return ChaseCondition(bb); }
					);
				}
				static_cast<BT::SequenceNode*>(ChaseSequenceNode.get())->AddChild(ChaseConditionNode);

				auto ChaseActionNode = behaviorTree->CreateNode("Action");
				{// action
					static_cast<BT::ActionNode*>(ChaseActionNode.get())->SetAction(
						[this](float dt, BlackBoard& bb) { return ChaseAction(dt, bb); }
					);
				}
				static_cast<BT::SequenceNode*>(ChaseSequenceNode.get())->AddChild(ChaseActionNode);
			}
			static_cast<BT::SelectorNode*>(selectorNode.get())->AddChild(ChaseSequenceNode);


			auto DetectSequenceNode = behaviorTree->CreateNode("Sequence");
			{
				// Detect Sequence
				auto DetectConditionNode = behaviorTree->CreateNode("Condition");
				{
					// condition
					static_cast<BT::ConditionNode*>(DetectConditionNode.get())->SetCondition(
						[this](const BlackBoard& bb) { return DetectCondition(bb); }
					);
				}
				static_cast<BT::SequenceNode*>(DetectSequenceNode.get())->AddChild(DetectConditionNode);

				auto DetectActionNode = behaviorTree->CreateNode("Action");
				{// action
					static_cast<BT::ActionNode*>(DetectActionNode.get())->SetAction(
						[this](float dt, BlackBoard& bb) { return DetectAction(dt, bb); }
					);
				}
				static_cast<BT::SequenceNode*>(DetectSequenceNode.get())->AddChild(DetectActionNode);
			}

			static_cast<BT::SelectorNode*>(selectorNode.get())->AddChild(DetectSequenceNode);
		}
		static_cast<BT::SequenceNode*>(rootNode.get())->AddChild(selectorNode);
	}

	player1 = GameObject::Find("Player1");
	if (!player1)
	{
		std::cout << "Player1 not found!" << std::endl;
		return;
	}
	player2 = GameObject::Find("Player2");
	if (!player2)
	{
		std::cout << "Player2 not found!" << std::endl;
		return;
	}
	asis = GameObject::Find("Asis");
	if (!asis)
	{
		std::cout << "Asis not found!" << std::endl;
		return;
	}
	
}

void NewBehaviourScript::Update(float tick)
{
	//m_pOwner;
	
	//m_pTransform->AddPosition({ 0.0f, 0.0f, 1.0f * tick });
	BlackBoard& bb = m_pOwner->GetComponent<BehaviorTreeComponent>()->GetBlackBoard();
	Mathf::Vector3 pos = m_pTransform->GetWorldPosition();
	auto p1pos = Mathf::Vector3::Zero;
	auto p2pos = Mathf::Vector3::Zero;
	auto asisPos = Mathf::Vector3::Zero;
	if (!player1) {
		p1pos = player1->GetComponent<Transform>()->GetWorldPosition();
	}
	if (!player2)
	{
		p2pos = player2->GetComponent<Transform>()->GetWorldPosition();
	}
	if (!asis)
	{
		asisPos = asis->GetComponent<Transform>()->GetWorldPosition();
	}
	


	float player1Distance = (p1pos != Mathf::Vector3::Zero) ? Mathf::Vector3::Distance(pos, p1pos) : LONG_MAX;
	float player2Distance = (p2pos != Mathf::Vector3::Zero) ? Mathf::Vector3::Distance(pos, p2pos) : LONG_MAX;
	float AsisDistance = (asisPos != Mathf::Vector3::Zero) ? Mathf::Vector3::Distance(pos, asisPos) : LONG_MAX;
	
	bb.Set<float>("deltaTime", tick);
	bb.Set<float>("Player1Distance", player1Distance);
	bb.Set<float>("Player2Distance", player2Distance);
	bb.Set<float>("AsisDistance", AsisDistance);

}

bool NewBehaviourScript::DetectCondition(const BlackBoard& bb)
{
	
	//auto transform = m_pOwner->GetComponent<Transform>();
	float p1Distance =  bb.Get<float>("Player1Distance");
	float p2Distance = bb.Get<float>("Player2Distance");
	float AsisDistance = bb.Get<float>("AsisDistance");

	if (p1Distance < m_detectRange || p2Distance < m_detectRange || AsisDistance < m_detectRange)
	{
		if (!(p1Distance < m_detectRange || p2Distance < m_detectRange))
		{
			target = asis;
		}
		else
		{
			if (p1Distance < p2Distance)
			{
				target = player1;
			}
			else
			{
				target = player2;
			}

		}
		return true;
	}

	return false;
}

bool NewBehaviourScript::ChaseCondition(const BlackBoard& bb)
{
	if (target) 
	{
		if (!m_isChaseAsis&&(target==asis))
		{
			return false;
		}

		Mathf::Vector3 pos = m_pTransform->GetWorldPosition();
		Mathf::Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();

		float distance = Mathf::Vector3::Distance(pos, targetPos);
		
		if (distance < m_chaseRange)
		{
			// tracking start
			m_isChasing = true;
			return true;
		}
		else if (m_isChasing)
		{
			m_countDown += bb.Get<float>("DeltaTime");
			if (m_countDown >= m_chaseDelay)
			{
				// tracking end
				m_isChasing = false;
				m_countDown = 0.0f; // reset countdown
				return false; // stop chasing
			}
			else
			{
				return true; // still in chase mode, but not close enough
			}
			
		}
		
	}

	return false;
}

bool NewBehaviourScript::AttackCondition(const BlackBoard& bb)
{
	if (target) {
		Mathf::Vector3 pos = m_pTransform->GetWorldPosition();
		Mathf::Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();

		float distance = Mathf::Vector3::Distance(pos, targetPos);
		if (distance < m_attackRange)
		{
			std::cout << "AttackCondition: Target in range: " << target->GetHashedName().ToString() << std::endl;
			return true; // Attack condition met
		}
		else
		{
			std::cout << "AttackCondition: Target out of range: " << target->GetHashedName().ToString() << std::endl;
		}
	}

	return false;
}

BT::NodeStatus NewBehaviourScript::DetectAction(float dt, BlackBoard& bb)
{
	if (target)
	{
		auto targetTr = target->GetComponent<Transform>();
		LookAtTarget(m_pTransform, targetTr);
		std::cout << "DetectAction: Looking at target: " << target->GetHashedName().ToString() << std::endl;
		return BT::NodeStatus::Success;
	}
	return BT::NodeStatus::Failure;
}

BT::NodeStatus NewBehaviourScript::ChaseAction(float dt, BlackBoard& bb)
{
	if (!target) return BT::NodeStatus::Failure;

	
	auto targetTr = target->GetComponent<Transform>();
	LookAtTarget(m_pTransform, targetTr);
	Mathf::Vector3 pos = m_pTransform->GetWorldPosition();
	Mathf::Vector3 targetPos = targetTr->GetWorldPosition();
	Mathf::Vector3 direction = (targetPos - pos);
	direction.Normalize();
	m_pTransform->AddPosition(direction * m_speed * dt);
	static float time = 0;
	time += dt;
	if (time >= 1.0f) { // log every 1 second
		time = 0;
		std::cout << "ChaseAction: Moving towards target: " << target->GetHashedName().ToString() << std::endl;
	}
	return BT::NodeStatus::Success;
}

BT::NodeStatus NewBehaviourScript::AttackAction(float dt, BlackBoard& bb)
{
	static float time = 0;
	time += dt;
	if (time >= 1.0f) { // Attack every 1 second
		time = 0;
		if (target) {
			std::cout << "AttackAction: Attacking target: " << target->GetHashedName().ToString() << std::endl;
			// Implement attack logic here, e.g., reduce health of the target
			// target->GetComponent<HealthComponent>()->TakeDamage(10);
			return BT::NodeStatus::Success;
		}
	}

}


void NewBehaviourScript::LookAtTarget(Transform* selfTransform, Transform* targetTransform) {
	XMMATRIX selfWorld = selfTransform->GetWorldMatrix();
	XMVECTOR selfPos = selfWorld.r[3];

	XMVECTOR targetPos = targetTransform->GetWorldPosition();
	XMVECTOR forward = XMVector3Normalize(targetPos - selfPos);

	// 예외 처리: forward와 up이 너무 유사하면 회전 꼬일 수 있음
	XMVECTOR up = XMVectorSet(0, 1, 0, 0);
	if (fabs(XMVectorGetX(XMVector3Dot(forward, up))) > 0.999f) {
		up = XMVectorSet(0, 0, 1, 0); // fallback up
	}

	XMMATRIX lookMat = XMMatrixLookToRH(XMVectorZero(), forward, up);
	XMVECTOR rotQuat = XMQuaternionRotationMatrix(lookMat);

	selfTransform->SetRotation(rotQuat);
}