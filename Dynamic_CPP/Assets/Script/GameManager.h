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
	std::vector<Entity*>	m_asis{};		//테스트나 만약 아시스가 여럿이 나올 경우 대비.
	//Pool Managers
	SFXPoolManager*			SFXPool{};
	ObjectPoolManager*		objectPoolManager{};
	ControllerVibration*	ControllerVibrationData{};

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
		float baseThrowRange{ 0 };
		bool  initialized{ false };
	};
	std::unordered_map<class Player*, PlayerBaseSnapshot> m_baseByPlayer{};

	void EnsureBaseSnapshot(class Player* player);

public:
	class SceneTransitionUI*	m_sceneTransitionUI{ nullptr };
	float						displayPollutionGaugeRatio{}; //테스트 용
	float						m_fadeInDuration = 0.3f; // 필요시 인스펙터/ini로
	bool						TestCameraControll = false; //10월 시연용 카메라 따라가기 On, Off면 아시스따라가기 and 캐릭터 가두기 
	bool						m_isGameOver = false;
};

