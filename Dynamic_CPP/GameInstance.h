#pragma once
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"

static constexpr int MAX_INPUT_DEVICE = 2;
enum class CharType { None = 0, Man = 1, Woman = 2 };
enum class PlayerDir { None = 0, Left = 1, Right = 2 };

class EventManager;
class GameInstance : public DLLCore::Singleton<GameInstance>
{
private:
	friend class DLLCore::Singleton<GameInstance>;
	GameInstance() = default;
	~GameInstance() = default;

public:
	//Scene Management
	void AsyncSceneLoadUpdate();
	void LoadScene(const std::string& sceneName);
	void SwitchScene(const std::string& sceneName);
	void UnloadScene(const std::string& sceneName);
	void LoadImidiateNextScene();
	bool IsLoadSceneComplete() const { return m_isLoadSceneComplete; }
	void SetNextSceneName(const std::string& sceneName) { m_nextSceneName = sceneName; }
	const std::string& GetNextSceneName() const { return m_nextSceneName; }
	// Input Device Management
	void SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	// Reward Management
	void SetRewardAmount(int amount) { m_RewardAmount = amount; } 	// 게임 시작 시 또는 치트용.
	void AddRewardAmount(int amount);
	int GetRewardAmount() const { return m_RewardAmount; }
	// Event Manager Management
	EventManager* GetActiveEventManager() const { return m_eventManager; }
	void SetActiveEventManager(EventManager* mgr) { m_eventManager = mgr; }
	void ClearActiveEventManager() { m_eventManager = nullptr; }

private:
	EventManager* m_eventManager{ nullptr };
	int m_RewardAmount{};
	bool m_isLoadSceneComplete{ false };
	std::string m_nextSceneName{};
	// 로드된 씬들을 저장하는 맵
	std::unordered_map<std::string, class Scene*> m_loadedScenes;
	// 오른쪽 왼쪽 UI 구분용 Left: 0, Right: 1
	std::array<std::pair<CharType, PlayerDir>, MAX_INPUT_DEVICE> m_playerInputDevices;
	std::future<Scene*>  m_loadingSceneFuture;
};