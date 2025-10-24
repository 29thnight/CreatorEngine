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
	std::vector<WeaponData> m_weaponDatas; //type이랑 내구도만 갖고있다가 새씬에서 AddWeapon 해주기 

};


struct AsisData
{
	int curHP = 1;
};


//스테이지 -> 보스로 넘어갈떄 들고갈 플레이어 데이터들
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
	void SavePlayerData(Player* player);  //보스씬가기전에 현재 상태저장
	void LoadPlayerData(int _playerIndex); //보스씬등 도착시 데이터 로드

	void SaveAsisData(Asis* asis);
	void LoadAsisData();
};

