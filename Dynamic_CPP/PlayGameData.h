#pragma once
#include "Core.Minimal.h"


class Weapon;
//�������� -> ������ �Ѿ�� ��� �÷��̾� �����͵�
class PlayGameData
{

	bool hasData = false;
	

public:
	PlayGameData();
	~PlayGameData();

	
	

	void SavePlayerData(int _playerIndex);  //�������������� ���� ��������
	void LoadPlayerData(int _playerIndex); //�������� ������ ������ �ε�

	void SaveAsisData();
	void LoadAsisData();
};


class PlayerData
{
	int curHP = 1;
	std::vector<Weapon> m_weaponInventory; //type�̶� �������� �����ִٰ� �������� AddWeapon ���ֱ�
};


class AsisData
{

};