#include "TBoss1.h"
#include "pch.h"
#include "CharacterControllerComponent.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include "BP003.h"

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
	//Prefab* BP001Prefab = PrefabUtilitys->LoadPrefab("Boss1BP001Obj");
	Prefab* BP003Prefab = PrefabUtilitys->LoadPrefab("Boss1BP003Obj");

	////1�� ���� ����ü �ִ� 10��
	for (size_t i = 0; i < 10; i++) {
		//	std::string Projectilename = "BP001Projectile" +std::to_string(i);
		//	GameObject* Prefab1 = PrefabUtilitys->InstantiatePrefab(BP001Prefab, Projectilename);
		//	Prefab1->SetEnabled(false);
		//	BP001Objs.push_back(Prefab1);
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
	//test code
	bool hasTarget = BB->HasKey("1P");
	if (hasTarget) {
		m_target = BB->GetValueAsGameObject("1P");
	}

	RotateToTarget();
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

void TBoss1::PattenActionIdle()
{
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
}

