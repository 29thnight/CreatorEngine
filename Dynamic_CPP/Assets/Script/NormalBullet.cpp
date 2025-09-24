#include "NormalBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "Player.h"
#include "DebugLog.h"
#include "Entity.h"
void NormalBullet::Start()
{
	__super::Start();
	bulletType = BulletType::Normal;
}


void NormalBullet::Update(float tick)
{
	__super::Update(tick);
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->AddPosition(m_moveDir * rangedProjSpd);

	lifeTime -= tick;
	if (lifeTime <= 0)
	{
		//GetOwner()->Destroy();
	}
}


void NormalBullet::OnTriggerEnter(const Collision& collision)
{

	//�ϳ��� ������ ���� 
	if (hasAttacked == false)
	{
		//if (collision.otherObj->m_tag == "Enemy")
		{
			Entity* enemy = collision.otherObj->GetComponentDynamicCast<Entity>();

			if (enemy)
			{
				LOG("EnemyHit!");
				enemy->SendDamage(m_ownerPlayer, m_damage);
				hasAttacked = true;
				GetOwner()->Destroy(); //���������� BulletPool�����

			}
		}
	}
}
