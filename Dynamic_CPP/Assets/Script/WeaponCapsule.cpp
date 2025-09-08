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
				if (weaponCode == 0)  //������ meleeweapon��������  weaponcode Ȯ���ؼ� melee,range,bomb ���������ؼ� add
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
	//throw�� �̵����� weaponĸ���� �ű��
	OwnerPlayerIndex = _player->playerIndex;
	ownerPlayer = _player->GetOwner();
	startPos = statrPos;
	timer = 0.f;
}

