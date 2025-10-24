#pragma once
#include "Core.Minimal.h"
#include "ItemType.h"

class Weapon;
class Player;
class Asis;

struct WeaponData
{
	ItemType itemType = ItemType::Basic;
	int curDur = 1;

};
struct PlayerData
{
	int curHP = 1;
	std::vector<WeaponData> m_weaponDatas; //type�̶� �������� �����ִٰ� �������� AddWeapon ���ֱ� 

};


struct AsisData
{
	int curHP = 1;
};


//�������� -> ������ �Ѿ�� ��� �÷��̾� �����͵�
class PlayGameData
{

	bool hasData = false;
	
	std::array<PlayerData, 2> playerDatas;
	AsisData asisData;
public:
	PlayGameData();
	~PlayGameData();

	
	
	void Initialize();
	void SaveData();
	void SavePlayerData(Player* player);  //�������������� ���� ��������
	void LoadPlayerData(int _playerIndex); //�������� ������ ������ �ε�

	void SaveAsisData(Asis* asis);
	void LoadAsisData();
};

