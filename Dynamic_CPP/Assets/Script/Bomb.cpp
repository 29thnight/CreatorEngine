#include "Bomb.h"
#include "pch.h"
#include "DebugLog.h"
void Bomb::Start()
{
	
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
		//���� �����ϸ� Explosion �����ϰ� �ڱ��ڽ��� ���̱�?
	}

}

void Bomb::OnTriggerEnter(const Collision& collision)
{
	//���� �ε����� �ݴ�� ƨ���
	if (collision.otherObj->m_tag == "Wall")
	{
		LOG("bomb trigger wall");
	}
}


void Bomb::ThrowBomb(Player* _owner,Mathf::Vector3 _startPos, Mathf::Vector3 _targetPos)
{
	isThrow = true;
	m_ownerPlayer = _owner;
	m_startPos = _startPos;
	m_targetPos = _targetPos;
}


