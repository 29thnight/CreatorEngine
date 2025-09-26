#include "TBoss1.h"
#include "pch.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include "BP003.h"

void TBoss1::Start()
{
	BT = m_pOwner->GetComponent<BehaviorTreeComponent>();
	BB = BT->GetBlackBoard();


	//prefab load
	//Prefab* BP001Prefab = PrefabUtilitys->LoadPrefab("BP001");
	//Prefab* BP003Prefab = PrefabUtilitys->LoadPrefab("BP003");

	////1�� ���� ����ü �ִ� 10��
	////3�� ���� ���ǵ� �ִ� 10��
	//for (size_t i = 0; i < 10; i++)
	//{
	//	std::string Projectilename = "BP001Projectile" + i;
	//	std::string Floorname = "BP003Floor" + i;
	//	GameObject* Prefab1 = PrefabUtilitys->InstantiatePrefab(BP001Prefab, Projectilename);
	//	GameObject* Prefab2 = PrefabUtilitys->InstantiatePrefab(BP003Prefab, Floorname);
	//	BP001Objs.push_back(Prefab1);
	//	BP003Objs.push_back(Prefab2);
	//}
	

}

void TBoss1::Update(float tick)
{
	//test code
	bool hasTarget = BB->HasKey("1P");
	if (hasTarget) {
		m_target = BB->GetValueAsGameObject("1P");
	}
}

void TBoss1::BP0031()
{
	std::cout << "BP0031" << std::endl;
	//target ��� ���� �ϳ�? ���� �ؿ� �ϳ�? ���� ��ġ �ϳ�? 
	//�׽�Ʈ������ �ϴ� Ÿ�� �÷��̾ �� �ؿ� �ϳ�
	if (!m_target) {
		return;
	}

	Mathf::Vector3 pos = m_target->GetComponent<Transform>()->GetWorldPosition();
	
	//�Ѱ� 
	BP003* script = BP001Objs[0]->GetComponent<BP003>();
	BP001Objs[0]->SetEnabled(true);
	BP001Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
	script->Initialize(this, BP003Damage, BP003RadiusSize, BP003Delay);
	script->isAttackStart = true;
}

void TBoss1::BP0032()
{
	std::cout << "BP0032" << std::endl;
	//�� ������ 3��
}

void TBoss1::BP0033()
{
	std::cout << "BP0033" << std::endl;
}

void TBoss1::BP0034()
{
	std::cout << "BP0034" << std::endl;
}

