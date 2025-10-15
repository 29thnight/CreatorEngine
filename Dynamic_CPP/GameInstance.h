#pragma once
#include "Core.Minimal.h"
#include "ItemInfo.h"
#include "DLLAcrossSingleton.h"
//input device
static constexpr int MAX_INPUT_DEVICE = 2;
enum class CharType { None = 0, Man = 1, Woman = 2 };
enum class PlayerDir { None = 0, Left = 1, Right = 2 };
//정리해보면 이런데...
enum class SceneType { Bootstrap, SelectChar, Loading, Stage, Tutorial, Boss, Credits };

class EventManager;
class GameInstance : public DLLCore::Singleton<GameInstance>
{
private:
	friend class DLLCore::Singleton<GameInstance>;
	GameInstance() = default;
	~GameInstance() = default;

public:
	void Initialize();
	//Scene Management
	void LoadSceneSettings();
	void AsyncSceneLoadUpdate();
	void LoadScene(const std::string& sceneName);
	void SwitchScene(const std::string& sceneName);
	void UnloadScene(const std::string& sceneName);
	void LoadImidiateNextScene();
	bool IsLoadSceneComplete() const { return m_isLoadSceneComplete; }
	void SetNextSceneName(const std::string& sceneName) { m_nextSceneName = sceneName; }
	const std::string& GetNextSceneName() const { return m_nextSceneName; }
	void SetBeyondSceneName(const std::string& sceneName) { m_beyondSceneName = sceneName; }
	const std::string& GetBeyondSceneName() const { return m_beyondSceneName; }
	// Input Device Management
	void SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	void RemovePlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir);
	// Reward Management
	void SetRewardAmount(int amount) { m_RewardAmount = amount; } 	// 게임 시작 시 또는 치트용.
	void AddRewardAmount(int amount);
	int GetRewardAmount() const { return m_RewardAmount; }
	// Event Manager Management
	EventManager* GetActiveEventManager() const { return m_eventManager; }
	void SetActiveEventManager(EventManager* mgr) { m_eventManager = mgr; }
	void ClearActiveEventManager() { m_eventManager = nullptr; }
	// Item Info Management
	void LoadItemInfoFromCSV(const std::string& csvFilePath);
	const ItemInfo* GetItemInfo(int itemID, int rarity) const;
	int GetMaxItemID() const { return m_maxItemID; }
	// Enhancement Management
	template<ItemEnhancementType T>
	void AddEnhancementDelta(SourceKey key, const EnhancementDelta<T>& d);
	void RemoveEnhancementDelta(SourceKey key);

	// 조회/적용
	float GetEnhancement(ItemEnhancementType t) const;
	int   ApplyToBaseInt(ItemEnhancementType t, int base)   const;
	float ApplyToBaseFloat(ItemEnhancementType t, float base) const;

	void ResetAllEnhancements();
	void ApplyItemEnhancement(const ItemInfo& info);
	void RemoveItemEnhancement(const ItemInfo& info);

	bool HasApplied(int itemId, int rarity) const;
	std::vector<ItemInfo> PickRandomUnappliedItems(int count);
	//테스트 전용
	Mathf::Color4 CommonItemColor{ 1.f, 1.f, 1.f, 1.f };
	Mathf::Color4 RareItemColor{ 1.f, 1.f, 1.f, 1.f };
	Mathf::Color4 EpicItemColor{ 1.f, 1.f, 1.f, 1.f };

private:
	EventManager* m_eventManager{ nullptr };
	int m_RewardAmount{};
	bool m_isLoadSceneComplete{ false };
	bool m_isInitialize{ false };
	//임시 씬 로드용
	std::string m_nextSceneName{};
	std::string m_beyondSceneName{}; //로딩씬 전용
	// 로드된 씬들을 저장하는 맵
	std::unordered_map<SceneType, std::string> m_settingedSceneNames;
	std::unordered_map<std::string, class Scene*> m_loadedScenes;
	// 오른쪽 왼쪽 UI 구분용 Left: 0, Right: 1
	std::array<std::pair<CharType, PlayerDir>, MAX_INPUT_DEVICE> m_playerInputDevices;
	std::future<Scene*>  m_loadingSceneFuture;
	// Item Info Management
	std::unordered_map<ItemUniqueID, ItemInfo, ItemUniqueIDHash> m_itemInfoMap;
	int m_maxItemID{ 0 };

	//int   m_addInt[MAX_ENHANCEMENT_TYPE] = {};
	//float m_addFloat[MAX_ENHANCEMENT_TYPE] = {};
	//float m_mulFloat[MAX_ENHANCEMENT_TYPE] = {};

	float m_enhancementValue[MAX_ENHANCEMENT_TYPE] = {};

	std::unordered_map<SourceKey, AnyDelta> m_applied;
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