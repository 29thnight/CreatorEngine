#include "MonEleteProjetile.h"
#include "pch.h"
#include "Entity.h"
#include "Camera.h"

void MonEleteProjetile::Start()
{
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
	m_Damege = damage;
	m_Speed = speed;
	m_Dir = dir;
	isInitialize = true;
}

void MonEleteProjetile::Action(GameObject* target)
{
	if (!isInitialize) return;

	//�����浹 ���Ѵٰ� ���������� 1ȸ������ ��������
	if (isTrigger) return;

	Entity* targetEntity = target->GetComponentDynamicCast<Entity>();
	targetEntity->SendDamage(ownerEntity, m_Damege);

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
}

