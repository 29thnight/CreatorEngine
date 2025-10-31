#include "MonEleteProjetile.h"
#include "pch.h"
#include "Entity.h"
#include "Camera.h"
#include "EffectComponent.h"
#include "Core.Minimal.h"
#include "PlayEffectAll.h"`
#include "PrefabUtility.h"
#include "SphereColliderComponent.h"
#include "EntityEleteMonster.h"
#include "GameManager.h"
#include "SFXPoolManager.h"
void MonEleteProjetile::Start()
{
	if (nullptr == m_effect)
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			m_effect = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			break;
		}
	}
}


void MonEleteProjetile::OnTriggerEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Action(collision.otherObj);
	}
	
	//TODO : 
	//�����̳� ������ �浹�Ǿ����� ������� ������ �ʿ��Ѱ�?
}

void MonEleteProjetile::OnCollisionEnter(const Collision& collision)
{
	//�ٸ� ������Ʈ �浹�� �������� trigger ��ü�� �Ҳ� ������ �ϴ� �̰͵� ������ 
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Action(collision.otherObj);
	}
	//TODO : 
	//�����̳� ������ �浹�Ǿ����� ������� ������ �ʿ��Ѱ�?
}

void MonEleteProjetile::Update(float tick)
{
	auto camera = CameraManagement->GetLastCamera().get();

	//�ʱ�ȭ ��Ű�� �̵� ��� ������ �ʱ�ȭ�� ����� �ӷ����� �̵�
	if (isInitialize && m_isMoving) {
		Mathf::Vector3 v = m_Dir * m_Speed * tick;
		m_pTransform->AddPosition(v);
	}

	auto camViewProj = camera->CalculateView() * camera->CalculateProjection();
	auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	float w = XMVectorGetW(clipSpacePos);
	if (w < 0.001f) {
		//ī�޶� ����
		//����� ���� ���� �߸� �Ȱ���
		RevertPool();
	}

	XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	float x = XMVectorGetX(ndcPos);
	float y = XMVectorGetY(ndcPos);
	x = abs(x);
	y = abs(y);

	
	if (x > 1 || y>1)
	{
		//ī�޶� x�� Ȥ�� y�� ��
		//����� ���� �浹 ���� ī�޶� ������ �������
		RevertPool();
	}
}

void MonEleteProjetile::Initallize(Entity* owner, int damage, float speed, Mathf::Vector3 dir)
{
	ownerEntity = owner;
	EntityEleteMonster* elete = dynamic_cast<EntityEleteMonster*>(owner);
	if (elete)
	{
		KnockbackDistacneX = elete->KnockbackDistacneX;
		KnockbackDistacneY = elete->KnockbackDistacneY;
		KnockbackTime = elete->KnockbackTime;
	}
	m_Damege = damage;
	m_Speed = speed;
	m_Dir = dir;
	isInitialize = true;

	if (nullptr == m_effect)
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			m_effect = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			break;
		}
	}
	Transform* transform = GetOwner()->GetComponent<Transform>();
	float yaw = atan2(m_Dir.x, m_Dir.z);

	Mathf::Quaternion dirRot = Mathf::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
	transform->SetRotation(dirRot);
	if (m_effect)
	{
		m_effect->Apply();
	}

}

void MonEleteProjetile::Action(GameObject* target)
{
	if (!isInitialize) return;

	//�����浹 ���Ѵٰ� ���������� 1ȸ������ ��������
	if (isTrigger) return;

	HitInfo hitInfo;
	hitInfo.attakerPos = GetOwner()->m_transform.GetWorldPosition();
	auto collider = GetComponent<SphereColliderComponent>();
	float myRadius = collider->GetRadius();
	Mathf::Vector3 mypos = GetOwner()->m_transform.GetWorldPosition();
	Mathf::Vector3 otherpos = target->m_transform.GetWorldPosition();
	Mathf::Vector3 dir = otherpos - mypos;
	dir.Normalize();
	Mathf::Vector3 contactPoint = mypos + dir * myRadius;
	hitInfo.hitPos = contactPoint;
	Entity* targetEntity = target->GetComponentDynamicCast<Entity>();
	hitInfo.KnockbackForce = { KnockbackDistacneX ,KnockbackDistacneY};
	hitInfo.KnockbackTime = KnockbackTime;
	targetEntity->SendDamage(ownerEntity, m_Damege, hitInfo);


	auto GMobj = GameObject::Find("GameManager");
	if (GMobj)
	{
		GameManager* GM = GMobj->GetComponent<GameManager>();
		if (GM)
		{
			auto pool = GM->GetSFXPool();
			if (pool)
			{
				pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("Mon003AttackExplosion"));
			}
		}
	}


	Prefab* hitPrefab = PrefabUtilitys->LoadPrefab("EliteAtkHitEffect");
	if (hitPrefab)
	{
		GameObject* hitObj = PrefabUtilitys->InstantiatePrefab(hitPrefab, "monhitEffect");
		auto hitEffect = hitObj->GetComponent<PlayEffectAll>();
		hitObj->GetComponent<Transform>()->SetPosition(contactPoint);
		hitEffect->Initialize();
	}





	//todo : ���⼭ ���� �հ� ������ ��ȭ��ȭ �Ұ���? �ϴ��� ��ȸ������ ������� ����
	RevertPool();
}

void MonEleteProjetile::RevertPool()
{
	//���ư��� ��ü�� �ൿ�� ���߰� ��Ȱ��ȭ �Ǿ� Ǯ�� ���ư��� ����
	if (OwnerDestroy)
	{
		GetOwner()->Destroy();
	}
	m_isMoving = false;
	GetOwner()->SetEnabled(false);
	if (m_effect)
	{
		m_effect->StopEffect();
	}
}

