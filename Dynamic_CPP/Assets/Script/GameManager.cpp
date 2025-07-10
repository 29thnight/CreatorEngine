#include "GameManager.h"
#include "pch.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "Entity.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MaterialInfomation.h"
#include "InputManager.h"
void GameManager::Awake()
{
	std::cout << "GameManager Awake" << std::endl;

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
	playerMap->AddButtonAction("LODADADW", 0, InputType::KeyBoard, KeyBoard::M, KeyState::Down, [this]() { std::cout << "adsqawdadad" << std::endl;});
	//playerMap->AddValueAction("LoadScene", 0, InputValueType::Float, InputType::KeyBoard, { 'N', 'M' }, [this](float value) {Inputblabla(value);});
}

void GameManager::Update(float tick)
{
}

void GameManager::OnDisable()
{
	std::cout << "GameManager OnDisable" << std::endl;
	playerMap->DeleteAction("LoadScene");
	for (auto& entity : m_Entities)
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
		m_Entities.push_back(entity);
	}
	else
	{
		std::cout << "Entity is null, cannot push to GameManager." << std::endl;
	}
}

void GameManager::Inputblabla()
{
	LoadScene("SponzaTest.creator");
}

