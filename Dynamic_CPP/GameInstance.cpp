#include "GameInstance.h"
#include "SceneManager.h"
#include "DebugLog.h"
#include "CSVLoader.h"
#include "SimpleIniFile.h"
#include "pch.h"

inline constexpr std::string_view ToKey(SceneType t) {
	switch (t) {
	case SceneType::Bootstrap:  return "Bootstrap";
	case SceneType::SelectChar: return "SelectChar";
	case SceneType::Loading:    return "Loading";
	case SceneType::Stage:      return "Stage";
	case SceneType::Tutorial:   return "Tutorial";
	case SceneType::Boss:       return "Boss";
	case SceneType::Credits:    return "Credits";
	}
	return "Unknown";
}

inline std::optional<SceneType> FromKey(std::string_view k) {
	if (k == "Bootstrap")  return SceneType::Bootstrap;
	if (k == "SelectChar") return SceneType::SelectChar;
	if (k == "Loading")    return SceneType::Loading;
	if (k == "Stage")      return SceneType::Stage;
	if (k == "Tutorial")   return SceneType::Tutorial;
	if (k == "Boss")       return SceneType::Boss;
	if (k == "Credits")    return SceneType::Credits;
	return std::nullopt;
}

void GameInstance::Initialize()
{
	if (m_isInitialize) {
		return;
	}
	m_isInitialize = true;
	m_RewardAmount = 0;
	m_playerInputDevices.fill({ CharType::None, PlayerDir::None });

	LoadSceneSettings();

	std::string csvFileName = "ItemEnhancementSetting.csv";
	LoadItemInfoFromCSV(csvFileName);

}

void GameInstance::AddRewardAmount(int amount)
{
	m_RewardAmount += amount;
	while(m_RewardAmount < 0)
	{
		m_RewardAmount = 0;
	}

	while (m_RewardAmount > 99)
	{
		m_RewardAmount = 99;
	}
}

void GameInstance::LoadItemInfoFromCSV(const std::string& csvFilePath)
{
	file::path csvPath = PathFinder::Relative("CSV") / csvFilePath;

	m_itemInfoMap.clear();
	m_maxItemID = 0;
	int loaded = 0;

	try
	{
		CSVReader reader(csvPath.string(), true); // 헤더 스킵 모드
		for (const auto& row : reader)
		{
			ItemInfo info{};
			info.id					= row["id"].as<int>();
			info.rarity				= row["rarity"].as<int>();
			info.name				= row["name"].as<std::string>();
			info.description		= row["desc"].as<std::string>();
			info.price				= row["price"].as<int>();
			info.enhancementType	= row["enhancementType"].as<int>();

			if (IsFloatStat(info.enhancementType))
			{
				info.enhancementValue = row["enhancementValue"].as<float>();
			}
			else 
			{
				info.enhancementValue = row["enhancementValue"].as<int>();
			}

			ItemUniqueID key{ info.id, info.rarity };
			m_itemInfoMap[key] = std::move(info);

			// 최대치 갱신
			if (key.first > m_maxItemID)
			{
				m_maxItemID = key.first;
			}

			++loaded;
		}
	}
	catch (const std::exception& e)
	{
		LOG("LoadItemInfoFromCSV failed: " + std::string(e.what()));
		return;
	}

	LOG("LoadItemInfoFromCSV loaded " + std::to_string(loaded) + " rows from " + csvPath.string());
}

const ItemInfo* GameInstance::GetItemInfo(int itemID, int rarity) const
{
	auto it = m_itemInfoMap.find({ itemID, rarity });
	if (it != m_itemInfoMap.end()) {
		return &it->second;
	}
	return nullptr;
}

void GameInstance::RemoveEnhancementDelta(SourceKey key)
{
	auto it = m_applied.find(key);
	if (it == m_applied.end()) return;

	std::visit([&](auto&& held) {
		constexpr ItemEnhancementType T = std::decay_t<decltype(held)>::type;
		constexpr int idx = static_cast<int>(T);
		if constexpr (std::same_as<typename std::decay_t<decltype(held)>::value_type, int>) {
			m_addInt[idx] -= held.value;
		}
		else {
			m_mulFloat[idx] -= held.value;
		}
		}, it->second);

	m_applied.erase(it);
}

