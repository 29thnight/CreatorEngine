#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "GameManager.generated.h"
#include "GameInstance.h"

class ObjectPoolManager;
class Entity;
class ActionMap;
class Weapon;
class SFXPoolManager;
class ControllerVibration;
class GameManager : public ModuleBehavior
{
public:
   ReflectGameManager
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(GameManager)
	// 기본 함수들
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
	// Input Device Management
	void SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	void RemovePlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	float GetAsisPollutionGaugeRatio();

	// Test for Scene Management
	[[Method]]
	void LoadTestScene();
	[[Method]]
	void SwitchTestScene();
	[[Method]]
	void LoadNextScene();
	[[Method]]
	void SwitchNextScene();
	[[Method]]
	void LoadImidiateNextScene();
	[[Property]]
	bool m_isTestReward{ false };
	[[Property]]
	std::string m_nextSceneName{};
	// Player Stat
	[[Method]]
	void ApplyGlobalEnhancementsToAllPlayers();

	void ApplyGlobalEnhancementsToPlayer(class Player* player);

	int selectPlayerCount{};
	bool startSelectTimer{ false };
	float displayPollutionGaugeRatio{}; //테스트 용

public:
	bool TestCameraControll = false; //10월 시연용 카메라 따라가기 On, Off면 아시스따라가기 and 캐릭터 가두기 
	

	GameObject* testCamera = nullptr;
	void PushEntity(Entity* entity);
	void PushPlayer(Entity* player);
	void PushAsis(Entity* asis);
	const std::vector<Entity*>& GetEntities();
	const std::vector<Entity*>& GetPlayers();
	const std::vector<Entity*>& GetAsis();

	std::vector<Entity*>& GetResourcePool();
	std::vector<Weapon*>& GetWeaponPiecePool();

	void PushSFXPool(SFXPoolManager* _SFXPool);
	SFXPoolManager* GetSFXPool();
	void PushObjectPoolManager(ObjectPoolManager* _objPoolManager);
	ObjectPoolManager* GetObjectPoolManager();

	void PushControllerVibration(ControllerVibration* _ControllerVibration);
	ControllerVibration* GetControllerVibration();
private:
	std::vector<Entity*> m_entities;
	std::vector<Entity*> m_resourcePool;
	std::vector<Weapon*> m_weaponPiecePool;
	std::vector<Entity*> m_players;
	std::vector<Entity*> m_asis;		//테스트나 만약 아시스가 여럿이 나올 경우 대비.

	SFXPoolManager* SFXPool;
	ObjectPoolManager* objectPoolManager;
	ControllerVibration* ControllerVibrationData;

private:
	void CheatMiningResource();

	struct PlayerBaseSnapshot {
		// Player 쪽(가산/곱산 대상)
		float baseMoveSpeed{ 0.f };         // Player::baseMoveSpeed 를 기준으로 사용
		int   baseDashAmount{ 0 };          // Player::dashAmount 의 초기값 스냅샷
		int   baseAtk{ 0 };                 // Player::Atk 의 초기값 스냅샷
		float baseAtkSpeedMult{ 1.f };      // Player::MultipleAttackSpeed 의 초기값 스냅샷

		// Entity 쪽(최대체력은 Entity 사용)
		int   baseMaxHP{ 0 };               // Entity::m_maxHP 초기값 스냅샷
		bool  initialized{ false };
	};
	std::unordered_map<class Player*, PlayerBaseSnapshot> m_baseByPlayer;

	void EnsureBaseSnapshot(class Player* player);

public:
	void InitReward(int amount);
	void AddReward(int amount);
	int GetReward();
};

