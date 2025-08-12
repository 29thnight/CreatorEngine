#include "pch.h"
#include "CriticalMark.h"



void CriticalMark::UpdateMark()
{
	
}

void CriticalMark::ResetMark()
{
	markIndex = -1;
}

bool CriticalMark::TryCriticalHit(int _playerindex)
{
	if (markIndex == -1)   //마크가없을떄 떄리면 그대로 부여
	{
		markIndex = _playerindex;
		return true;
	}
	else  //이미 있을때 떄리면
	{
		if (markIndex != _playerindex) //딴애가 떄리면
		{
			markIndex = -1;           //마크지우고 마크터졌다는 알림주기
			return true;              //true 크리티컬공격 발동
		}

	}
	
	return false;
}
