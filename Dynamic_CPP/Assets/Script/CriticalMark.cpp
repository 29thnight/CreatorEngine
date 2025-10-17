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
		if (markIndex == -1 && canMark == true)   //��ũ�������� ������ �״�� �ο�
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
		else  //�̹� ������ ������
		{
			if (markIndex != _playerindex) //���ְ� ������
			{
				markIndex = -1;           //��ũ����� ��ũ�����ٴ� �˸��ֱ�
				canMark = false;          //������ n�ʰ� ��ũ ����Ұ���
				OnMark = false;
				if (markEffect)
					markEffect->StopEffect();
				return true;              //true ũ��Ƽ�ð��� �ߵ�
			}

		}
	}

	return false;



}

