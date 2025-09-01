#include "SpecialBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
void SpecialBullet::Start()
{
	bulletType = BulletType::Special;
}

void SpecialBullet::OnTriggerEnter(const Collision& collision)
{
	//�ǰݵ� ���� �߽����� ���� ���� //�� ������Ʈ ���� �°� ó��
	if (hasAttacked == false)
	{
		if (collision.otherObj->m_tag == "Enemy")
		{
			EntityEnemy* enemy = collision.otherObj->GetComponent<EntityEnemy>();

			std::cout << "EnemyHit!" << std::endl;

			hasAttacked = true;
			GetOwner()->Destroy(); //���������� BulletPool�����

			//�������� ������Ʈ ����
		}
	}
}

void SpecialBullet::OnTriggerStay(const Collision& collision)
{
}

void SpecialBullet::Update(float tick)
{
}

