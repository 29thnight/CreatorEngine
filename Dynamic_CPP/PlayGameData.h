#pragma once
#include "Core.Minimal.h"


class Weapon;
//스테이지 -> 보스로 넘어갈떄 들고갈 플레이어 데이터들
class PlayGameData
{

	bool hasData = false;
	

public:
	PlayGameData();
	~PlayGameData();

	
	

	void SavePlayerData(int _playerIndex);  //보스씬가기전에 현재 상태저장
	void LoadPlayerData(int _playerIndex); //보스씬등 도착시 데이터 로드

	void SaveAsisData();
	void LoadAsisData();
};


class PlayerData
{
	int curHP = 1;
	std::vector<Weapon> m_weaponInventory; //type이랑 내구도만 갖고있다가 새씬에서 AddWeapon 해주기
};


class AsisData
{

};