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
				if (weaponCode == 0)  //지금은 meleeweapon만이지만  weaponcode 확인해서 melee,range,bomb 개별생성해서 add
				{

				}
				//Prefab* meleeweapon = PrefabUtilitys->LoadPrefab("MeleeWeapon");
				Prefab* meleeweapon = PrefabUtilitys->LoadPrefab("Staff");
				if (meleeweapon && ownerPlayer)
				{
					GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(meleeweapon, "meleeWeapon");
					auto weapon = weaponObj->GetComponent<Weapon>();
					Player* player= ownerPlayer->GetComponent<Player>();
					player->AddWeapon(weapon); 

					
				}


				GetOwner()->Destroy();
			}
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

