#include "TBoss1.h"
#include "pch.h"
#include "CharacterControllerComponent.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include <utility>
#include "BP003.h"
#include "BP001.h"

void TBoss1::Start()
{
	BT = m_pOwner->GetComponent<BehaviorTreeComponent>();
	BB = BT->GetBlackBoard();


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

	RotateToTarget();

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

void TBoss1::Update_BP0034(float tick)
{
	// ���� �ܰ�(Phase)�� ���� �ٸ� ������ �����մϴ�.
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		bp0034Timer += tick;
		while (bp0034Timer >= delay)
		{
			if (pattenIndex < BP0034Points.size())
			{
				// ������Ʈ �ϳ��� Ȱ��ȭ�մϴ�.
				Mathf::Vector3 objpos = BP0034Points[pattenIndex].second;
				GameObject* floor = BP003Objs[pattenIndex];
				BP003* script = floor->GetComponent<BP003>();
				floor->SetEnabled(true);
				script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay);
				script->isAttackStart = true;

				pattenIndex++;
				bp0034Timer -= delay;
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
	Transform* tr = GetOwner()->GetComponent<Transform>();
	//todo: �ϴ� Ÿ������ ���� �÷��̾� ��ġ�� ���� �Ѵ� ����� ���°� �� �÷��̾� ���� �޾Ƽ� ���� �� �غ���
	Mathf::Vector3 targetPos;

	if (m_target) {
		targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
	}
	else {//������ ���ܼ� Ÿ���� �Ҿ� ���ȴ´� ���� �߻��� 
		//�ϴ� �÷��̾�1�� �������� �������� -> todo

	}


	Mathf::Vector3 direction; //�켱 ����
	Mathf::Vector3 pos = tr->GetWorldPosition();//���� ��ġ 
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


	// 3. ���� �⺻ �� ��� �� ���� ��Ģ ���� (Tilted ������ ������ ����)
	const float PI = 3.1415926535f;
	float circleArea = PI * m_chunsikRadius * m_chunsikRadius;
	float singleCellArea = circleArea / 16;
	float gridSpacing = sqrt(singleCellArea);
	int gridDimension = static_cast<int>(ceil(m_chunsikRadius * 2 / gridSpacing));

	Mathf::Vector3 localPlayerDir = Mathf::Vector3::Transform(direction, inverseGridRotation);
	bool isMajorAxisX = std::abs(localPlayerDir.x) > std::abs(localPlayerDir.z);
	float majorSortSign = (isMajorAxisX ? localPlayerDir.x : localPlayerDir.z) < 0.f ? 1.f : -1.f;
	float minorSortSign = (isMajorAxisX ? localPlayerDir.z : localPlayerDir.x) < 0.f ? 1.f : -1.f;

	// 3. �켱������ ��ġ�� ������ ����
	//std::vector<std::pair<int, Mathf::Vector3>> placements;
	BP0034Points.clear();

	// 4. ���� ��ȸ (Tilted ������ ������ ����)
	int iz = 0;
	for (float z = -m_chunsikRadius; z <= m_chunsikRadius; z += gridSpacing, ++iz) {
		int ix = 0;
		for (float x = -m_chunsikRadius; x <= m_chunsikRadius; x += gridSpacing, ++ix) {
			Mathf::Vector3 localOffset(x, 0, z);
			if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

			Mathf::Vector3 rotatedOffset = Mathf::Vector3::Transform(localOffset, gridRotation);
			Mathf::Vector3 spawnPos = pos + rotatedOffset;

			int majorIndex, minorIndex;
			if (isMajorAxisX) { majorIndex = ix; minorIndex = iz; }
			else { majorIndex = iz; minorIndex = ix; }

			int priority = static_cast<int>((majorIndex * majorSortSign) * gridDimension + (minorIndex * minorSortSign));
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

void TBoss1::BP0011()
{
	//todo : Ÿ���� ���� ȭ�� ����ü�� 1�� �߻��Ͽ� ����
	//�� �� ���� �غ��� �ϴ� ���� ���� ȸ���� ���Ͽ� ������ ��� �÷��̾ ���� ���Ŵ�
	//����ü�� ���Ͽ� ��� Ÿ�ֿ̹� ������ ����� ���̴°�
	//�ϴ� Ÿ������

	if (!m_target) {
		return;
	}

	//������� ���� �δ� �׷� ��������
	if (BP001Objs.empty()) { return; }

	Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	
	Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
	
	Mathf::Vector3 dir = targetPos - ownerPos;
	dir.y = 0;
	dir.Normalize();
	Mathf::Vector3 pos = ownerPos + (dir * 2.0f) + (Mathf::Vector3(0, 1, 0) * 0.5);
	

	BP001* script = BP001Objs[0]->GetComponent<BP001>();
	BP001Objs[0]->SetEnabled(true);
	script->Initialize(this, pos, dir, BP001Damage, BP001RadiusSize, BP001Delay, BP001Speed);
	script->isAttackStart = true;

}

void TBoss1::BP0012()
{
	//todo : 60�� ��ä�� ������ ȭ�� ����ü 3�� �߻�
}

void TBoss1::BP0013()
{
	//todo : Ÿ���� ���� ȭ�� ����ü�� 3�� ���� �߻��Ͽ� ����
}

void TBoss1::BP0014()
{
	//todo : 60�� ��ä�� ������ ȭ�� ����ü 5���� 2ȸ ���� �߻�
}

void TBoss1::BP0031()
{
	std::cout << "BP0031" << std::endl;
	//target ��� ���� �ϳ�? ���� �ؿ� �ϳ�? ���� ��ġ �ϳ�? 
	//todo: --> �÷��̾� �� ��ŭ 1���� �� ��ġ��
	if (!m_target) {
		return;
	}

	//������� ���� �δ� �׷� ��������
	if (BP003Objs.empty()) { return; }

	//todo: �ϴ� Ÿ������ ���� �÷��̾� ��ġ�� ���� �Ѵ� ����� ���°� �� �÷��̾� ���� �޾Ƽ� ���� �� �غ���
	Mathf::Vector3 pos = m_target->GetComponent<Transform>()->GetWorldPosition();
	
	//�Ѱ� 
	BP003* script = BP003Objs[0]->GetComponent<BP003>();
	BP003Objs[0]->SetEnabled(true);
	//BP003Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
	script->Initialize(this, pos, BP003Damage, BP003RadiusSize, BP003Delay);
	script->isAttackStart = true;
}

void TBoss1::BP0032()
{
	std::cout << "BP0032" << std::endl;
	//�� ������ 3��
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

	////�� ���� ������ ���� �� �ʿ���� �����ð�
	//index = 0; //���� ������ �Լ� �θ������� �ʱ�ȭ �ǰ����� �ϴ� ����
	//directions.clear(); //����

}

void TBoss1::BP0033()
{
	std::cout << "BP0033" << std::endl;
	//�߿� ������ �߾� ��ǥ�� ��� ���ұ�?
	//->���Ϳ� chunsik�� ���� ���� �װ� ������ ã��.
	//����� �������� �Ÿ����ؼ� �̵� ���ɵ� ���ϰ�
	//��ü �ʵ� ����� �������� ���� ���ο� ���� 4*4���ڸ� ������ ��ü �� ���� 
	//���� �߰� ���� ������ �� BP003Obj�� ����� Ȥ�� ����� ��ġ�� �˰� �־�� �Ѵ�.
	//�ʱ�ȭ �Լ��� �Ѱ��ֳ�? �ƴϸ� ���� ������ ��� ��������?
	
	//�ϴ� ������ ����� ��ġ�� �Ű�  --> �Ḹ ������ cct�� �����̳�?? �׷� �� ����δ�.. �̵� ���� ������ �ϳ�? -->������ CCT ���� ����
	//CharacterControllerComponent* cct = GetOwner()->GetComponent< CharacterControllerComponent>();
	
	Transform* tr = GetOwner()->GetComponent<Transform>();
	if (m_chunsik) {
		Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
		tr->SetWorldPosition(chunsik);
		//�̶� ȸ���̳� ���� �͵� �˸��� ������ ���� ������
	}
	//����� ������ ���ڸ�

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
		script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay,true,false);
		script->isAttackStart = true;

		index++;

		//�հ�
		Mathf::Vector3 objpos2 = pos + dir * 12.0f; // 12��ŭ ������ ��ġ �� ��ġ�� ������Ƽ�� ���� �ϴ� ���� ��� ���Ƿ� �������ִ°ɷ�
		GameObject* floor2 = BP003Objs[index];
		BP003* script2 = floor2->GetComponent<BP003>();
		floor2->SetEnabled(true);
		//floor2->GetComponent<Transform>()->SetWorldPosition(objpos2);
		script2->Initialize(this, objpos2, BP003Damage, BP003RadiusSize, BP003Delay,true);
		script2->isAttackStart = true;

		index++;
	}

}

void TBoss1::BP0034()
{
	std::cout << "BP0034" << std::endl;
	//�� ������ �켱���� �밢�� �������� �� ��ü�� ���� ������� ���������� ���� ����
	//=> todo : ���� ���� �켱������ ���پ� ���� / �߰������� ���������� ������Ƽ ����

	//�ϴ� ������ ����� ��ġ�� �Ű� 0033�� ����
	Transform* tr = GetOwner()->GetComponent<Transform>();
	if (m_chunsik) {
		Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
		tr->SetWorldPosition(chunsik);
		//�̶� ȸ���̳� ���� �͵� �˸��� ������ ���� ������
	}
	//����� ������ ���ڸ�

	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // �ٸ� ������ ���� ���̸� ����
	//
	m_activePattern = EPatternType::BP0034;
	/* start patten end*/

	// 1. ������ �ܰ踦 '���� ��'���� �����ϰ� ���� �������� �ʱ�ȭ�մϴ�.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	bp0034Timer = 0.f;

	// 2. �� ���Ͽ� ����� ���� ��ġ�� �̸� ����մϴ�.
	Calculate_BP0034();
}

