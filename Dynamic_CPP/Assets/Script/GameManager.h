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
	virtual void Update(float tick) override;
	virtual void OnDisable() override;

public:
	void GameInit();
	//Scene Management
	void LoadScene(int sceneType);
	void SwitchScene(int sceneType);
	// Input Device Management
	void SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	void RemovePlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	float GetAsisPollutionGaugeRatio();
	void SetLoadingReq(bool req = true) { m_isLoadingReq = req; }

	// Test for Scene Management
	[[Method]]
	void LoadNextScene();
	[[Method]]
	void SwitchNextScene();
	[[Method]]
	void LoadPrevScene();
	[[Method]]
	void SwitchPrevScene();
	[[Method]]
	void SwitchNextSceneWithFade();
	[[Method]]
	void SwitchPrevSceneWithFade();
	[[Method]]
	void LoadImidiateNextScene();
	[[Property]]
	int			m_prevSceneIndex{ 0 };
	[[Property]]
	int			m_nextSceneIndex{ 0 };
	[[Property]]
	bool		m_isTestReward{ false };
	bool		m_isLoadingReq{ false };
	bool		startSelectTimer{ false };
	bool		m_isSwitching = false;
	int			selectPlayerCount{};
	GameObject*	testCamera = nullptr;
	// ~Test for Scene Management
public:
	void PushEntity(Entity* entity);
	void PushPlayer(Entity* player);
	void PushAsis(Entity* asis);
	const std::vector<Entity*>& GetEntities();
	const std::vector<Entity*>& GetPlayers();
	const std::vector<Entity*>& GetAsis();

	std::vector<Entity*>& GetResourcePool();
	std::vector<Weapon*>& GetWeaponPiecePool();
	// SFX Pool Manager
	void PushSFXPool(SFXPoolManager* _SFXPool);
	SFXPoolManager* GetSFXPool();
	void PushObjectPoolManager(ObjectPoolManager* _objPoolManager);
	ObjectPoolManager* GetObjectPoolManager();
	// Controller Vibration
	void PushControllerVibration(ControllerVibration* _ControllerVibration);
	ControllerVibration* GetControllerVibration();
	// Player Stat
	[[Method]]
	void ApplyGlobalEnhancementsToAllPlayers();
	void ApplyGlobalEnhancementsToPlayer(class Player* player);
	// Reward Management
	void InitReward(int amount);
	void AddReward(int amount);
	int GetReward();

	void BossClear();
private:
	//Entities
	std::vector<Entity*>	m_entities{};
	std::vector<Entity*>	m_resourcePool{};
	std::vector<Weapon*>	m_weaponPiecePool{};
	std::vector<Entity*>	m_players{};
	std::vector<Entity*>	m_asis{};		//�׽�Ʈ�� ���� �ƽý��� ������ ���� ��� ���.
	//Pool Managers
	SFXPoolManager*			SFXPool{};
	ObjectPoolManager*		objectPoolManager{};
	ControllerVibration*	ControllerVibrationData{};

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
		float baseThrowRange{ 0 };
		bool  initialized{ false };
	};
	std::unordered_map<class Player*, PlayerBaseSnapshot> m_baseByPlayer{};

	void EnsureBaseSnapshot(class Player* player);

public:
	class SceneTransitionUI*	m_sceneTransitionUI{ nullptr };
	float						displayPollutionGaugeRatio{}; //�׽�Ʈ ��
	float						m_fadeInDuration = 0.3f; // �ʿ�� �ν�����/ini��
	bool						TestCameraControll = false; //10�� �ÿ��� ī�޶� ���󰡱� On, Off�� �ƽý����󰡱� and ĳ���� ���α� 
	bool						m_isGameOver = false;
};

