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

void GameManager::Awake()
{
	LOG("GameManager Awake");
	//앞으론 언리얼 처럼 게임인스턴스를 활용해서 전역 설정값 관리
	GameInstance::GetInstance();
	
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

	GameInstance::GetInstance()->AsyncSceneLoadUpdate();

	//테스트용 보상 코드
	static float rewardTimer = 0.f;
	rewardTimer += tick;

	if(rewardTimer >= 1.f)
	{
		rewardTimer = 0.f;
		//1초마다 보상
		if (m_isTestReward)
		{
			int reward = GetReward();
			if (reward < 99)
			{
				AddReward(1);
			}
			else
			{
				InitReward(0);
			}
		}
	}
}

void GameManager::OnDisable()
{
	LOG("GameManager OnDisable");
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
	GameInstance::GetInstance()->LoadScene(sceneName);
}

void GameManager::SwitchScene(const std::string& sceneName)
{
	GameInstance::GetInstance()->SwitchScene(sceneName);
}

void GameManager::UnloadScene(const std::string& sceneName)
{
	GameInstance::GetInstance()->UnloadScene(sceneName);
}

void GameManager::SetPlayerInputDevice(int playerIndex, CharType charType, PlayerDir dir)
{
	if (playerIndex < 0 || playerIndex >= MAX_INPUT_DEVICE) return;
	GameInstance::GetInstance()->SetPlayerInputDevice(playerIndex, charType, dir);
}

void GameManager::LoadTestScene()
{
	LoadScene("CreateUIPrefabV2");
}

void GameManager::SwitchTestScene()
{
	SwitchScene("CreateUIPrefabV2");
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

void GameManager::InitReward(int amount)
{
	GameInstance::GetInstance()->SetRewardAmount(amount);
}

void GameManager::AddReward(int amount)
{
	GameInstance::GetInstance()->AddRewardAmount(amount);
}

int GameManager::GetReward()
{
	return GameInstance::GetInstance()->GetRewardAmount();
}
