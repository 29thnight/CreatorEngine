#include "SpecialBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
void SpecialBullet::Start()
{
	bulletType = BulletType::Special;
}

void SpecialBullet::OnTriggerEnter(const Collision& collision)
{
	//피격된 지점 중심으로 범위 공격 //새 오브젝트 만들어서 걔가 처리
	if (hasAttacked == false)
	{
		if (collision.otherObj->m_tag == "Enemy")
		{
			EntityEnemy* enemy = collision.otherObj->GetComponent<EntityEnemy>();

			std::cout << "EnemyHit!" << std::endl;

			hasAttacked = true;
			GetOwner()->Destroy(); //지우지말고 BulletPool만들기

			//범위공격 오브젝트 생성
		}
	}
}

void SpecialBullet::OnTriggerStay(const Collision& collision)
{
}

void SpecialBullet::Update(float tick)
{
}

