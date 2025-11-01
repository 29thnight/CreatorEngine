#include "ClearPortal.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "InputManager.h"
#include "EffectComponent.h"
#include "SceneManager.h"
void ClearPortal::Start()
{
	auto GMObj = GameObject::Find("GameManager");

	if (GMObj)
	{
		m_gameManager = GMObj->GetComponent<GameManager>();
	}

	auto portalObj = SceneManagers->GetActiveScene()->CreateGameObject("portalEffect", GameObjectType::Empty,GetOwner()->m_index).get();
	if (portalObj)
	{
		m_portalEffect = portalObj->AddComponent<EffectComponent>();
		portalObj->GetComponent<Transform>()->AddPosition({ 0,2.5,0 });
		m_portalEffect->m_effectTemplateName = "portaloff";
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

	TriggerPortal();
	//portalReady가 false -> true되는 타이밍에 포탈UI등 표시
	if (InPortal2)
	{

		//모델 깜빡이는 연출등
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

void ClearPortal::TriggerPortal()
{
	if(isSwitching)  return; //씬 체인지중이면 리턴

	if (playerCount >= 2)
	{
		InPortal2 = true;
	}
	else
	{
		InPortal2 = false;
	}
	if (preInPortal2 != InPortal2)  //전이랑 후랑 다르면 포탈체인지
	{
		preInPortal2 = InPortal2;

		if (InPortal2) //둘다 활성화면
		{
			if (m_portalEffect)
			{
				m_portalEffect->m_effectTemplateName = "portal";
				m_portalEffect->Apply();
			}
		}
		else  //내려갔으면
		{
			if (m_portalEffect)
			{
				m_portalEffect->m_effectTemplateName = "portaloff";
				m_portalEffect->Apply();
			}
		}
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

