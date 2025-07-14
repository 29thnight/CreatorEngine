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
void GameManager::Awake()
{
	std::cout << "GameManager Awake" << std::endl;
	
	auto resourcePool = GameObject::Find("ResourcePool");
	auto weaponPiecePool = GameObject::Find("WeaponPiecePool");

	if (resourcePool) {
		for (auto& index : resourcePool->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			auto entity = object->GetComponent<Entity>();
			if (entity)
				m_resourcePool.push_back(entity);
		}
	}
	else {
		std::cout << "not assigned resourcePool" << std::endl;
	}
	if (weaponPiecePool) {
		for (auto& index : weaponPiecePool->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			auto entity = object->GetComponent<Entity>();
			if (entity)
				m_weaponPiecePool.push_back(entity);
		}
	}
	else {
		std::cout << "not assigned weaponPiecePool" << std::endl;
	}
}

inline static void Loaderererer() {
	std::cout << "Loader" << std::endl;
}
void GameManager::Start()
{
	std::cout << "GameManager Start" << std::endl;
	playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Test");
	playerMap->AddButtonAction("LoadScene", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, [this]() { Inputblabla(); });
	//playerMap->AddButtonAction("LoadScene", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, Loaderererer);
	playerMap->AddButtonAction("CheatMineResource", 0, InputType::KeyBoard, KeyBoard::M, KeyState::Down, [this]() { CheatMiningResource();});
	//playerMap->AddValueAction("LoadScene", 0, InputValueType::Float, InputType::KeyBoard, { 'N', 'M' }, [this](float value) {Inputblabla(value);});
}

void GameManager::Update(float tick)
{
}

void GameManager::OnDisable()
{
	std::cout << "GameManager OnDisable" << std::endl;
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
	
}

void GameManager::PushEntity(Entity* entity)
{
	if (entity)
	{
		m_entities.push_back(entity);
	}
	else
	{
		std::cout << "Entity is null, cannot push to GameManager." << std::endl;
	}
}

const std::vector<Entity*>& GameManager::GetEntities()
{
	return m_entities;
}

void GameManager::CheatMiningResource()
{
	for (auto& resource : m_resourcePool) {
		auto& rb = resource->GetComponent<RigidBodyComponent>();
		rb.AddForce((Mathf::Vector3::Up + Mathf::Vector3::Backward) * 100.f, EForceMode::IMPULSE);
	}
}

void GameManager::Inputblabla()
{
	LoadScene("SponzaTest.creator");
}

