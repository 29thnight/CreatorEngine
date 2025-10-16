#include "TBoss1.h"
#include "pch.h"
#include "CharacterControllerComponent.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include "RigidBodyComponent.h"
#include <utility>
#include "BP003.h"
#include "BP001.h"

void TBoss1::Start()
{
	BT = m_pOwner->GetComponent<BehaviorTreeComponent>();
	BB = BT->GetBlackBoard();
	m_rigid = m_pOwner->GetComponent<RigidBodyComponent>();

	//����̴� �����忡�� ����Ʈ ��Ƴ�������. ���߿� �ٲٴ��� ������
	bool hasChunsik = BB->HasKey("Chunsik");
	if (hasChunsik) {
		m_chunsik = BB->GetValueAsGameObject("Chunsik");
	}

	


	//prefab load
	Prefab* BP001Prefab = PrefabUtilitys->LoadPrefab("Boss1BP001Obj");
	Prefab* BP003Prefab = PrefabUtilitys->LoadPrefab("Boss1BP003Obj");

	////1�� ���� ����ü �ִ� 10��
	for (size_t i = 0; i < 10; i++) {
		std::string Projectilename = "BP001Projectile" +std::to_string(i);
		GameObject* Prefab1 = PrefabUtilitys->InstantiatePrefab(BP001Prefab, Projectilename);
		Prefab1->SetEnabled(false);
		BP001Objs.push_back(Prefab1);
	}
	
	////3�� ���� ������ �ִ� 16��
	for (size_t i = 0; i < 16; i++)
	{
		std::string Floorname = "BP003Floor" +std::to_string(i);
		GameObject* Prefab2 = PrefabUtilitys->InstantiatePrefab(BP003Prefab, Floorname);
		Prefab2->SetEnabled(false);
		BP003Objs.push_back(Prefab2);
	}

	m_currentHP = m_maxHP;
	//blackboard initialize
	BB->SetValueAsString("State", m_state); //���� ����
	BB->SetValueAsString("Identity", m_identity); //���� ���̵�ƼƼ

	BB->SetValueAsInt("MaxHP", m_maxHP); //�ִ� ü��
	BB->SetValueAsInt("CurrHP", m_currentHP); //���� ü��

}

void TBoss1::Update(float tick)
{

	//test code  ==> todo : �ʿ信 ���� ���� Ÿ�ٰ� Ÿ���� �����ϴ� ���� ==> ������� ī��Ʈ? �Ÿ�? ����?
	//1. ���� �������� ���� ����� �÷��̾�
	//2. ���� Ÿ�ٰ��� ���� Ƚ�� ���̰� 1 �������� Ȯ�� (eB_TargetNumLimit)

	bool hasTarget = BB->HasKey("1P");
	if (hasTarget) {
		m_target = BB->GetValueAsGameObject("1P");
	}
	hazardTimer += tick;
	//������ �������� ȸ�� ������ �ʿ�� ���ο��� ȸ�� ��Ű��
	if (m_activePattern == EPatternType::None) {
		RotateToTarget();
	}

	UpdatePattern(tick);
}

void TBoss1::RotateToTarget()
{
	SimpleMath::Quaternion rot = m_pOwner->m_transform.GetWorldQuaternion();
	if (m_target)
	{
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Transform* targetTransform = m_target->GetComponent<Transform>();
		if (targetTransform) {
			Mathf::Vector3 targetpos = targetTransform->GetWorldPosition();
			Mathf::Vector3 dir = targetpos - pos;
			dir.y = 0.f;
			dir.Normalize();
			SimpleMath::Quaternion lookRot = SimpleMath::Quaternion::CreateFromRotationMatrix(SimpleMath::Matrix::CreateLookAt(Mathf::Vector3::Zero, -dir, Mathf::Vector3::Up));
			lookRot.Inverse(lookRot);
			//rot = SimpleMath::Quaternion::Slerp(rot, lookRot, 0.2f);
			//m_pOwner->m_transform.SetRotation(rot);
			m_pOwner->m_transform.SetRotation(lookRot);
		}
	}
}

void TBoss1::ShootProjectile(int index, Mathf::Vector3 pos, Mathf::Vector3 dir)
{
	if (index < 0 || index >= BP001Objs.size()) return; // �ε��� ���� �˻�
	GameObject* obj = BP001Objs[index];
	BP001* script = obj->GetComponent<BP001>();
	obj->SetEnabled(true);
	script->Initialize(this, pos, dir, BP001Damage, BP001RadiusSize, BP001Delay, BP001Speed);
	script->isAttackStart = true;
}

