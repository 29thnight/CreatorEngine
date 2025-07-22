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
	playerMap->AddButtonAction("LoadScene", 0, InputType::KeyBoard, static_cast<size_t>(KeyBoard::N), KeyState::Down, [this]() { Inputblabla(); });
	//playerMap->AddButtonAction("LoadScene", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, Loaderererer);
	playerMap->AddButtonAction("CheatMineResource", 0, InputType::KeyBoard, static_cast<size_t>(KeyBoard::M), KeyState::Down, [this]() { CheatMiningResource();});
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

std::vector<Entity*>& GameManager::GetResourcePool()
{
	// TODO: 여기에 return 문을 삽입합니다.
	return m_resourcePool;
}

std::vector<Entity*>& GameManager::GetWeaponPiecePool()
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
		std::cout << hit.hitObject->m_name.data() << std::endl;
	}

	/*for (auto& resource : m_resourcePool) {
		auto& rb = resource->GetComponent<RigidBodyComponent>();
		resource->GetOwner()->m_transform.SetScale(resource->GetOwner()->m_transform.GetWorldScale() * .3f);
		
		rb.AddForce((Mathf::Vector3::Up + Mathf::Vector3::Backward) * 300.f, EForceMode::IMPULSE);
	}*/
}

void GameManager::Inputblabla()
{
	LoadScene("SponzaTest.creator");
}

