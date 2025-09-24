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
			else //타겟지점도착 같은타입 weapon 생성해서 player에게 주기 //
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
					bool Success = player->AddWeapon(weapon); //AddWeapon 실패시 그냥 바닥에 둥둥 떠있기   and 둥둥떠있는중 플레이어가 집으면 addweapon 다시시도
					if (Success)
					{
						GetOwner()->Destroy();
					}
					else
					{
						ownerPlayer = nullptr;
						//바닥에 둥둥
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
	//throw나 이동등은 weapon캡슐로 옮기기
	OwnerPlayerIndex = _player->playerIndex;
	ownerPlayer = _player->GetOwner();
	startPos = statrPos;
	timer = 0.f;
}

