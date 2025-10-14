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
	// �⺻ �Լ���
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
	float displayPollutionGaugeRatio{}; //�׽�Ʈ ��

public:
	bool TestCameraControll = false; //10�� �ÿ��� ī�޶� ���󰡱� On, Off�� �ƽý����󰡱� and ĳ���� ���α� 
	

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
	std::vector<Entity*> m_asis;		//�׽�Ʈ�� ���� �ƽý��� ������ ���� ��� ���.

	SFXPoolManager* SFXPool;
	ObjectPoolManager* objectPoolManager;
	ControllerVibration* ControllerVibrationData;

private:
	void CheatMiningResource();

	struct PlayerBaseSnapshot {
		// Player ��(����/���� ���)
		float baseMoveSpeed{ 0.f };         // Player::baseMoveSpeed �� �������� ���
		int   baseDashAmount{ 0 };          // Player::dashAmount �� �ʱⰪ ������
		int   baseAtk{ 0 };                 // Player::Atk �� �ʱⰪ ������
		float baseAtkSpeedMult{ 1.f };      // Player::MultipleAttackSpeed �� �ʱⰪ ������

		// Entity ��(�ִ�ü���� Entity ���)
		int   baseMaxHP{ 0 };               // Entity::m_maxHP �ʱⰪ ������
		bool  initialized{ false };
	};
	std::unordered_map<class Player*, PlayerBaseSnapshot> m_baseByPlayer;

	void EnsureBaseSnapshot(class Player* player);

public:
	void InitReward(int amount);
	void AddReward(int amount);
	int GetReward();
};

