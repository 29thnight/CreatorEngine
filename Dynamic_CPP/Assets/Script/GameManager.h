#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class ActionMap;
class Weapon;
class GameManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(GameManager)
	virtual void Awake() override;
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override;
	virtual void OnDestroy() override  {}

public:
	void LoadScene(const std::string& sceneName);

public:
	void PushEntity(Entity* entity);
	void PushPlayer(Entity* player);
	void PushAsis(Entity* asis);
	const std::vector<Entity*>& GetEntities();
	const std::vector<Entity*>& GetPlayers();
	const std::vector<Entity*>& GetAsis();

	std::vector<Entity*>& GetResourcePool();
	std::vector<Weapon*>& GetWeaponPiecePool();
private:
	std::vector<Entity*> m_entities;
	ActionMap* playerMap{ nullptr };

	std::vector<Entity*> m_resourcePool;
	std::vector<Weapon*> m_weaponPiecePool;
	std::vector<Entity*> m_players;
	std::vector<Entity*> m_asis;		//테스트나 만약 아시스가 여럿이 나올 경우 대비.
private:
	void CheatMiningResource();

private:
	static int m_RewardAmount;
	static int m_player1DeviceID;
	static int m_player2DeviceID;
public:
	static void InitReward(int amount) { m_RewardAmount = amount; }	// 게임 시작 시 또는 치트용.
	inline void AddReward(int amount) { 
		GameManager::m_RewardAmount += amount; 
		std::cout << "Current Reward: " << m_RewardAmount << std::endl;
	}
	static void SetPlayer1DeviceID(int id) { m_player1DeviceID = id; }
	static void SetPlayer2DeviceID(int id) { m_player2DeviceID = id; }
	static void SwitchPlayerInputDevice() {
		auto temp = m_player1DeviceID;
		m_player1DeviceID = m_player2DeviceID;
		m_player2DeviceID = temp;
	}
};