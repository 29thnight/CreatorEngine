#include "EntityBigWood.h"
#include "pch.h"
#include "Player.h"
#include "PrefabUtility.h"
#include "RigidBodyComponent.h"
#include "Core.Random.h"
#include "TweenManager.h"
#include "EntityAsis.h"
#include "InputManager.h"
#include "Material.h"
#include "MeshRenderer.h"
#include "PlayEffectAll.h"
#include "CriticalMark.h"
#include "Weapon.h"
#include "GameManager.h"
void EntityBigWood::Start()
{
	m_maxHP = maxHP;
	m_currentHP = m_maxHP;
	HitImpulseStart();
	Prefab* deadPrefab = PrefabUtilitys->LoadPrefab("DestroyEffect");
	if (deadPrefab)
	{
		deadObj = PrefabUtilitys->InstantiatePrefab(deadPrefab, "DeadEffect");
		deadObj->SetEnabled(false);
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

}

void EntityBigWood::OnTriggerEnter(const Collision& collision)
{
	// 아시스가 부딪히면 아시스에게 데미지를 줌.
	EntityAsis* asis = collision.otherObj->GetComponent<EntityAsis>();
	HitAsis(asis);
}

void EntityBigWood::OnCollisionEnter(const Collision& collision)
{
	// 아시스가 부딪히면 아시스에게 데미지를 줌.
	EntityAsis* asis = collision.otherObj->GetComponent<EntityAsis>();
	HitAsis(asis);
}

void EntityBigWood::Update(float tick)
{
	if (InputManagement->IsKeyDown((unsigned int)KeyBoard::M)) {
		SendDamage(nullptr, 10);
	}
	HitImpulseUpdate(tick);
}

void EntityBigWood::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	if (m_currentHP <= 0) return; // 풀링해서 사용하기 위해 만약 이미 파괴된 상태라면 무시.

	Entity::SendDamage(sender, damage, hitinfo);
	// 플레이어가 공격한 경우에만 처리.
	m_currentHP -= damage; 
	Player* player = dynamic_cast<Player*>(sender);
	if (player)
	{
		if (m_criticalMark)
		{
			if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
			{
				damage *= player->m_curWeapon->coopCrit;
				hitinfo.isCritical = true;
				//데미지2배및 hitEffect 크리티컬 이펙트로 출력 몬스터,리소스 동일
			}
		}
	}
	PlayHitEffect(this->GetOwner(), hitinfo);
	if (m_currentHP <= 0)
	{
		// 파괴 처리.
		m_currentHP = 0;
		// 재화 오브젝트 생성 후 렌덤위치로.

		/*if (m_onDeathEvent.IsBound())
		{
			m_onDeathEvent.Invoke();
		}
		player->AddSpiritStone(m_stoneReward);*/

		int index = 0;
		int maxCount = type1Count + type2Count + type3Count;

		Prefab* itemPrefab;
		int itemCount = type1Count;
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxFruit");
		while (itemCount > 0) {
			itemCount--;
			if (itemPrefab)
			{
				GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(itemPrefab, "entityItem");
				Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
				spawnPos.y += 0.1f;
				Mathf::Vector3 temp = UniformRandomUpdirection(m_rewardUpAngle, index++, maxCount) * m_rewardRandomRange;
				temp.y = 0;
				//spawnPos += temp;
				itemObj->m_transform.SetPosition(spawnPos);

				float f = Random<float>(m_minRewardUpForce, m_maxRewardUpForce).Generate();
				auto tween = std::make_shared<Tweener<float>>(
					[=]() {return 0.f;},
					[=](float val) {
						Mathf::Vector3 pos = spawnPos;
						float force = f;
						pos.x = Mathf::Lerp(spawnPos.x, spawnPos.x + temp.x, val);
						pos.z = Mathf::Lerp(spawnPos.z, spawnPos.z + temp.z, val);
						pos.y = Mathf::Lerp(spawnPos.y, spawnPos.y + temp.y, val) + force * (1 - (2 * val - 1) * (2 * val - 1));
						itemObj->m_transform.SetPosition(pos);
					},
					1.f,
					.5f,
					[](float t) {return Easing::Linear(t);}
				);

				GameObject::Find("GameManager")->GetComponent<TweenManager>()->AddTween(tween); 
			}
		}
		itemCount = type2Count;
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxMushroom");
		while (itemCount > 0) {
			itemCount--;
			if (itemPrefab)
			{
				GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(itemPrefab, "entityItem");
				Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
				spawnPos.y += 0.1f;
				Mathf::Vector3 temp = UniformRandomUpdirection(m_rewardUpAngle, index++, maxCount) * m_rewardRandomRange;
				temp.y = 0;
				//spawnPos += temp;
				itemObj->m_transform.SetPosition(spawnPos);

				float f = Random<float>(m_minRewardUpForce, m_maxRewardUpForce).Generate();
				auto tween = std::make_shared<Tweener<float>>(
					[=]() {return 0.f;},
					[=](float val) {
						Mathf::Vector3 pos = spawnPos;
						float force = f;
						pos.x = Mathf::Lerp(spawnPos.x, spawnPos.x + temp.x, val);
						pos.z = Mathf::Lerp(spawnPos.z, spawnPos.z + temp.z, val);
						pos.y = Mathf::Lerp(spawnPos.y, spawnPos.y + temp.y, val) + force * (1 - (2 * val - 1) * (2 * val - 1));
						itemObj->m_transform.SetPosition(pos);
					},
					1.f,
					.5f,
					[](float t) {return Easing::Linear(t);}
				);

				GameObject::Find("GameManager")->GetComponent<TweenManager>()->AddTween(tween);
			}
		}
		itemCount = type3Count;
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxMineral");
		while (itemCount > 0) {
			itemCount--;
			if (itemPrefab)
			{
				GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(itemPrefab, "entityItem");
				Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
				spawnPos.y += 0.1f;
				Mathf::Vector3 temp = UniformRandomUpdirection(m_rewardUpAngle, index++, maxCount) * m_rewardRandomRange;
				temp.y = 0;
				//spawnPos += temp;
				itemObj->m_transform.SetPosition(spawnPos);

				float f = Random<float>(m_minRewardUpForce, m_maxRewardUpForce).Generate();
				auto tween = std::make_shared<Tweener<float>>(
					[=]() {return 0.f;},
					[=](float val) {
						Mathf::Vector3 pos = spawnPos;
						float force = f;
						pos.x = Mathf::Lerp(spawnPos.x, spawnPos.x + temp.x, val);
						pos.z = Mathf::Lerp(spawnPos.z, spawnPos.z + temp.z, val);
						pos.y = Mathf::Lerp(spawnPos.y, spawnPos.y + temp.y, val) + force * (1 - (2 * val - 1) * (2 * val - 1));
						itemObj->m_transform.SetPosition(pos);
					},
					1.f,
					.5f,
					[](float t) {return Easing::Linear(t);}
				);

				GameObject::Find("GameManager")->GetComponent<TweenManager>()->AddTween(tween);
			}
		}

		SetAlive(false);
		if (deadObj)
		{
			deadObj->SetEnabled(true);
			auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
			Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
			deadPos.y += 0.0f;
			deadObj->GetComponent<Transform>()->SetPosition(deadPos);
			deadEffect->Initialize();
		}

		GameObject* GMObj = GameObject::Find("GameManager");
		if (GMObj)
		{
			GameManager* GM = GMObj->GetComponent<GameManager>();
			if (GM)
			{
				GM->AddReward(reward);
			}
		}


		GetOwner()->Destroy();
		
	}
	else
	{
		HitImpulse();
		// 데미지를 받았지만 아직 살아있는 경우.
		/*if (m_onDamageEvent.IsBound())
		{
			m_onDamageEvent.Invoke();
		}*/
	}
}

void EntityBigWood::HitAsis(EntityAsis* asis)
{
	if (asis)
	{
		SendDamage(asis, 10000);
		asis->SendDamage(this, m_logDamage);
	}
}

double EntityBigWood::radical_inverse_base2(unsigned int n)
{
	double inv = 0.0;
	double f = 0.5;
	while (n > 0) {
		inv += f * (n % 2);
		n /= 2;
		f *= 0.5;
	}
	return inv;
}

Mathf::Vector3 EntityBigWood::UniformRandomUpdirection(float angle, int curindex, int maxCount)
{
	double cone_angle_rad = (angle / 2.0) * (Mathf::pi / 180.0);
	double cos_cone_angle = cos(cone_angle_rad);

	double u = static_cast<double>(curindex) / maxCount;
	double v = radical_inverse_base2(curindex);

	double phi = u * 2.0 * Mathf::pi;

	double cos_theta = 1.0 - v * (1.0 - cos_cone_angle);
	double sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	Mathf::Vector3 dir;
	dir = {
		(float)(sin_theta * cos(phi)),
		(float)cos_theta,
		(float)(sin_theta * sin(phi))
	};

	return dir;
}

