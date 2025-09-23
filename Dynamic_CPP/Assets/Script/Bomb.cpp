#include "Bomb.h"
#include "pch.h"
#include "DebugLog.h"
#include "PrefabUtility.h"
#include "Explosion.h"
#include "Entity.h"
#include "Player.h"
void Bomb::Start()
{
	
}


void Bomb::Update(float tick)
{
	if (isThrow || isBound)
	{
		float t = elapsedTime / duration;
		Transform* transform = GetOwner()->GetComponent<Transform>();
		elapsedTime += tick;
		if (t > 1.f) t = 1.0f;
		Mathf::Vector3 pos = Mathf::Lerp(m_startPos, m_targetPos, t);
		if (isThrow)
		{
			float throwPowerY = m_throwPowerY;
			pos.y += throwPowerY * 4.0f * t * (1 - t);

			transform->SetPosition(pos);
		}
		else if (isBound)
		{
			elapsedTime += tick;
			float boundPowerY = m_boundPowerY;
			// 반사 후에도 그냥 작은 포물선
			pos.y += boundPowerY * 4.0f * t * (1 - t);

			transform->SetPosition(pos);

		}
		//땅에 도착하면 Explosion 생성하고 자기자신은 죽이기?
		if (t >= 1.0f) 
		{
			transform->SetPosition(m_targetPos);

			float explosionRadius = 1;
			Prefab* ExplosionPrefab = PrefabUtilitys->LoadPrefab("Explosion");
			if (ExplosionPrefab)
			{
				GameObject* ExplosionObj = PrefabUtilitys->InstantiatePrefab(ExplosionPrefab, "Explosion");
				auto explosion = ExplosionObj->GetComponent<Explosion>();
				ExplosionObj->GetComponent<Transform>()->SetPosition(m_targetPos);
				explosionRadius = explosion->explosionRadius;
				explosion->Initialize(m_ownerPlayer);
			}


			std::vector<HitResult> hits;
			OverlapInput explosionInfo;
			explosionInfo.layerMask = 1 << 0 | 1 << 8 | 1 << 10;
			explosionInfo.position = transform->GetWorldPosition();
			PhysicsManagers->SphereOverlap(explosionInfo, explosionRadius, hits);


 			for (auto& hit : hits)
			{
				auto object = hit.gameObject;
				if (object == GetOwner()) continue;

				auto enemy = object->GetComponentDynamicCast<Entity>();
				if (enemy)
				{
					enemy->SendDamage(m_ownerPlayer, m_damage);
				}
			}


			GetOwner()->Destroy(); //&&&&& 풀에넣기
		}
	}

}

void Bomb::OnTriggerEnter(const Collision& collision)
{
	//벽에 부딪히면 반대로 튕기기
	if (collision.otherObj->m_tag == "Wall")
	{

		Mathf::Vector3 curPos = GetOwner()->m_transform.GetWorldPosition();
		Mathf::Vector3 dir = m_targetPos - curPos;

		float boundPower = 2.0f;
		dir.Normalize();
		dir = -dir;
		Mathf::Vector3 boundTargetPos = boundPower * dir;
		boundTargetPos.y = 0.f;

		BoundBomb(boundTargetPos);
		LOG("bomb trigger wall");
	}
}


void Bomb::ThrowBomb(Player* _owner,Mathf::Vector3 _startPos, Mathf::Vector3 _targetPos, float _damage)
{
	isThrow = true;
	m_ownerPlayer = _owner;
	m_startPos = _startPos;
	m_targetPos = _targetPos;
	m_damage = _damage;
	m_throwPowerY = 4.0f;
}

void Bomb::BoundBomb(Mathf::Vector3 _targetPos)
{
	m_startPos = GetOwner()->m_transform.GetWorldPosition();
	m_targetPos = _targetPos;
	elapsedTime = 0.f;
	isBound = true;
	isThrow = false;
}



