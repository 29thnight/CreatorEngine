#include "ClearPortal.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "InputManager.h"
void ClearPortal::Start()
{
	auto GMObj = GameObject::Find("GameManager");

	if (GMObj)
	{
		m_gameManager = GMObj->GetComponent<GameManager>();
	}
}

void ClearPortal::OnTriggerEnter(const Collision& collision)
{
	Player* p = collision.otherObj->GetComponent<Player>();
	if (p)
	{
		playerCount++;
	}
}

void ClearPortal::OnTriggerStay(const Collision& collision)
{

}

void ClearPortal::OnTriggerExit(const Collision& collision)
{
	Player* p = collision.otherObj->GetComponent<Player>();
	if (p)
	{
		playerCount--;
	}
}

void ClearPortal::Update(float tick)
{
	if (!isPortalReady) return;

	//portalReady°¡ false -> trueµÇ´Â Å¸ÀÌ¹Ö¿¡ Æ÷Å»UIµî Ç¥½Ã
	if (playerCount >= 2)
	{

		//¸ðµ¨ ±ôºýÀÌ´Â ¿¬Ãâµî
		if (InputManagement->IsControllerButtonDown(0, ControllerButton::A) || InputManagement->IsControllerButtonDown(1, ControllerButton::A))
		{
			SwitchScene();
		}

		
	}

	if (isSwitching)
	{
		if(m_gameManager)
			m_gameManager->SwitchNextSceneWithFade();
	}
}

void ClearPortal::SwitchScene()
{
	if (isSwitching|| !m_gameManager) return;
	auto curScene = GameInstance::GetInstance()->GetCurrentSceneType();
	
	switch (static_cast<SceneType>(curScene))
	{
	case SceneType::Tutorial:
		m_gameManager->m_nextSceneIndex = (int)SceneType::Stage;
		break;
	case SceneType::Stage:
		m_gameManager->SavePlayerData();
		m_gameManager->m_nextSceneIndex = (int)SceneType::Boss;
		break;
	default:
		m_gameManager->m_nextSceneIndex = (int)SceneType::Stage;
		break;
	}
	m_gameManager->m_isLoadingReq = true;
	m_gameManager->LoadNextScene();

	isSwitching = true;
	
}

