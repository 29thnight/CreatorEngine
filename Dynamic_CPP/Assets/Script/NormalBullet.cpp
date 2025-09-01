#include "NormalBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
void NormalBullet::Start()
{
}


void NormalBullet::Update(float tick)
{
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->AddPosition(m_moveDir * rangedProjSpd);

	lifeTime -= tick;
	if (lifeTime <= 0)
	{
		GetOwner()->Destroy();
	}
}


void NormalBullet::OnTriggerEnter(const Collision& collision)
{

	//�ϳ��� ������ ���� 
	if (hasAttacked == false)
	{
		if (collision.otherObj->m_tag == "Enemy")
		{
			EntityEnemy* enemy = collision.otherObj->GetComponent<EntityEnemy>();

			std::cout << "EnemyHit!" << std::endl;

			hasAttacked = true;
			GetOwner()->Destroy(); //���������� BulletPool�����

		}
	}
}
