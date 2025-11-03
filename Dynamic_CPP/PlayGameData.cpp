#include "PlayGameData.h"
#include "pch.h"
#include "GameManager.h"
#include "Player.h"
#include "Weapon.h"
#include "PrefabUtility.h"
#include "EntityAsis.h"
PlayGameData::PlayGameData()
{
}

PlayGameData::~PlayGameData()
{
}

void PlayGameData::Initialize()
{
	hasData = false;
}

void PlayGameData::SaveData()
{




	hasData = true;
}

void PlayGameData::SavePlayerData(Player* player)
{
	hasData = true; 
	auto& data = playerDatas[player->playerIndex];
	data.m_weaponDatas.clear();
	data.curHP = player->m_currentHP;

	for (auto weapon : player->m_weaponInventory)
	{
		WeaponData weapondata;
		weapondata.curDur = weapon->curDur;
		weapondata.itemType = weapon->itemType;
		data.m_weaponDatas.push_back(weapondata);
	}
}

void PlayGameData::LoadPlayerData(int _playerIndex)
{
	if (hasData == false) return;
	auto GMObj = GameObject::Find("GameManager");
	if (GMObj)
	{
		GameManager* GM = GMObj->GetComponent<GameManager>();
		auto players= GM->GetPlayers();
		Player* player = nullptr;
		for (auto& entity : players)
		{
			auto curPlayer = dynamic_cast<Player*>(entity);
			if (curPlayer->playerIndex == _playerIndex)
			{
				player = curPlayer;
				break;
			}
		}
		auto& data = playerDatas[_playerIndex];
		player->SetCurHP(data.curHP);

		for (auto& weaponData : data.m_weaponDatas)
		{
			GameObject* weaponObj = nullptr;
			Weapon* weapon = nullptr;
			Prefab* weaponPrefab = nullptr;
			switch (weaponData.itemType)
			{

			case ItemType::Melee:
				weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponMelee");
				if (weaponPrefab)
				{
					weaponObj  = PrefabUtilitys->InstantiatePrefab(weaponPrefab, "Weapon");
					weapon = weaponObj->GetComponent<Weapon>();
					if (weapon)
					{
						player->AddWeapon(weapon); // 필요한 경우 weapon 전달
						weapon->Initialize(weaponData.curDur);
					}
				}
				break;
			case ItemType::Range:
				weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponWand");
				if (weaponPrefab)
				{
					weaponObj = PrefabUtilitys->InstantiatePrefab(weaponPrefab, "Weapon");
					weapon = weaponObj->GetComponent<Weapon>();
					if (weapon)
					{
						player->AddWeapon(weapon); // 필요한 경우 weapon 전달
						weapon->Initialize(weaponData.curDur);
					}
				}
				break;
			case ItemType::Bomb:
				weaponPrefab = PrefabUtilitys->LoadPrefab("WeaponBomb");
				if (weaponPrefab)
				{
					weaponObj = PrefabUtilitys->InstantiatePrefab(weaponPrefab, "Weapon");
					weapon = weaponObj->GetComponent<Weapon>();
					if (weapon)
					{
						player->AddWeapon(weapon); // 필요한 경우 weapon 전달
						weapon->Initialize(weaponData.curDur);
					}
				}
				break;
			default:
				break;
			}
			player->SwapBasicWeapon();
		}

	}
}

void PlayGameData::SaveAsisData(EntityAsis* asis)
{
	hasData = true;
	auto& data = asisData;
	data.curHP = asis->m_currentHP;
}

void PlayGameData::LoadAsisData()
{
	if (hasData == false) return;


	auto GMObj = GameObject::Find("GameManager");
	if (GMObj)
	{
		GameManager* GM = GMObj->GetComponent<GameManager>();
		if (GM)
		{
			if (!GM->GetAsis().empty())
			{
				auto asis = GM->GetAsis()[0];
				EntityAsis* Asis = dynamic_cast<EntityAsis*>(asis);
				if (Asis)
					Asis->SetCurHP(asisData.curHP);
			}
		}
	}
}