int GameInstance::GetAdd(ItemEnhancementType t) const
{
	return m_addInt[static_cast<int>(t)];
}

float GameInstance::GetMul(ItemEnhancementType t) const
{
	return m_mulFloat[static_cast<int>(t)];
}

int GameInstance::ApplyToBaseInt(ItemEnhancementType t, int base) const
{
	float result = (static_cast<float>(base + GetAdd(t))) * (1.f + GetMul(t));
	return static_cast<int>(std::lround(result));
}

float GameInstance::ApplyToBaseFloat(ItemEnhancementType t, float base) const
{
	return (base + static_cast<float>(GetAdd(t))) * (1.f + GetMul(t));
}

void GameInstance::ResetAllEnhancements()
{
	std::fill(std::begin(m_addInt), std::end(m_addInt), 0);
	std::fill(std::begin(m_mulFloat), std::end(m_mulFloat), 0.f);
	m_applied.clear();
}

void GameInstance::ApplyItemEnhancement(const ItemInfo& info)
{
	auto t = static_cast<ItemEnhancementType>(info.enhancementType);
	if (t == ItemEnhancementType::None) return;

	SourceKey key = MakeSourceKey(info.id, info.rarity);

	switch (t)
	{
	case ItemEnhancementType::MaxHPUp:
		AddEnhancementDelta<ItemEnhancementType::MaxHPUp>(key,
			EnhancementDelta<ItemEnhancementType::MaxHPUp>::Make(info.enhancementValue));
		break;
	case ItemEnhancementType::Atk:
		AddEnhancementDelta<ItemEnhancementType::Atk>(key,
			EnhancementDelta<ItemEnhancementType::Atk>::Make(info.enhancementValue));
		break;
	case ItemEnhancementType::DashCountUp:
		AddEnhancementDelta<ItemEnhancementType::DashCountUp>(key,
			EnhancementDelta<ItemEnhancementType::DashCountUp>::Make(info.enhancementValue));
		break;
	case ItemEnhancementType::WeaponDurabilityUp:
		AddEnhancementDelta<ItemEnhancementType::WeaponDurabilityUp>(key,
			EnhancementDelta<ItemEnhancementType::WeaponDurabilityUp>::Make(info.enhancementValue));
		break;

	case ItemEnhancementType::MoveSpeedUp: {
		// CSV가 10(%)면 0.10f로 변환 권장
		float mul = static_cast<float>(info.enhancementValue) * 0.01f;
		AddEnhancementDelta<ItemEnhancementType::MoveSpeedUp>(key,
			EnhancementDelta<ItemEnhancementType::MoveSpeedUp>::Make(mul));
		break;
	}
	case ItemEnhancementType::AtkSpeedUp: {
		float mul = static_cast<float>(info.enhancementValue) * 0.01f;
		AddEnhancementDelta<ItemEnhancementType::AtkSpeedUp>(key,
			EnhancementDelta<ItemEnhancementType::AtkSpeedUp>::Make(mul));
		break;
	}
	default: break;
	}
}

void GameInstance::RemoveItemEnhancement(const ItemInfo& info)
{
	RemoveEnhancementDelta(MakeSourceKey(info.id, info.rarity));
}

bool GameInstance::HasApplied(int itemId, int rarity) const
{
	SourceKey key = MakeSourceKey(itemId, rarity);
	return m_applied.find(key) != m_applied.end();
}

std::vector<ItemInfo> GameInstance::PickRandomUnappliedItems(int count)
{
	std::vector<const ItemInfo*> candidates;
	candidates.reserve(m_itemInfoMap.size());

	// 1) 미적용(HasApplied == false)인 후보 모으기
	for (const auto& kv : m_itemInfoMap)
	{
		const ItemUniqueID& key = kv.first;
		const ItemInfo& info = kv.second;

		if (!HasApplied(info.id, info.rarity))
			candidates.push_back(&info);
	}

	// 2) 무작위 셔플 + 앞에서 count개 선택
	std::vector<ItemInfo> picked;
	if (candidates.empty()) return picked;

	std::random_device rd;
	std::mt19937 rng(rd());
	std::shuffle(candidates.begin(), candidates.end(), rng);

	const int n = std::min<int>(count, static_cast<int>(candidates.size()));
	picked.reserve(n);
	for (int i = 0; i < n; ++i)
		picked.push_back(*candidates[i]); // 복사

	return picked;
}

