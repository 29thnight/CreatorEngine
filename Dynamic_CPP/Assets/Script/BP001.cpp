#include "BP001.h"
#include "pch.h"
#include "Entity.h"
#include "PrefabUtility.h"
#include "EffectComponent.h"
#include "GameManager.h"
#include "SFXPoolManager.h"
void BP001::Start()
{
}

void BP001::OnTriggerEnter(const Collision& collision)
{
	//todo : �� �浹 ������ �ؼ� ��Ʈ���� �ؿ�
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Explosion();
	}
}

void BP001::OnCollisionEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Explosion();
	}
}

void BP001::Update(float tick)
{
	if (!isInitialize || !isAttackStart) {
		return;
	}

	m_timer += tick;

	//�̵�
	Transform* tr = GetOwner()->GetComponent<Transform>();
	Mathf::Vector3 pos = direction * m_speed * tick;
	tr->AddPosition(pos);

	//�ð� �Ǹ� ����
	if (m_timer > m_delay) { 
		Explosion();
	}
}

void BP001::Initialize(Entity* owner, Mathf::Vector3 pos, Mathf::Vector3 dir, int damage, float radius, float delay, float speed)
{
	m_ownerEntity = owner;
	GetOwner()->GetComponent<Transform>()->SetWorldPosition(pos);
	direction = dir;
	m_damage = damage;
	m_radius = radius;
	m_delay = delay;
	m_speed = speed;
	isExplode = false; //���� ���� �ʱ�ȭ
	GetOwner()->GetComponent<EffectComponent>()->Apply();
	isInitialize = true;
}

void BP001::Explosion()
{ 
	if (isExplode) return; //���� ��ȸ��

	//overlap ����
	Mathf::Vector3 pos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

	OverlapInput input;
	input.position = pos;
	input.rotation = Mathf::Quaternion::Identity;
	input.layerMask = 1 << 5; // �÷��̾� ���̾ �˻�

	std::vector<HitResult> res;

	//�̶� �߰� ����Ʈ�� �ε������� �ִ��� ������
	Prefab* ExplosionEff = nullptr;
	ExplosionEff = PrefabUtilitys->LoadPrefab("BossExplosion");
	GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(ExplosionEff, "entityItem");
	itemObj->GetComponent<Transform>()->SetWorldPosition(pos);
	itemObj->GetComponent<EffectComponent>()->Apply();
	GetOwner()->GetComponent<EffectComponent>()->StopEffect();
	PhysicsManagers->SphereOverlap(input, m_radius, res);

	auto GMobj = GameObject::Find("GameManager");
	if (GMobj)
	{
		GameManager* GM = GMobj->GetComponent<GameManager>();
		if (GM)
		{
			auto pool = GM->GetSFXPool();
			if (pool)
			{
				pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("BossFireAttackExplosion"));
			}
		}
	}

	//���� ������ ������ ó��
	for (auto& hit : res) {
		//tag �Ǵ�? ���̾� �Ǵ�? �̸����� ã����? ���Ѵٸ� �ٲ㵵 ���� �ƴ� ���� �ʿ��ϴٸ� init�� ���̾� ����ũ���� �ѱ����
		//�÷��̾ ã�� �ƽý� �ȵ��´ٸ�
		
		Entity* objEntity = hit.gameObject->GetComponentDynamicCast<Entity>();
		objEntity->SendDamage(m_ownerEntity, m_damage);
		
	}
	//

	isExplode = true;
	m_timer = 0.0f;
	GetOwner()->SetEnabled(false);
}

