#include "TutorialScript.h"
#include "pch.h"
#include "EntityAsis.h"
#include "Entity.h"
#include "Player.h"
#include "GameManager.h"
#include "PrefabUtility.h"
#include "Core.Random.h"
#include "TweenManager.h"
void TutorialScript::Start()
{


	auto GMObj = GameObject::Find("GameManager");
	if (GMObj)
	{
		GM = GMObj->GetComponent<GameManager>();
	}

	auto children = GetOwner()->m_childrenIndices;
	for (auto& child : children)
	{
		GameObject* childObj = GameObject::FindIndex(child);
		if (childObj->m_tag == "Target")
		{
			m_targetObj = childObj;
		}

	}

	if (m_targetObj)
	{
		auto hostchildren = m_targetObj->m_childrenIndices;
		for (auto& child : hostchildren)
		{
			GameObject* childObj = GameObject::FindIndex(child);
			if (childObj)
			{
				auto entity = childObj->GetComponentDynamicCast<Entity>();
				if (entity)
				{
					m_targets.push_back(entity);
					entity->SetTuToMooJuk(true);
				}
			}
		}
	}



	if (EventID == 3111)
	{
		GM->SetReadyItemTutorial(false);
	}

}

void TutorialScript::OnTriggerEnter(const Collision& collision)
{
	EntityAsis* asis = collision.otherObj->GetComponent<EntityAsis>();
	if (asis)
	{
		if (EventID == 3107)
		{
			SpawnItem();
		}
		else if (EventID == 3111)
		{
			GM->SetReadyItemTutorial(true);
		}
		else
		{
			for (auto& target : m_targets)
			{
				target->SetTuToMooJuk(false);
			}
		}
	}
}

void TutorialScript::OnTriggerStay(const Collision& collision)
{
}

void TutorialScript::Update(float tick)
{
}

void TutorialScript::SpawnItem()
{
	float PI = 3.141592;
	Mathf::Vector3 OwnerPos = GetOwner()->m_transform.GetWorldPosition();
	float radius = 6.0f; // 원의 반지름
	int itemCount = 6;



	
	Mathf::Vector3 temp = { 0,0.6f,0 };

	Prefab* meleeItem = PrefabUtilitys->LoadPrefab("BoxMineral");
	Prefab* rangeItem = PrefabUtilitys->LoadPrefab("BoxMushroom");


	if (meleeItem && rangeItem)
	{

		for (int i = 0; i < itemCount; ++i)
		{
			GameObject* itemObj = nullptr;
			if (i <3)
			{
				itemObj = PrefabUtilitys->InstantiatePrefab(meleeItem, "entityItem");
			}
			else
			{
				itemObj = PrefabUtilitys->InstantiatePrefab(rangeItem, "entityItem");
			}
			float angle = (PI / itemCount) * i; // 각 아이템의 각도 (라디안)
			Mathf::Vector3 spawnpos = OwnerPos;
			spawnpos.x += radius * cosf(angle);
			spawnpos.z += radius * sinf(angle);
			spawnpos.y += 0.1f;
			itemObj->m_transform.SetPosition(spawnpos);


			float f = Random<float>(2.f, 3.f).Generate();
			auto tween = std::make_shared<Tweener<float>>(
				[=]() { return 0.f; },
				[=](float val) {
					Mathf::Vector3 pos = spawnpos;
					float force = f; // 중력 비슷하게 y축 곡선
					pos.x = Mathf::Lerp(spawnpos.x, spawnpos.x + temp.x, val);
					pos.z = Mathf::Lerp(spawnpos.z, spawnpos.z + temp.z, val);
					pos.y = Mathf::Lerp(spawnpos.y, spawnpos.y + temp.y, val)
						+ force * (1 - (2 * val - 1) * (2 * val - 1));
					itemObj->m_transform.SetPosition(pos);
				},
				1.f,
				.5f,
				[](float t) { return Easing::Linear(t); }
			);

				if (GM)
				{
					auto tweenManager = GM->GetComponent<TweenManager>();
					if (tweenManager)
					{
						tweenManager->AddTween(tween);
					}
				}
			}
		}
		
	

	
}