void GameInstance::LoadSceneSettings()
{
	auto settingINIPath = PathFinder::Relative("INI") / std::string("SceneBindSettings.ini");
	SimpleIniFile loadINI{ settingINIPath };

	if(const auto* sec = loadINI.TryGetSection("Scenes"))
	{
		for (const auto& [k, v] : *sec) {
			if (auto t = FromKey(k)) {
				m_settingedSceneNames[*t] = v; // INI 값이 있으면 덮어씀
			}
		}
	}
}

void GameInstance::AsyncSceneLoadUpdate()
{
	if (m_loadingSceneFuture.valid() && m_loadingSceneFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		Scene* loadedScene = m_loadingSceneFuture.get();
		if (loadedScene)
		{
			std::string sceneName = loadedScene->m_sceneName.ToString();
			m_loadedScenes[sceneName] = loadedScene;
			m_isLoadSceneComplete = true;
			LOG("Scene loaded: " + sceneName);
		}
		else {
			LOG("Failed to load scene.");
		}
	}
}

void GameInstance::LoadScene(const std::string& sceneName)
{
	file::path fullPath = PathFinder::Relative("Scenes\\") / std::string(sceneName + ".creator");
	m_loadingSceneFuture = SceneManagers->LoadSceneAsync(fullPath.string());
}

void GameInstance::SwitchScene(const std::string& sceneName)
{
	if (m_loadedScenes.find(sceneName) == m_loadedScenes.end()) {
		LOG("Scene not loaded: " + sceneName);
		return;
	}
	SceneManagers->ActivateScene(m_loadedScenes[sceneName]);
	m_isLoadSceneComplete = false;
}

void GameInstance::UnloadScene(const std::string& sceneName)
{
	// Unload only if the scene is loaded
}

void GameInstance::LoadImidiateNextScene()
{
	if (m_nextSceneName.empty()) {
		LOG("Next scene name is empty.");
		return;
	}
	LoadScene(m_nextSceneName);
	Scene* loadedScene = m_loadingSceneFuture.get();
	if (loadedScene)
	{
		m_loadedScenes[m_nextSceneName] = loadedScene;
		m_isLoadSceneComplete = true;
		LOG("Scene loaded: " + m_nextSceneName);
	}
	else 
	{
		LOG("Failed to load scene: " + m_nextSceneName);
		return;
	}
	SwitchScene(m_nextSceneName);
	m_isLoadSceneComplete = false;
}

void GameInstance::SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir)
{
	if (playerIndex < 0 || playerIndex >= MAX_INPUT_DEVICE) {
		LOG("Invalid player index: " + std::to_string(playerIndex));
		return;
	}
	m_playerInputDevices[playerIndex] = { charType, dir };
	LOG("Player " + std::to_string(playerIndex) + " set to CharType: " + 
		std::to_string(static_cast<int>(charType)) + ", PlayerDir: " + std::to_string(static_cast<int>(dir)));
}

void GameInstance::RemovePlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir)
{
	if (playerIndex < 0 || playerIndex >= MAX_INPUT_DEVICE) {
		LOG("Invalid player index: " + std::to_string(playerIndex));
		return;
	}

	auto& current = m_playerInputDevices[playerIndex];

	// 현재 설정된 값과 동일할 때만 해제
	if (current.first == charType && current.second == dir)
	{
		current = { CharType::None, PlayerDir::None };
		LOG("Player " + std::to_string(playerIndex) + " input device removed.");
	}
	else
	{
		LOG("RemovePlayerInputDevice: mismatch, nothing removed for player " + std::to_string(playerIndex));
	}
}
