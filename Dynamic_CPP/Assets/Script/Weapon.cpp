#include "Weapon.h"
#include "pch.h"
#include "Player.h"
#include "Transform.h"
using namespace Mathf;

void Weapon::Start()
{
}

void Weapon::Update(float tick)
{

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

void Weapon::DecreaseDur(bool isCharge)
{
	if (itemType == ItemType::Basic) return; 

	if (isCharge == false)
	{
		curDur -= durUseAtk;
		if (curDur <= 0)
		{
			curDur = 0;
			isBreak = true;
		}
	}
	else   //���� ���� �ϰ��
	{
		curDur = 0;
		isBreak = true;
	}
}

bool Weapon::CheckChargedDur(float chargedTime)  
{
	//������ �׳� �ٻѼ�
	curDur -= curDur;
	if (curDur)
		return true;
	return false;
}



void Weapon::OnTriggerEnter(const Collision& collision)
{
}
