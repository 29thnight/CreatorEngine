#include "Weapon.h"
#include "pch.h"
#include "Player.h"
#include "Transform.h"
using namespace Mathf;
void Weapon::Start()
{
	if (itemtype == 0)
	{
		itemType = ItemType::Meely;
	}
	else if(itemtype == 1)
	{
		itemType = ItemType::Range;
	}
	else if (itemtype == 2)
	{
		itemType = ItemType::Bomb;
	}
	else 
	{
		itemType = ItemType::Basic;
	}
	isBreak = false;
}


void Weapon::Update(float tick)
{


	//날아가는 로직은 weaponCapsule로 이전예정
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
		}
	}
}

void Weapon::SetEnabled(bool able)
{
	GetOwner()->SetEnabled(able);
	for (auto child : GetOwner()->m_childrenIndices)
	{
		auto childobj = GameObject::FindIndex(child);
		childobj->SetEnabled(able);
	}
}





bool Weapon::CheckDur()
{
	if (itemType == ItemType::Basic) return false; 


	curDur -= durUseAtk;

	if (curDur <= 0)
	{
		isBreak = true;
		return true;
	}
	return false;
}

bool Weapon::CheckChargedDur(float chargedTime)  //charge count세서 리턴
{
	int durUsechargeAtk = chargedTime / chgTime * durUseChg;
	curDur -= durUsechargeAtk;
	if (curDur)
		return true;
	return false;
}


void Weapon::Throw(Player* _player,Mathf::Vector3 statrPos)
{

	//throw나 이동등은 weapon캡슐로 옮기기
	OwnerPlayerIndex = _player->playerIndex;
	ownerPlayer = _player->GetOwner();
	startPos = statrPos;
	timer = 0.f;
}


void Weapon::OnTriggerEnter(const Collision& collision)
{
}
