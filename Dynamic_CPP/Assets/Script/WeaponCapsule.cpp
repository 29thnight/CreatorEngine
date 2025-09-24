#include "WeaponCapsule.h"
#include "pch.h"
#include "Player.h"
#include "Weapon.h"
#include "PrefabUtility.h"
using namespace Mathf;
void WeaponCapsule::Start()
{
}

void WeaponCapsule::Update(float tick)
{
	if (ownerPlayer)
	{
		Transform* targetTransform = ownerPlayer->GetComponent<Transform>();
		if (targetTransform)
		{
			speed -= tick;
			if (speed < 1.f)
			{
				speed = 1.f;
			}
			Transform* myTr = GetOwner()->GetComponent<Transform>();
			Mathf::Vector3 tailPos = targetTransform->GetWorldPosition();
			Vector3 pB = ((tailPos - startPos) / 2) + startPos;
			pB.y += 10.f;
			Vector3 pA = startPos;
			timer += tick * speed;
			if (timer < 1.f) {
				Vector3 p0 = Lerp(pA, pB, timer);
				Vector3 p1 = Lerp(pB, tailPos, timer);
				Vector3 p01 = Lerp(p0, p1, timer);
				myTr->SetPosition(p01);
			}
			else //Ÿ���������� ����Ÿ�� weapon �����ؼ� player���� �ֱ� //
			{
				Prefab* weaponPrefab;
				if (weaponCode == 1)  
				{
					weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponMelee");
				}
				else if (weaponCode == 2)  
				{
					weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponWand");
				}
				else if (weaponCode == 3)  
				{
					weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponBomb");
				}
				else
				{
					weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponBasic");
				}
				
				if (weaponPrefab && ownerPlayer)
				{
					GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(weaponPrefab, "Weapon");
					auto weapon = weaponObj->GetComponent<Weapon>();
					Player* player= ownerPlayer->GetComponent<Player>();
					bool Success = player->AddWeapon(weapon); //AddWeapon ���н� �׳� �ٴڿ� �յ� ���ֱ�   and �յն��ִ��� �÷��̾ ������ addweapon �ٽýõ�
					if (Success)
					{
						GetOwner()->Destroy();
					}
					else
					{
						ownerPlayer = nullptr;
						//�ٴڿ� �յ�
					}

				}
			}
		}
	}
	else
	{
		Transform* transform = GetOwner()->GetComponent<Transform>();

		float delta = (goingUp ? 1 : -1) * boundSpeed * tick;
		transform->AddPosition({ 0,delta,0 });
		yBoundpos += delta;
		if (yBoundpos > boundingRange)
		{
			yBoundpos = boundingRange;
			goingUp = false;
		}
		else if (yBoundpos < 0.0f)
		{
			yBoundpos = 0.0f;
			goingUp = true;
		}
		
	}
}

void WeaponCapsule::Throw(Player* _player, Mathf::Vector3 statrPos)
{
	//throw�� �̵����� weaponĸ���� �ű��
	OwnerPlayerIndex = _player->playerIndex;
	ownerPlayer = _player->GetOwner();
	startPos = statrPos;
	timer = 0.f;
}

