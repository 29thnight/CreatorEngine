//전역 게임 관리자 클래스
#pragma once
#include "Utility_Framework\Core.Definition.h"
#include "Utility_Framework\Core.Minimal.h"
#include "EventSystem.h"
#include "World.h"

class GameManager : public Singleton<GameManager>
{
private:
	friend class Singleton;
private:
	GameManager() = default;
	~GameManager() = default;

public:
	enum class GameState
	{
		InitState,
		Loading,
		Play,
		Pause,
		Clear,
		GameOver
	};

	enum class GameLevel
	{
		MainMenu = 0,
		Stage1,
		Stage2,
		Stage3,
		Stage4,
		Stage5,
		MaxGameLevel
	};

public:
	void Initialize();
	void Loading();
	void Update(float tick);
	void PlayUpdate(float tick);
	bool EnemyRespawn(Object* target);
public:
	void SetGameState(GameState state) { _gameState = state; }
	GameState GetGameState() const { return _gameState; }

	void SetGameLevel(GameLevel level) { _gameLevel = level; }
	GameLevel GetGameLevel() const { return _gameLevel; }

	void SetWorld(World* world) { currWorld = world; }
	World* GetCurrWorld() const { return currWorld; }

	std::string GetStageName(GameLevel level) const { return _stageList[(unsigned long long)level]; }

	float GetTimeScale() const { return _timeScale; }

	void Pause() { _timeScale = 0.0f; }
	void Resume() { _timeScale = 1.0f; }

	void SceneChange(int level);

private:
	GameState _gameState = GameState::InitState; // Gmanager_GameState
	GameLevel _gameLevel = GameLevel::MainMenu; // Gmanager_GameLevel
	World* currWorld = nullptr; // Gmanager_currWorld

	float _timeScale = 1.0f; // inGameTimeScale

	//Player* _player1 = nullptr; // Gmanager_Player
	//Player* _player2 = nullptr; // Gmanager_Player

	std::vector<std::string> _stageList; // Gmanager_stageList
};

inline static auto& GameManagement = GameManager::GetInstance();