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
#include "Weapon.h"
#include "PlayEffectAll.h"
#include "EventTarget.h"
void EntityResource::Start()
{

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


	if (m_criticalMark == nullptr)
	{
		Prefab* criticalMarkPre = PrefabUtilitys->LoadPrefab("CriticalMark");
		{
			if (criticalMarkPre)
			{
				auto Obj = PrefabUtilitys->InstantiatePrefab(criticalMarkPre, "criticalMark");
				m_criticalMark = Obj->GetComponent<CriticalMark>();
				GetOwner()->AddChild(Obj);
			}
		}
	}



	m_currentHP = maxHP;
	HitImpulseStart();
	auto newEffect = SceneManagers->GetActiveScene()->CreateGameObject("effect", GameObjectType::Empty, GetOwner()->m_index);
	m_effect = newEffect->AddComponent<EffectComponent>();
	m_effect->m_effectTemplateName = "resourceView";
	m_effect->Apply();


	Prefab* deadPrefab = PrefabUtilitys->LoadPrefab("EnemyDeathEffect");
	if (deadPrefab)
	{
		deadObj = PrefabUtilitys->InstantiatePrefab(deadPrefab, "DeadEffect");
		deadObj->SetEnabled(false);
		deadObj->m_transform.SetScale({ 0.5,0.5,0.5 });
	}
}

void EntityResource::Update(float tick)
{
	HitImpulseUpdate(tick);
	UpdateOutLine(tick);
}
void EntityResource::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	//LOG("EntityResource Attack");
	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		if (player)
		{
			Entity::SendDamage(sender, damage, hitinfo);
			// hit
			if (m_criticalMark)
			{
				if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
				{
					damage *= player->m_curWeapon->coopCrit;
					hitinfo.isCritical = true;
					//데미지2배및 hitEffect 크리티컬 이펙트로 출력 몬스터,리소스 동일
				}
			}
			m_currentHP -= std::max(damage, 0);
			PlayHitEffect(this->GetOwner(), hitinfo);
			if (m_currentHP <= 0) {
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

				if(deadObj)
				{
					deadObj->SetEnabled(true);
					auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
					Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
					deadPos.y += 0.0f;
					deadObj->GetComponent<Transform>()->SetPosition(deadPos);
					deadEffect->Initialize();
				}
				GetOwner()->Destroy();
				/*auto pung = GameObject::Find("Pung2");
				if (pung)
				{
					Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
					pung->m_transform.SetPosition(pos);
					pung->GetComponent<EffectComponent>()->Apply();
				}*/
			}
			else {
				HitImpulse();

			}
		}
	}
	else
	{
		LOG("EntityResource Attack sender is nullptr");
	}
}

