#include "Bomb.h"
#include "pch.h"
#include "DebugLog.h"
#include "PrefabUtility.h"
#include "Explosion.h"
#include "Entity.h"
#include "Player.h"
#include "GameManager.h"
#include "SFXPoolManager.h"
#include "Core.Random.h"
#include "SoundName.h"

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
			// �ݻ� �Ŀ��� �׳� ���� ������
			pos.y += boundPowerY * 4.0f * t * (1 - t);

			transform->SetPosition(pos);

		}
		//���� �����ϸ� Explosion �����ϰ� �ڱ��ڽ��� ���̱�?
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
				explosionRadius = radius;
				explosion->Initialize(m_ownerPlayer);
			}


			std::vector<HitResult> hits;
			OverlapInput explosionInfo;
			explosionInfo.layerMask = 1 << 0 | 1 << 8 | 1 << 10;
			explosionInfo.position = transform->GetWorldPosition();
			PhysicsManagers->SphereOverlap(explosionInfo, explosionRadius, hits);


			auto GMobj = GameObject::Find("GameManager");
			if (GMobj)
			{
				GameManager* GM = GMobj->GetComponent<GameManager>();
				if (GM)
				{
					auto pool = GM->GetSFXPool();
					if (pool)
					{
						int rand = Random<int>(0, ExplosionSounds.size() - 1).Generate();
						pool->PlayOneShot(ExplosionSounds[rand]);
					}
				}
			}

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


			GetOwner()->Destroy(); //&&&&& Ǯ���ֱ�
		}
	}

}

void Bomb::OnTriggerEnter(const Collision& collision)
{
	//���� �ε����� �ݴ�� ƨ���
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


void Bomb::ThrowBomb(Player* _owner,Mathf::Vector3 _startPos, Mathf::Vector3 _targetPos,float bombThrowDuration, float _radius, float _damage)
{
	isThrow = true;
	m_ownerPlayer = _owner;
	m_startPos = _startPos;
	m_targetPos = _targetPos;
	m_damage = _damage;
	duration = bombThrowDuration;
	radius = _radius;
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



