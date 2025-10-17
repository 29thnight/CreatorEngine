#include "CriticalMark.h"
#include "pch.h"
#include "EffectComponent.h"
void CriticalMark::Start()
{
	markEffect = GetOwner()->GetComponent<EffectComponent>();
}

void CriticalMark::Update(float tick)
{
	if (OnMark)
	{
		markElaspedTime += tick;
		if (markDuration <= markElaspedTime)
		{
			ResetMark();
			canMark = false;
			markElaspedTime = 0.f;
			if(markEffect)
				markEffect->StopEffect();
		}
	}


	if (false == canMark)
	{
		markCoolElaspedTime += tick;
		if (markCoolDown <= markCoolElaspedTime)
		{
			canMark = true;
			markCoolElaspedTime = 0;
		}
	}
}

void CriticalMark::ResetMark()
{
	markIndex = -1;
	OnMark = false;
}
bool CriticalMark::UpdateMark(int _playerindex)
{
	if (markEffect)
	{
		if (markIndex == -1 && canMark == true)   //마크가없을떄 떄리면 그대로 부여
		{
			markIndex = _playerindex;
			markElaspedTime = 0.f;
			OnMark = true;
 
			if (markIndex == 0)
			{
				markEffect->PlayEffectByName("red");
			}
			else if (markIndex == 1)
			{
				markEffect->PlayEffectByName("blue");
			}
			return false;
		}
		else  //이미 있을때 떄리면
		{
			if (markIndex != _playerindex) //딴애가 떄리면
			{
				markIndex = -1;           //마크지우고 마크터졌다는 알림주기
				canMark = false;          //터지면 n초간 마크 새김불가능
				OnMark = false;
				if (markEffect)
					markEffect->StopEffect();
				return true;              //true 크리티컬공격 발동
			}

		}
	}

	return false;



}

