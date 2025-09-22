#include "GameInstance.h"
#include "SceneManager.h"
#include "DebugLog.h"
#include "pch.h"

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