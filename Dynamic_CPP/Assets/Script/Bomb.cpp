#include "Bomb.h"
#include "pch.h"
void Bomb::Start()
{
	itemType = ItemType::Bomb;
}


void Bomb::Update(float tick)
{
	if (isThrow)
	{
		Transform* transform = GetOwner()->GetComponent<Transform>();
		elapsedTime += tick;
		float t = elapsedTime / duration;
		if (t > 1.f) t = 1.0f;

		Mathf::Vector3 pos = Mathf::Lerp(m_startPos, m_targetPos, t);
		pos.y += throwPowerY * 4.0f * t * (1 - t);

		transform->SetPosition(pos);
		//���� �����ϸ� BobmAttack �����ϰ� �ڱ��ڽ��� ���̱�?
	}

}

void Bomb::OnTriggerEnter(const Collision& collision)
{
	//���� �ε����� �ݴ�� ƨ���
	if (collision.otherObj->m_tag == "Wall")
	{
		std::cout << "bomb trigger wall" << std::endl;
	}
}

void Bomb::Attack(Player* _Owner, AttackContext _attackContext)
{
	isThrow = true;
	m_ownerPlayer = _Owner;
	m_startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_targetPos = _attackContext.targetPosition;
}
void Bomb::ThrowBomb(Player* _owner, Mathf::Vector3 _targetPos)
{
	isThrow = true;
	m_ownerPlayer = _owner;
	m_startPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	m_targetPos = _targetPos;
}


