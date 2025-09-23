#include "EntityResource.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "RigidBodyComponent.h"
#include "EntityItem.h"
#include "SceneManager.h"
#include "EffectComponent.h"
#include "DebugLog.h"
#include "PrefabUtility.h"
#include "CriticalMark.h"
#include "TweenManager.h"
#include "Core.Random.h"
void EntityResource::Start()
{


	m_criticalMark = GetOwner()->GetComponent<CriticalMark>();


	auto owner = GetOwner();
	auto childred = owner->m_childrenIndices;
	for (auto& child : childred)
	{
		m_criticalMark = GameObject::FindIndex(child)->GetComponent<CriticalMark>();
	}
	if (!m_criticalMark)
	{
		m_criticalMark = GetOwner()->GetComponent<CriticalMark>();
	}


}

void EntityResource::Update(float tick)
{
}
void EntityResource::SendDamage(Entity* sender, int damage)
{
	//LOG("EntityResource Attack");
	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		if (player)
		{
			// hit
 			if (m_criticalMark != nullptr && true == m_criticalMark->UpdateMark(player->playerIndex))
			{
				//크리티컬 성공
			}
			m_currentHP -= std::max(damage, 0);

			if (m_currentHP <= 0) {
				// dead

				//auto& resources = GameObject::Find("GameManager")->GetComponent<GameManager>()->GetResourcePool();
				/*if (!resources.empty()) {
					int tempidx = 0;
					auto item = resources[tempidx];
					resources.erase(resources.begin() + tempidx);
					Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
					spawnPos.y += 3.f;
					item->GetOwner()->m_transform.SetPosition(spawnPos);
					item->GetComponent<RigidBodyComponent>().AddForce({ 0.f, 300.f, 300.f }, EForceMode::IMPULSE);
				}*/
				/*int tempidx = 0;   //리소스풀쓸때
				for (auto& resource : resources)
				{
					auto item = dynamic_cast<EntityItem*>(resource);
					if (item && itemCode == item->itemCode)
					{
						resources.erase(resources.begin() + tempidx);
						Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
						spawnPos.y += 3.f;
						item->GetOwner()->m_transform.SetPosition(spawnPos);
						item->GetComponent<RigidBodyComponent>().AddForce({ 0.f, 30.f, 300.f }, EForceMode::IMPULSE);
						break;
					}
					tempidx++;
				}*/
				Prefab* itemPrefab = nullptr;
				if (itemType == EItemType::Mushroom)
				{
					itemPrefab = PrefabUtilitys->LoadPrefab("BoxMushroom");
				}
				else if (itemType == EItemType::Mineral)
				{
					itemPrefab = PrefabUtilitys->LoadPrefab("BoxMineral");
				}
				else if(itemType == EItemType::Fruit)
				{
					itemPrefab = PrefabUtilitys->LoadPrefab("BoxFruit");
				}
				else if (itemType == EItemType::Flower)
				{
					itemPrefab = PrefabUtilitys->LoadPrefab("BoxFlower"); 
				}
				
				if (itemPrefab)
				{
					GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(itemPrefab, "entityItem");
					Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
					spawnPos.y += 0.1f;
					itemObj->m_transform.SetPosition(spawnPos);
					
					Random<float> randX(-3.0f, 3.0f);   
					Random<float> randY(0.2f, 1.f);         
					Random<float> randZ(-3.0f, 3.0f);

					float randx = randX();
					float randy = randY();
					float randz = randZ();

					Mathf::Vector3 temp = { randx, randy,randz };
					float f = Random<float>(m_minRewardUpForce, m_maxRewardUpForce).Generate();
					auto tween = std::make_shared<Tweener<float>>(
						[=]() { return 0.f; },
						[=](float val) {
							Mathf::Vector3 pos = spawnPos;
							float force = f; // 중력 비슷하게 y축 곡선
							pos.x = Mathf::Lerp(spawnPos.x, spawnPos.x + temp.x, val);
							pos.z = Mathf::Lerp(spawnPos.z, spawnPos.z + temp.z, val);
							pos.y = Mathf::Lerp(spawnPos.y, spawnPos.y + temp.y, val)
								+ force * (1 - (2 * val - 1) * (2 * val - 1));
							itemObj->m_transform.SetPosition(pos);
						},
						1.f,
						.5f,
						[](float t) { return Easing::Linear(t); }
					);

				
					auto GM = GameObject::Find("GameManager");
					if (GM)
					{
						auto tweenManager = GM->GetComponent<TweenManager>();
						if (tweenManager)
						{
							tweenManager->AddTween(tween);
						}
					}


				}

				GetOwner()->Destroy();
				auto pung = GameObject::Find("Pung2");
				if (pung)
				{
					Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
					pung->m_transform.SetPosition(pos);
					pung->GetComponent<EffectComponent>()->Apply();
				}
			}
		}
	}
	else
	{
		LOG("EntityResource Attack sender is nullptr");
	}
}