void TBoss1::SweepAttack(Mathf::Vector3 pos, Mathf::Vector3 dir)
{
	//������ ���� ==> �ڽ� �������� ó�� �ұ�? 
	//���� : BP002Dist, BP002Widw --> ��� ���̴� �ͺ��� ũ�ų� ������ ����
	Mathf::Vector3 boxHalfExtents(BP002Widw * 0.5f, 2.0f, BP002Dist * 0.5f); // ���� 2.0f�� ���Ƿ� ����
	Mathf::Vector3 boxCenter = pos + dir * (BP002Dist * 0.5f); // �ڽ� �߽� ��ġ
	Mathf::Quaternion boxOrientation = Mathf::Quaternion::CreateFromRotationMatrix(Mathf::Matrix::CreateLookAt(Mathf::Vector3::Zero, dir, Mathf::Vector3::Up));
	std::vector<HitResult> hitObjects;
	SweepInput input;
	input.direction = dir;
	input.distance = BP002Dist;
	input.startPosition = pos;
	input.startRotation = boxOrientation;
	input.layerMask = ~0; // ��� ���̾� �˻�
	int hitCount = PhysicsManagers->BoxSweep(input, boxHalfExtents, hitObjects);
	isAttacked = true;
	if (hitCount > 0)
	{
		for (const auto& hit : hitObjects)
		{
			GameObject* hitObject = hit.gameObject;
			if (hitObject && hitObject->m_tag == "Player")
			{
				// �÷��̾�� ������ ����
				Entity* entity = hitObject->GetComponentDynamicCast<Entity>();
				if (entity)
				{
					HitInfo hitInfo;
					hitInfo.hitPos = hit.point;
					hitInfo.hitNormal = hit.normal;
					hitInfo.attakerPos = pos;
					//hitInfo.KnockbackForce
					//hitInfo.bulletType
					//hitInfo.itemType
					entity->SendDamage(this, BP002Damage, hitInfo);
				}
			}
		}
	}
}

void TBoss1::MoveToChunsik(float tick)
{
	if (!isMoved)
	{
		if (!isBurrow) {
			Burrow();
		}
		else {
			burrowTimer += tick;
			if (burrowTimer >= 2.f) {
				ProtrudeChunsik();
				burrowTimer = 0.f;
				isMoved = true;
			}
		}
	}
}

void TBoss1::BurrowMove(float tick)
{
	if(!isMoved)
	{
		if (!isBurrow) {
			Burrow();
		}
		else {
			burrowTimer += tick;
			if (burrowTimer >= 2.f) {
				Protrude();
				burrowTimer = 0.f;
				isMoved = true;
			}
		}
	}
}

//void TBoss1::StartPattern(EPatternType type)
//{
//	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ����
//
//	m_activePattern = type;
//
//	// ������ ���� Ÿ�Կ� ���� ������ �ʱ�ȭ �Լ��� ȣ���մϴ�.
//	switch (type)
//	{
//	case EPatternType::BP0034:
//		BP0034();
//		break;
//
//		// �ٸ� ���ϵ��� case�� ���⿡ �߰�...
//	}
//}



void TBoss1::UpdatePattern(float tick)
{
	// Ȱ��ȭ�� ���Ͽ� ���� ������ ������Ʈ �Լ��� �б��մϴ�.
	switch (m_activePattern)
	{
	case EPatternType::BP0011:
		Update_BP0011(tick);
		break;
	case EPatternType::BP0013:
		Update_BP0013(tick);
		break;
	case EPatternType::BP0021:
		Update_BP0021(tick);
		break;
	case EPatternType::BP0022:
		Update_BP0022(tick);
		break;
	case EPatternType::BP0031:
		Update_BP0031(tick);
		break;
	case EPatternType::BP0032:
		Update_BP0032(tick);
		break;
	case EPatternType::BP0033:
		Update_BP0033(tick);
		break;
	case EPatternType::BP0034:
		Update_BP0034(tick);
		break;

		// �ٸ� ���ϵ��� case�� ���⿡ �߰�...

	case EPatternType::None:
	default:
		// ���� ���� ������ ������ �ƹ��͵� ���� �ʽ��ϴ�.
		break;
	}
}

