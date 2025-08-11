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
	if (markIndex == -1)   //��ũ�������� ������ �״�� �ο�
	{
		markIndex = _playerindex;
		return true;
	}
	else  //�̹� ������ ������
	{
		if (markIndex != _playerindex) //���ְ� ������
		{
			markIndex = -1;           //��ũ����� ��ũ�����ٴ� �˸��ֱ�
			return true;              //true ũ��Ƽ�ð��� �ߵ�
		}

	}
	
	return false;
}
