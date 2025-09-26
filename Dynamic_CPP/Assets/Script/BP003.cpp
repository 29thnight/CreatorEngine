#include "BP003.h"
#include "Entity.h"
#include "pch.h"
void BP003::Start()
{
}

void BP003::Update(float tick)
{
	if (!isInitialize || !isAttackStart) {
		return;
	}

	m_timer += tick;
	//���� ������ ���ϸ��̼� ȿ�� ��� �Ѵٰ� �ϸ� ����ٰ�


	if (m_timer > m_delay) {
		Explosion(); 
	}

	if (ownerDestory) {
		GetOwner()->SetEnabled(false); //���� ������ �������
		GetOwner()->Destroy(); //���� ��ũ ����
	}
}

void BP003::Initialize(Entity* owner, int damage, float radius, float delay)
{
	m_ownerEntity = owner;
	m_damage = damage;
	m_radius = radius;
	m_delay = delay;
	isInitialize = true;
}

void BP003::Explosion()
{
	//���� 
	//overlap ����
	Mathf::Vector3 pos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

	OverlapInput input;
	input.position = pos;
	input.rotation = Mathf::Quaternion::Identity;
	//input.layerMask = ~0;

	std::vector<HitResult> res;

	//�̶� �߰� ����Ʈ�� �ε������� �ִ��� ������

	PhysicsManagers->SphereOverlap(input, m_radius, res);

	//���� ������ ������ ó��
	for (auto& hit : res) {
		//tag �Ǵ�? ���̾� �Ǵ�? �̸����� ã����? ���Ѵٸ� �ٲ㵵 ���� �ƴ� ���� �ʿ��ϴٸ� init�� ���̾� ����ũ���� �ѱ����
		//�÷��̾ ã�� �ƽý� �ȵ��´ٸ�
		if (hit.gameObject->m_tag == "Player") {
			Entity* objEntity = hit.gameObject->GetComponentDynamicCast<Entity>();
			objEntity->SendDamage(m_ownerEntity, m_damage);
		}
	}

	//
	GetOwner()->SetEnabled(false);

}

