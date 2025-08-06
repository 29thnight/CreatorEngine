#include "Weapon.h"
#include "pch.h"
#include "Player.h"
#include "Transform.h"
using namespace Mathf;
void Weapon::Start()
{
	if (itemtype == 0)
	{
		itemType = ItemType::Basic;
	}
	else
	{
		itemType = ItemType::Meely;
	}
}


void Weapon::Update(float tick)
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


void Weapon::Throw(Player* _player,Mathf::Vector3 statrPos)
{
	OwnerPlayerIndex = _player->playerIndex;
	ownerPlayer = _player->GetOwner();
	startPos = statrPos;
	timer = 0.f;
}


void Weapon::OnTriggerEnter(const Collision& collision)
{
}
