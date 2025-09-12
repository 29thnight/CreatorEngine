#include "GameManager.h"
#include "pch.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "Entity.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "InputManager.h"
#include "RigidBodyComponent.h"
#include "RaycastHelper.h"
#include "Weapon.h"
#include "EntityResource.h"
#include "EntityItem.h"
#include "DebugLog.h"

int GameManager::m_RewardAmount = 0;
int GameManager::m_player1DeviceID = -1;
int GameManager::m_player2DeviceID = -1;
std::unordered_map<std::string, Scene*> GameManager::m_loadedScenes;

void GameManager::Awake()
{
	//if (!GetOwner()->IsDontDestroyOnLoad())
	//{
	//	SetDontDestroyOnLoad(GetOwner());
	//	LOG("GameManager set DontDestroyOnLoad");
	//}

	LOG("GameManager Awake");
	
	auto resourcePool = GameObject::Find("ResourcePool");
	auto weaponPiecePool = GameObject::Find("WeaponPiecePool");

	if (resourcePool) {
		for (auto& index : resourcePool->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			auto entity = object->GetComponent<EntityItem>();
			if (entity)
				m_resourcePool.push_back(entity);
		}
	}
	else {
		LOG("not assigned resourcePool");
	}
	if (weaponPiecePool) {
		for (auto& index : weaponPiecePool->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			auto entity = object->GetComponent<Weapon>();
			if (entity)
				m_weaponPiecePool.push_back(entity);
		}
	}
	else {
		LOG("not assigned weaponPiecePool");
	}
}

void GameManager::Start()
{
	LOG("GameManager Start");
	//playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Test");
	//playerMap->AddButtonAction("LoadScene", 0, InputType::KeyBoard, static_cast<size_t>(KeyBoard::N), KeyState::Down, [this]() { Inputblabla(); });
	//playerMap->AddButtonAction("LoadScene", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, Loaderererer);
	//playerMap->AddButtonAction("CheatMineResource", 0, InputType::KeyBoard, static_cast<size_t>(KeyBoard::M), KeyState::Down, [this]() { CheatMiningResource();});
	//playerMap->AddValueAction("LoadScene", 0, InputValueType::Float, InputType::KeyBoard, { 'N', 'M' }, [this](float value) {Inputblabla(value);});
}

void GameManager::Update(float tick)
{
	auto cam = GameObject::Find("Main Camera");
	if (!cam) return;

	std::vector<HitResult> hits;
	Quaternion currentRotation = cam->m_transform.GetWorldQuaternion();
	currentRotation.Normalize();
	Vector3 currentForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);

	int size = RaycastAll(cam->m_transform.GetWorldPosition(), currentForward, 10.f, 1u, hits);

	if (m_loadingSceneFuture.valid() && m_loadingSceneFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) 
	{
		Scene* loadedScene = m_loadingSceneFuture.get();
		if (loadedScene) 
		{
			std::string sceneName = loadedScene->m_sceneName.ToString();
			m_loadedScenes[sceneName] = loadedScene;
			LOG("Scene loaded: " + sceneName);
		}
		else {
			LOG("Failed to load scene.");
		}
	}

	if (m_isTestReward)
	{
		AddReward(1);
	}
}

void GameManager::OnDisable()
{
	LOG("GameManager OnDisable");
	playerMap->DeleteAction("LoadScene");
	for (auto& entity : m_entities)
	{
		auto meshrenderer = entity->GetOwner()->GetComponent<MeshRenderer>();
		if (meshrenderer)
		{
			auto material = meshrenderer->m_Material;
			if (material)
			{
				material->m_materialInfo.m_bitflag = 0;
			}
		}
	}
}

void GameManager::LoadScene(const std::string& sceneName)
{
	if (m_loadedScenes.find(sceneName) != m_loadedScenes.end()) {
		LOG("Scene already loaded: " + sceneName);
		return;
	}

	file::path fullPath = PathFinder::Relative("Scenes\\") / std::string(sceneName + ".creator");
	m_loadingSceneFuture = SceneManagers->LoadSceneAsync(fullPath.string());
}

void GameManager::SwitchScene(const std::string& sceneName)
{
	if (m_loadedScenes.find(sceneName) == m_loadedScenes.end()) {
		LOG("Scene not loaded: " + sceneName);
		return;
	}
	SceneManagers->ActivateScene(m_loadedScenes[sceneName]);
}

void GameManager::UnloadScene(const std::string& sceneName)
{
}

void GameManager::LoadTestScene()
{
	LoadScene("Test");
}

void GameManager::SwitchTestScene()
{
	SwitchScene("Test");
}

void GameManager::PushEntity(Entity* entity)
{
	if (entity)
	{
		m_entities.push_back(entity);
	}
	else
	{
		LOG("Entity is null, cannot push to GameManager.");
	}
}

void GameManager::PushPlayer(Entity* player)
{
	m_players.push_back(player);
}

void GameManager::PushAsis(Entity* asis)
{
	m_asis.push_back(asis);
}

const std::vector<Entity*>& GameManager::GetEntities()
{
	return m_entities;
}

const std::vector<Entity*>& GameManager::GetPlayers()
{
	return m_players;
}

const std::vector<Entity*>& GameManager::GetAsis()
{
	return m_asis;
}

std::vector<Entity*>& GameManager::GetResourcePool()
{
	// TODO: 여기에 return 문을 삽입합니다.
	return m_resourcePool;
}

std::vector<Weapon*>& GameManager::GetWeaponPiecePool()
{
	// TODO: 여기에 return 문을 삽입합니다.
	return m_weaponPiecePool;
}

void GameManager::CheatMiningResource()
{
	auto cam = GameObject::Find("Main Camera");
	if (!cam) return;

	HitResult hit;
	Quaternion currentRotation = cam->m_transform.GetWorldQuaternion();
	currentRotation.Normalize();
	Vector3 currentForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);

	bool size = Raycast(cam->m_transform.GetWorldPosition(), currentForward, 10.f, 1, hit);
	for (int i = 0; i < size; i++) {
		LOG(hit.gameObject->m_name.data());
	}

	/*for (auto& resource : m_resourcePool) {
		auto& rb = resource->GetComponent<RigidBodyComponent>();
		resource->GetOwner()->m_transform.SetScale(resource->GetOwner()->m_transform.GetWorldScale() * .3f);
		
		rb.AddForce((Mathf::Vector3::Up + Mathf::Vector3::Backward) * 300.f, EForceMode::IMPULSE);
	}*/
}