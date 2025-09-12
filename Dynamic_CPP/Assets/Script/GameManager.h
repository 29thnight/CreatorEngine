#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "GameManager.generated.h"

class Entity;
class ActionMap;
class Weapon;
class GameManager : public ModuleBehavior
{
public:
   ReflectGameManager
	[[ScriptReflectionField]]
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
	//Scene Management
	void LoadScene(const std::string& sceneName);
	void SwitchScene(const std::string& sceneName);
	void UnloadScene(const std::string& sceneName);

	[[Method]]
	void LoadTestScene();
	[[Method]]
	void SwitchTestScene();

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
	std::future<Scene*> m_loadingSceneFuture;

private:
	void CheatMiningResource();

private:
	static int m_RewardAmount;
	static int m_player1DeviceID;
	static int m_player2DeviceID;
private:
	static std::unordered_map<std::string, Scene*> m_loadedScenes; // 로드된 씬들을 저장하는 맵
public:
	static void InitReward(int amount) { m_RewardAmount = amount; }	// 게임 시작 시 또는 치트용.
	inline void AddReward(int amount) { 
		GameManager::m_RewardAmount += amount; 
		std::cout << "Current Reward: " << m_RewardAmount << std::endl;
	}
	static int GetReward() { return m_RewardAmount; }
	static void SetPlayer1DeviceID(int id) { m_player1DeviceID = id; }
	static void SetPlayer2DeviceID(int id) { m_player2DeviceID = id; }
	static void SwitchPlayerInputDevice() {
		auto temp = m_player1DeviceID;
		m_player1DeviceID = m_player2DeviceID;
		m_player2DeviceID = temp;
	}
};
