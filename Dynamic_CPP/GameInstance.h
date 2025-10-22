#pragma once
#include "Core.Minimal.h"
#include "ItemInfo.h"
#include "DLLAcrossSingleton.h"
//input device
static constexpr int MAX_INPUT_DEVICE = 2;
enum class CharType { None = 0, Man = 1, Woman = 2 };
enum class PlayerDir { None = 0, Left = 1, Right = 2 };
enum class SceneType { Bootstrap, SelectChar, Loading, Stage, Tutorial, Boss, Credits, GameOver };

class EventManager;
class GameInstance : public DLLCore::Singleton<GameInstance>
{
private:
	friend class DLLCore::Singleton<GameInstance>;
	GameInstance() = default;
	~GameInstance() = default;

public:
	void Initialize();

public:
	//Scene Management
	void LoadSceneSettings();
	void AsyncSceneLoadUpdate();
	void LoadScene(const std::string& sceneName);
	void SwitchScene(const std::string& sceneName);
	void UnloadScene(const std::string& sceneName);
	bool IsLoadSceneComplete() const { return m_isLoadSceneComplete; }
	int GetAfterLoadSceneIndex() const { return m_beyondSceneIndex; }
	void SetAfterLoadSceneIndex(int type = 0) { m_beyondSceneIndex = type; }

public:
	//Scene Management(NEW)
	void LoadSettingedScene(int sceneType);
	void SwitchSettingedScene(int sceneType);
	void ReloadPrevScene() { LoadSettingedScene(static_cast<int>(m_prevSceneType)); }
	void SwitchToPrevScene() { SwitchSettingedScene(static_cast<int>(m_prevSceneType)); }
	int GetCurrentSceneType() const { return static_cast<int>(m_currentSceneType); }
	int GetPrevSceneType() const { return static_cast<int>(m_prevSceneType); }
	void SetCurrentSceneType(int sceneType) { m_currentSceneType = static_cast<SceneType>(sceneType); }
	void PauseGame();
	void ResumeGame();
	void ExitGame();

public:
	// Input Device Management
	void SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	void RemovePlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);

public:
	// Reward Management
	void SetRewardAmount(int amount) { m_RewardAmount = amount; } 	// 게임 시작 시 또는 치트용.
	void AddRewardAmount(int amount);
	int GetRewardAmount() const { return m_RewardAmount; }

public:
	// Event Manager Management
	EventManager* GetActiveEventManager() const { return m_eventManager; }
	void SetActiveEventManager(EventManager* mgr) { m_eventManager = mgr; }
	void ClearActiveEventManager() { m_eventManager = nullptr; }

public:
	// Item Info Management
	void LoadItemInfoFromCSV(const std::string& csvFilePath);
	const ItemInfo* GetItemInfo(int itemID, int rarity) const;
	int GetMaxItemID() const { return m_maxItemID; }

public:
	// Enhancement Management
	template<ItemEnhancementType T>
	void AddEnhancementDelta(SourceKey key, const EnhancementDelta<T>& d);
	void RemoveEnhancementDelta(SourceKey key);

public:
	// 조회/적용
	float GetEnhancement(ItemEnhancementType t) const;
	int	ApplyToBaseInt(ItemEnhancementType t, int base) const;
	float ApplyToBaseFloat(ItemEnhancementType t, float base) const;
	void ResetAllEnhancements();
	void ApplyItemEnhancement(const ItemInfo& info);
	void RemoveItemEnhancement(const ItemInfo& info);
	bool HasApplied(int itemId, int rarity) const;
	std::vector<ItemInfo> PickRandomUnappliedItems(int count);

public:
	// Game Setting
	bool IsViveEnabled() const { return m_isViveEnabled; }
	void SetViveEnabled(bool enable) { m_isViveEnabled = enable; }
	bool IsBootstrapCompleted() const { return m_isBootstrapCompleted; }
	void SetBootstrapCompleted(bool completed) { m_isBootstrapCompleted = completed; }

public:
	//Item BG Color
	Mathf::Color4									CommonItemColor{ 1.f, 1.f, 1.f, 1.f };
	Mathf::Color4									RareItemColor{ 1.f, 1.f, 1.f, 1.f };
	Mathf::Color4									EpicItemColor{ 1.f, 1.f, 1.f, 1.f };

private:
	EventManager*									m_eventManager{ nullptr };
	int												m_RewardAmount{};
	bool											m_isLoadSceneComplete{ false };
	bool											m_isInitialize{ false };

private:
	// 로딩 이후 전환 씬 인덱스 관리용
	int												m_beyondSceneIndex{ 0 };
	SceneType										m_currentSceneType{ SceneType::Bootstrap };
	SceneType										m_prevSceneType{ SceneType::Bootstrap };

private:
	// 로드된 씬들을 저장하는 맵
	std::unordered_map<SceneType, std::string>		m_settingedSceneNames;
	std::unordered_map<std::string, class Scene*>	m_loadedScenes;

private:
	using PlayerInputDiviceArr = std::array<std::pair<CharType, PlayerDir>, MAX_INPUT_DEVICE>;
	// 오른쪽 왼쪽 UI 구분용 Left: 0, Right: 1
	PlayerInputDiviceArr							m_playerInputDevices;
	std::future<Scene*>								m_loadingSceneFuture;

private:
	using ItemInfoMap = std::unordered_map<ItemUniqueID, ItemInfo, ItemUniqueIDHash>;
	// Item Info Management
	ItemInfoMap										m_itemInfoMap;
	int												m_maxItemID{ 0 };
	float											m_enhancementValue[MAX_ENHANCEMENT_TYPE]{};
	std::unordered_map<SourceKey, AnyDelta>			m_applied;

private:
	// Game Setting
	bool											m_isViveEnabled{ true };
	bool											m_isBootstrapCompleted{ false };
};

template<ItemEnhancementType T>
void GameInstance::AddEnhancementDelta(SourceKey key, const EnhancementDelta<T>& d)
{
	// 갱신 시 기존 제거
	RemoveEnhancementDelta(key);

	constexpr int idx = static_cast<int>(T);
	if (idx < 0 || idx >= MAX_ENHANCEMENT_TYPE) return;

	m_enhancementValue[idx] += static_cast<float>(d.value);
	m_applied.emplace(key, d);
}