void TBoss1::Update_BP0011(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		RotateToTarget(); //���� ���۽� Ÿ���� �ٶ󺸰�
		//todo :: ���ϸ��̼� ���

		BPTimer += tick;	
		// ������Ʈ �ϳ��� Ȱ��ȭ�մϴ�.
		Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

		Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();

		Mathf::Vector3 dir = targetPos - ownerPos;
		dir.y = 0;
		dir.Normalize();
		Mathf::Vector3 pos = ownerPos + (dir * 2.0f) + (Mathf::Vector3(0, 1, 0) * 0.5);
		ShootProjectile(0, pos, dir);//--> ���ϸ��̼ǿ��� �߻� �����ӿ� ���缭 ȣ���ϴ� ������� ���� ����
			
		// ��� ������Ʈ�� ���������� '�Ϸ� ���' �ܰ�� ��ȯ�մϴ�.
		m_patternPhase = EPatternPhase::Waiting;
		break;
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.

		BPTimer += tick;
		if (BPTimer >= 2.0f) //�� ������ 2���Ŀ� ���� ���� �ð��� ���߿� ���� �����ϰ�
		{
			allFinished = true;
		}
		else {
			allFinished = false;
		}
		//for (int i = 0; i < pattenIndex; ++i)
		//{
		//	if (BP001Objs[i]->IsEnabled())
		//	{
		//		allFinished = false; // ���� �ϳ��� Ȱ�� �����̸� ��⸦ ����մϴ�.
		//		break;
		//	}
		//}

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0013(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		BPTimer += tick;
		while (BPTimer >= BP0013delay)
		{
			if (pattenIndex < 3)
			{
				RotateToTarget(); //���� ���۽� Ÿ���� �ٶ󺸰�
				//todo :: ���ϸ��̼� ���

				// ������Ʈ �ϳ��� Ȱ��ȭ�մϴ�.
				Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

				Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();

				Mathf::Vector3 dir = targetPos - ownerPos;
				dir.y = 0;
				dir.Normalize();
				Mathf::Vector3 pos = ownerPos + (dir * 2.0f) + (Mathf::Vector3(0, 1, 0) * 0.5);
				ShootProjectile(pattenIndex, pos, dir); //--> ���ϸ��̼ǿ��� �߻� �����ӿ� ���缭 ȣ���ϴ� ������� ���� ����

				pattenIndex++;
				BPTimer -= BP0013delay;
			}
			else
			{
				// ��� ������Ʈ�� ���������� '�Ϸ� ���' �ܰ�� ��ȯ�մϴ�.
				m_patternPhase = EPatternPhase::Waiting;
				break; // while ���� Ż��
			}
		}
		break;
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.

		BPTimer += tick;
		if (BPTimer >= 5.0f) //�� ������ 5���Ŀ� ���� ���� �ð��� ���߿� ���� �����ϰ�
		{
			allFinished = true;
		}
		else {
			allFinished = false;
		}
		//for (int i = 0; i < pattenIndex; ++i)
		//{
		//	if (BP001Objs[i]->IsEnabled())
		//	{
		//		allFinished = false; // ���� �ϳ��� Ȱ�� �����̸� ��⸦ ����մϴ�.
		//		break;
		//	}
		//}

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0021(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		BPTimer += tick;
		
		// ���� ���۽� Ÿ���� �ٶ󺸰� ȸ�� �ϴµ� ���⼭ Ÿ���� ���󰡴°� �´°�? 
		Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
		//Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
		Mathf::Vector3 forward = GetOwner()->GetComponent<Transform>()->GetForward();

		//Mathf::Vector3 dir = targetPos - ownerPos;

		////���� ȸ�� --> ���� ���۽� ȸ��
		//dir.y = 0;
		//dir.Normalize();
				
		//���ϸ��̼� ���

		//SweepAttack(ownerPos, dir); // --> ���ϸ��̼ǿ��� ���� ���� �����ӿ� ���缭 ȣ���ϴ� ������� ���� ����
		if (BPTimer >= 2.0f){ //���ϸ��̼� 2�ʷ� ����
			SweepAttack(ownerPos, forward); // --> ���ϸ��̼ǿ��� ���� ���� �����ӿ� ���缭 ȣ���ϴ� ������� ���� ����
			BPTimer -= 2.0f;
		}

		// ��� ������Ʈ�� ���������� '�Ϸ� ���' �ܰ�� ��ȯ�մϴ�.
		if (isAttacked){
			m_patternPhase = EPatternPhase::Waiting;
			break;
		}
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.

		BPTimer += tick;
		if (BPTimer >= 2.0f) //�� ������ 2���Ŀ� ���� ���� �ð��� ���߿� ���� �����ϰ�
		{
			allFinished = true;
		}
		else {
			allFinished = false;
		}
		

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0022(float tick)
{
	// (�̵� + BP0021 ) *5 
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		BPTimer += tick;
		if (pattenIndex < 5) //�����̷�?? �ε�����??
		{
			//�̵�
			BurrowMove(tick);

			if (isMoved) {
				RotateToTarget(); //���� ���۽� Ÿ���� �ٶ󺸰�
				
				// ���� ���۽� Ÿ���� �ٶ󺸰� ȸ�� �ϴµ� ���⼭ Ÿ���� ���󰡴°� �´°�? 
				Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
				//Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
				Mathf::Vector3 forward = GetOwner()->GetComponent<Transform>()->GetForward();
				
				//Mathf::Vector3 dir = targetPos - ownerPos;
				//dir.y = 0;
				//dir.Normalize();

				//���ϸ��̼� ���

				//SweepAttack(ownerPos, dir); // --> ���ϸ��̼ǿ��� ���� ���� �����ӿ� ���缭 ȣ���ϴ� ������� ���� ����
				if (BPTimer >= 2.0f) { //���ϸ��̼� 2�ʷ� ����
					SweepAttack(ownerPos, forward); // --> ���ϸ��̼ǿ��� ���� ���� �����ӿ� ���缭 ȣ���ϴ� ������� ���� ����
					BPTimer -= 2.0f;
				}
				if (isAttacked)
				{
					pattenIndex++;
					isAttacked = false;
					isMoved = false;
				}
			}
		}
		else {
			m_patternPhase = EPatternPhase::Waiting; //���� 5ȸ ���� ������ ��� �ܰ��
			break;
		}
		break;
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		BPTimer += tick;
		if (BPTimer >= 1.0f) //�� ���ϴ��� 1���Ŀ� ���� ���� �ð��� ���߿� ���� �����ϰ�
		{
			allFinished = true;
		}
		else {
			allFinished = false;
		}


		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0031(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		//������� ���� �δ� �׷� ��������
		if (BP003Objs.empty()) { return; }

		//todo: �ϴ� Ÿ������ ���� �÷��̾� ��ġ�� ���� �Ѵ� ����� ���°� �� �÷��̾� ���� �޾Ƽ� ���� �� �غ���
		//Mathf::Vector3 pos = m_target->GetComponent<Transform>()->GetWorldPosition();
		// --> �÷��̾� ���

		bool hasP1 = BB->HasKey("1P");
		bool hasP2 = BB->HasKey("2P");
		int index = 0;
		if (hasP1) {
			Mathf::Vector3 pos = BB->GetValueAsGameObject("1P")->GetComponent<Transform>()->GetWorldPosition();
			//�Ѱ� 
			BP003* script = BP003Objs[index]->GetComponent<BP003>();
			BP003Objs[index]->SetEnabled(true);
			//BP003Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
			script->Initialize(this, pos, BP003Damage, BP003RadiusSize, BP003Delay);
			script->isAttackStart = true;
			index++;
		}

		if (hasP2)
		{
			Mathf::Vector3 pos = BB->GetValueAsGameObject("2P")->GetComponent<Transform>()->GetWorldPosition();
			BP003* script = BP003Objs[index]->GetComponent<BP003>();
			BP003Objs[index]->SetEnabled(true);
			//BP003Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
			script->Initialize(this, pos, BP003Damage, BP003RadiusSize, BP003Delay);
			script->isAttackStart = true;
			index++;
		}


		pattenIndex = index;
		m_patternPhase = EPatternPhase::Waiting;
		break; // while ���� Ż��
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // ���� �ϳ��� Ȱ�� �����̸� ��⸦ ����մϴ�.
				break;
			}
		}

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}

}

void TBoss1::Update_BP0032(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		//3������ ���� ��� ������ �ϴ� �����δ� ��������
		if (BP003Objs.size() < 3) { return; }

		//�ϴ� ����ġ�� ���� ����
		Transform* tr = GetOwner()->GetComponent<Transform>();
		Mathf::Vector3 pos = tr->GetWorldPosition();
		Mathf::Vector3 forward = tr->GetForward();

		//ȸ���� y�� ����
		Mathf::Vector3 up_axis(0.0f, 1.0f, 0.0f);

		//���濡�� 120�� 240 �ؼ� �ﰨ ��� ����
		Mathf::Quaternion rot120 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(120.0f));
		Mathf::Quaternion rot240 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(240.0f));

		//3���� 
		Mathf::Vector3 dir1 = forward; //����
		Mathf::Vector3 dir2 = Mathf::Vector3::Transform(forward, rot120); //120
		Mathf::Vector3 dir3 = Mathf::Vector3::Transform(forward, rot240); //240

		//�ϳ��� ��Ʈ�� �ϱ� ������ ���ͷ� ��������
		std::vector < Mathf::Vector3 > directions;
		directions.push_back(dir1);
		directions.push_back(dir2);
		directions.push_back(dir3);

		int index = 0;

		for (auto& dir : directions) {
			Mathf::Vector3 objpos = pos + dir * 6.0f; // 6��ŭ ������ ��ġ �� ��ġ�� ������Ƽ�� ���� �ϴ� ���� ��� ���Ƿ� �������ִ°ɷ�
			GameObject* floor = BP003Objs[index];
			BP003* script = floor->GetComponent<BP003>();
			floor->SetEnabled(true);
			//floor->GetComponent<Transform>()->SetWorldPosition(objpos); = �ʱ�ȭ����
			script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay);
			script->isAttackStart = true;

			index++;
		}

		pattenIndex = index;
		m_patternPhase = EPatternPhase::Waiting;
		break; // while ���� Ż��
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // ���� �ϳ��� Ȱ�� �����̸� ��⸦ ����մϴ�.
				break;
			}
		}

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0033(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		MoveToChunsik(tick);
		if (isMoved) {
			Transform* tr = GetOwner()->GetComponent<Transform>();
			//if (m_chunsik) {
			//	Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
			//	tr->SetWorldPosition(chunsik);
			//	//�̶� ȸ���̳� ���� �͵� �˸��� ������ ���� ������
			//}
			//�ϴ� ������ġ�� ���� ����
			Mathf::Vector3 pos = tr->GetWorldPosition();
			Mathf::Vector3 forward = tr->GetForward();

			//ȸ���� y�� ����
			Mathf::Vector3 up_axis(0.0f, 1.0f, 0.0f);

			//���濡�� 120�� 240 �ؼ� �ﰨ ��� ����
			Mathf::Quaternion rot120 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(120.0f));
			Mathf::Quaternion rot240 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(240.0f));

			//3���� 
			Mathf::Vector3 dir1 = forward; //����
			Mathf::Vector3 dir2 = Mathf::Vector3::Transform(forward, rot120); //120
			Mathf::Vector3 dir3 = Mathf::Vector3::Transform(forward, rot240); //240

			//�ϳ��� ��Ʈ�� �ϱ� ������ ���ͷ� ��������
			std::vector < Mathf::Vector3 > directions;
			directions.push_back(dir1);
			directions.push_back(dir2);
			directions.push_back(dir3);

			int index = 0;

			for (auto& dir : directions) {
				//������
				Mathf::Vector3 objpos = pos + dir * 6.0f; // 6��ŭ ������ ��ġ �� ��ġ�� ������Ƽ�� ���� �ϴ� ���� ��� ���Ƿ� �������ִ°ɷ�
				GameObject* floor = BP003Objs[index];
				BP003* script = floor->GetComponent<BP003>();
				floor->SetEnabled(true);
				//floor->GetComponent<Transform>()->SetWorldPosition(objpos);
				script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay, true, false);
				script->isAttackStart = true;

				index++;

				//�հ�
				Mathf::Vector3 objpos2 = pos + dir * 12.0f; // 12��ŭ ������ ��ġ �� ��ġ�� ������Ƽ�� ���� �ϴ� ���� ��� ���Ƿ� �������ִ°ɷ�
				GameObject* floor2 = BP003Objs[index];
				BP003* script2 = floor2->GetComponent<BP003>();
				floor2->SetEnabled(true);
				//floor2->GetComponent<Transform>()->SetWorldPosition(objpos2);
				script2->Initialize(this, objpos2, BP003Damage, BP003RadiusSize, BP003Delay, true);
				script2->isAttackStart = true;

				index++;
			}
			pattenIndex = index;
			m_patternPhase = EPatternPhase::Waiting;
			break; // while ���� Ż��
		}
		break;
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // ���� �ϳ��� Ȱ�� �����̸� ��⸦ ����մϴ�.
				break;
			}
		}

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0034(float tick)
{
	// ���� �ܰ�(Phase)�� ���� �ٸ� ������ �����մϴ�.
	// ���� 1���� -> ���� 4�� �� 
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		MoveToChunsik(tick);
		if (isMoved) {
			BPTimer += tick;
			while (BPTimer >= BP0034delay)
			{
				if (pattenIndex < BP0034Points.size())
				{
					// ������Ʈ �ϳ��� Ȱ��ȭ�մϴ�. --> row ���� 4����
					int i = 0;
					for (; i < 4;)
					{
						Mathf::Vector3 objpos = BP0034Points[pattenIndex + i].second;
						GameObject* floor = BP003Objs[pattenIndex + i];
						BP003* script = floor->GetComponent<BP003>();
						floor->SetEnabled(true);
						script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay);
						script->isAttackStart = true;
						i++;
						if (pattenIndex + i > BP0034Points.size()) {
							i--;
							break; // ������ ����� �ʵ��� �˻�
						}
					}
					pattenIndex += i;
					BPTimer -= BP0034delay;
				}
				else
				{
					// ��� ������Ʈ�� ���������� '�Ϸ� ���' �ܰ�� ��ȯ�մϴ�.
					m_patternPhase = EPatternPhase::Waiting;
					break; // while ���� Ż��
				}
			}
		}
		break;
	}
	case EPatternPhase::Waiting:
	{
		bool allFinished = true;
		// Ȱ��ȭ�ߴ� ��� ������Ʈ�� ��ȸ�ϸ� ��Ȱ��ȭ�Ǿ����� Ȯ���մϴ�.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // ���� �ϳ��� Ȱ�� �����̸� ��⸦ ����մϴ�.
				break;
			}
		}

		if (allFinished)
		{
			// ��� ������Ʈ�� ��Ȱ��ȭ�Ǿ����Ƿ� ������ ������ �����մϴ�.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Calculate_BP0034()
{
	//����̿��� ������ ���
	Transform* tr = m_chunsik->GetComponent<Transform>();

	//todo: �ϴ� Ÿ������ ���� �÷��̾� ��ġ�� ���� �Ѵ� ����� ���°� �� �÷��̾� ���� �޾Ƽ� ���� �� �غ���
	Mathf::Vector3 targetPos;

	if (m_target) {
		targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
	}
	else {//������ ���ܼ� Ÿ���� �Ҿ� ���ȴ´� ���� �߻��� 
		//�ϴ� �÷��̾�1�� �������� �������� -> todo

	}


	Mathf::Vector3 direction; //�켱 ����
	Mathf::Vector3 pos = tr->GetWorldPosition();//����� ��ġ 
	direction = targetPos - pos;
	direction.Normalize();

	// �� 1. ���� ����� 8���� ���� ã��, �밢������ �Ǻ�
	const std::vector<Mathf::Vector3> eightDirections = {
		Mathf::Vector3(0.f, 0.f, 1.f),   // 0: N
		Mathf::Vector3(1.f, 0.f, 1.f),   // 1: NE
		Mathf::Vector3(1.f, 0.f, 0.f),   // 2: E
		Mathf::Vector3(1.f, 0.f, -1.f),  // 3: SE
		Mathf::Vector3(0.f, 0.f, -1.f),  // 4: S
		Mathf::Vector3(-1.f, 0.f, -1.f), // 5: SW
		Mathf::Vector3(-1.f, 0.f, 0.f),  // 6: W
		Mathf::Vector3(-1.f, 0.f, 1.f)   // 7: NW
	};

	int closestDirIndex = -1;
	float maxDot = -2.0f;
	for (int i = 0; i < 8; ++i) {
		Mathf::Vector3 normalizedDir = eightDirections[i];
		normalizedDir.Normalize();
		float dot = direction.Dot(normalizedDir);
		if (dot > maxDot) {
			maxDot = dot;
			closestDirIndex = i;
		}
	}

	// �밢�� ����(�ε����� Ȧ��)�� ��쿡�� 45�� ȸ��
	bool shouldRotate = (closestDirIndex % 2 != 0);


	// �� 2. ������ ȸ���� ���� (�밢���� �ƴϸ� Identity, �� 0�� ȸ��)
	Mathf::Quaternion gridRotation = Mathf::Quaternion::Identity;
	if (shouldRotate) {
		const float ANGLE_45_RAD = 3.1415926535f / 4.f;
		gridRotation = Mathf::Quaternion::CreateFromAxisAngle(Mathf::Vector3(0, 1, 0), ANGLE_45_RAD);
	}
	Mathf::Quaternion inverseGridRotation = gridRotation;
	if (shouldRotate) {
		inverseGridRotation.Conjugate();
	}


	//// 3. ���� �⺻ �� ��� �� ���� ��Ģ ���� (Tilted ������ ������ ����)
	//const float PI = 3.1415926535f;
	//float circleArea = PI * m_chunsikRadius * m_chunsikRadius;
	//float singleCellArea = circleArea / 16;
	//float gridSpacing = sqrt(singleCellArea);
	//int gridDimension = static_cast<int>(ceil(m_chunsikRadius * 2 / gridSpacing));

	Mathf::Vector3 localPlayerDir = Mathf::Vector3::Transform(direction, inverseGridRotation);
	bool isMajorAxisX = std::abs(localPlayerDir.x) > std::abs(localPlayerDir.z);
	float majorSortSign = (isMajorAxisX ? localPlayerDir.x : localPlayerDir.z) < 0.f ? 1.f : -1.f;
	float minorSortSign = (isMajorAxisX ? localPlayerDir.z : localPlayerDir.x) < 0.f ? 1.f : -1.f;

	//// 3. �켱������ ��ġ�� ������ ����
	////std::vector<std::pair<int, Mathf::Vector3>> placements;
	//BP0034Points.clear();

	//// 4. ���� ��ȸ (Tilted ������ ������ ����)
	//int iz = 0;
	//for (float z = -m_chunsikRadius; z <= m_chunsikRadius; z += gridSpacing, ++iz) {
	//	int ix = 0;
	//	for (float x = -m_chunsikRadius; x <= m_chunsikRadius; x += gridSpacing, ++ix) {
	//		Mathf::Vector3 localOffset(x, 0, z);
	//		if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

	//		Mathf::Vector3 rotatedOffset = Mathf::Vector3::Transform(localOffset, gridRotation);
	//		Mathf::Vector3 spawnPos = pos + rotatedOffset;

	//		int majorIndex, minorIndex;
	//		if (isMajorAxisX) { majorIndex = ix; minorIndex = iz; }
	//		else { majorIndex = iz; minorIndex = ix; }

	//		int priority = static_cast<int>((majorIndex * majorSortSign) * gridDimension + (minorIndex * minorSortSign));
	//		BP0034Points.push_back({ priority, spawnPos });
	//	}
	//}
	int pointsPerRow = 4;
	float gridSpacing = (m_chunsikRadius * 2) / (pointsPerRow - 1);
	BP0034Points.clear();

	// 4. ���� ��ȸ (�ξ� ��������)
	for (int iz = 0; iz < pointsPerRow; ++iz) {
		for (int ix = 0; ix < pointsPerRow; ++ix) {
			// -halfSize ~ +halfSize ������ ��ǥ ���
			float x = -m_chunsikRadius + ix * gridSpacing;
			float z = -m_chunsikRadius + iz * gridSpacing;

			Mathf::Vector3 localOffset(x, 0, z);

			// �ڡڡ� �� �ۿ� �ִ��� �˻��ϴ� if���� �ʿ� ������! �ڡڡ�
			// if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

			Mathf::Vector3 rotatedOffset = Mathf::Vector3::Transform(localOffset, gridRotation);
			Mathf::Vector3 spawnPos = pos + rotatedOffset;

			int majorIndex, minorIndex;
			if (isMajorAxisX) { majorIndex = ix; minorIndex = iz; }
			else { majorIndex = iz; minorIndex = ix; }

			int priority = static_cast<int>((majorIndex * majorSortSign) * pointsPerRow + (minorIndex *
				minorSortSign));
			BP0034Points.push_back({ priority, spawnPos });
		}
	}


	// 5. �켱����(pair.first)�� ���� �⺻ �������� ����
	std::sort(BP0034Points.begin(), BP0034Points.end(), [](auto a, auto b) {
		return a.first < b.first;
		});

}

void TBoss1::EndPattern()
{
	m_activePattern = EPatternType::None;
	m_patternPhase = EPatternPhase::Inactive;
}

void TBoss1::SelectTarget()
{
	//todo : Ÿ�� ���� (1P/2P) �Լ��� ���� ������ ���Ͽ� ���� Ÿ���� ���� �������� �ٸ��� �ֱ� ����
	//�̱��÷��̾� ��� ���ɼ� �����Ƿ� 1P/2P�� ��� �ִ��� Ȯ��
	bool has1P = BB->HasKey("1P");
	bool has2P = BB->HasKey("2P");
	if (!has1P && !has2P) {
		//���� �÷��̾ ã�� �� ����
		return;
	}
	if (has1P && !has2P) {
		//1P�� ����
		m_target = BB->GetValueAsGameObject("1P"); //������ 1P
		return;
	}
	//2P�� ���� �ϴ� ��찡 ������? --> �̰� ���߿� ��������
	//�ƴ϶�� ���� ���õǾ��� Ƚ�� ��
	if (p1Count != p2Count)
	{
		if (p1Count < p2Count) {
			m_target = BB->GetValueAsGameObject("1P");
			p1Count++;
		}
		else {
			m_target = BB->GetValueAsGameObject("2P");
			p2Count++;
		}
		return;
	}
	//Ƚ���� ���ٸ�?
	//���� �佺 ó�� 2/1 Ȯ���� 1P/2P ����
	int randValue = rand() % 2; // 0, 1 �� �ϳ��� �������� ����
	if (randValue == 0) {
		m_target = BB->GetValueAsGameObject("1P");
		p1Count++;
	}
	else {
		m_target = BB->GetValueAsGameObject("2P");
		p2Count++;
	}
	
}

void TBoss1::Burrow()
{
	//todo : �������� ��
	//�������� ���� ���ϸ��̼� ���
	//���� �Ⱥ��̰� ó��
	//�ݶ��̴� ��Ȱ��ȭ
	m_rigid->SetColliderEnabled(false);
	isBurrow = true;
}

void TBoss1::Protrude()
{
	//todo : ���ӿ��� ����
	
	//Ƣ����� ���� �̵� ���ɿ��� �ȿ� �ִ��� Ȯ��
	Mathf::Vector3 chunsikPos = m_chunsik->GetComponent<Transform>()->GetWorldPosition(); //�����(�߽�) ��ġ
	//Ÿ�� Ȯ��  ==> Ÿ���� ���ų� �Ҿ���ȴٸ�? �׷��ٸ� ���� ��ġ��
	Mathf::Vector3 targetPos;
	if(m_target)
	{
		targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
		targetPos.y = chunsikPos.y; //���� ����
	}
	else {
		//����̸� �������� ���� ��ġ
		float angle = static_cast<float>(rand()) / RAND_MAX * 2.f * 3.1415926535f; // ����� ���� ���� ����
		float radius = static_cast<float>(rand()) / RAND_MAX * m_chunsikRadius; // ����� ���� ���� �Ÿ�
		targetPos = chunsikPos + Mathf::Vector3(cos(angle) * radius, 0, sin(angle) * radius); // ���� �������� ���� ��ġ ���
	}

	float dist = (targetPos - chunsikPos).Length();
	if (dist < m_chunsikRadius)
	{
		//����� �ݰ� �ȿ� ������ Ÿ�� ��ġ��
	}
	else {
		//����� �ݰ� �ۿ� ������ �ݰ� ���� �����ڸ���
		Mathf::Vector3 dir = targetPos - chunsikPos;
		dir.Normalize();
		targetPos = chunsikPos + dir * (m_chunsikRadius - 1.f); // �ణ ��������
	}

	//���� ��ġ ����
	m_pOwner->GetComponent<Transform>()->SetWorldPosition(targetPos); //�ش� ��ġ���� ��Ǹ� �����ְ� �ٷ� �ݶ��̴� Ȱ��ȭ
	//�ε������� �ʿ� ==> �ش� ��ġ�� �ε������� ���� �� ��� ��� �� Ƣ�����
	
	//�� ���̰� ó�� �ϸ�

	//���ӿ��� ������ ���ϸ��̼� ���

	//�ö���鼭 �÷��̾� ������ ���� + �÷��̾� �˹�
	
	//�ݶ��̴� Ȱ��ȭ
	m_rigid->SetColliderEnabled(true);
	isBurrow = false;
}

void TBoss1::ProtrudeChunsik()
{
	Mathf::Vector3 chunsikPos = m_chunsik->GetComponent<Transform>()->GetWorldPosition(); //�����(�߽�) ��ġ
	m_pOwner->GetComponent<Transform>()->SetWorldPosition(chunsikPos); //�ش� ��ġ���� ��Ǹ� �����ְ� �ٷ� �ݶ��̴� Ȱ��ȭ

	//���Ͻ� ������ �߽��̵� ��Ű�� ���� --> �̶��� �ε������Ϳ� ������ ������ �ʿ� �Ѱ�?

	//�� ���̰� ó�� �ϸ�

	//���ӿ��� ������ ���ϸ��̼� ���
	m_rigid->SetColliderEnabled(true);
	isBurrow = false;
}


void TBoss1::BP0011()
{
	//todo : Ÿ���� ���� ȭ�� ����ü�� 1�� �߻��Ͽ� ����
	//�� �� ���� �غ��� �ϴ� ���� ���� ȸ���� ���Ͽ� ������ ��� �÷��̾ ���� ���Ŵ�
	//����ü�� ���Ͽ� ��� Ÿ�ֿ̹� ������ ����� ���̴°�
	//�ϴ� Ÿ������

	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ����
	//
	m_activePattern = EPatternType::BP0011;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	BPTimer = 0.f;

}

void TBoss1::BP0012()
{
	//todo : 60�� ��ä�� ������ ȭ�� ����ü 3�� �߻�
	EndPattern();
}

void TBoss1::BP0013()
{
	//todo : Ÿ���� ���� ȭ�� ����ü�� 3�� ���� �߻��Ͽ� ����
	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ����
	//
	m_activePattern = EPatternType::BP0013;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	BPTimer = 0.f;
}

void TBoss1::BP0014()
{
	//todo : 60�� ��ä�� ������ ȭ�� ����ü 5���� 2ȸ ���� �߻�
	EndPattern();
}

void TBoss1::BP0021()
{
	//todo : �������� ������ �ֵѷ� ������ ����
	//
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ����
	//
	m_activePattern = EPatternType::BP0021;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	isAttacked = false;
	BPTimer = 0.f;

	RotateToTarget(); //���� ���۽� Ÿ���� �ٶ󺸰�
}

void TBoss1::BP0022()
{
	//todo : BP0021 * 5  ��ȸ Ÿ�� ����
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ����
	//
	m_activePattern = EPatternType::BP0022;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	isAttacked = false;
	isMoved = false;
	BPTimer = 0.f;
}

void TBoss1::BP0031()
{
	std::cout << "BP0031" << std::endl;
	//target ��� ���� �ϳ�? ���� �ؿ� �ϳ�? ���� ��ġ �ϳ�? 
	//todo: --> �÷��̾� �� ��ŭ 1���� �� ��ġ��
	
	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ���� //�� �˻�� BT���� �ϴ°� ���� �ʳ�?
	//
	m_activePattern = EPatternType::BP0031;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	BPTimer = 0.f;
}

void TBoss1::BP0032()
{
	std::cout << "BP0032" << std::endl;
	//�� ������ 3��
	
	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ���� //�� �˻�� BT���� �ϴ°� ���� �ʳ�?
	//
	m_activePattern = EPatternType::BP0032;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	BPTimer = 0.f;

}

void TBoss1::BP0033()
{
	std::cout << "BP0033" << std::endl;
	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ���� //�� �˻�� BT���� �ϴ°� ���� �ʳ�?
	//
	m_activePattern = EPatternType::BP0033;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	isMoved = false;
	BPTimer = 0.f;

}

void TBoss1::BP0034()
{
	std::cout << "BP0034" << std::endl;
	//�� ������ �켱���� �밢�� �������� �� ��ü�� ���� ������� ���������� ���� ����
	//=> todo : ���� ���� �켱������ ���پ� ���� / �߰������� ���������� ������Ƽ ����

	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ���� //�� �˻�� BT���� �ϴ°� ���� �ʳ�?
	//
	m_activePattern = EPatternType::BP0034;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	isMoved = false;
	BPTimer = 0.f;

	// 2. �� ���Ͽ� ����� ���� ��ġ�� �̸� ����մϴ�.
	Calculate_BP0034();
}

