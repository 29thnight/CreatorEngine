#include "GameInit.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "ImageSlideshow.h"
#include "ImageComponent.h"
#include "Canvas.h"
#include "pch.h"

void GameInit::Start()
{
	GameObject* gameManagerObj = GameObject::Find("GameManager");
	GameObject* logoObj = GameObject::Find("GameInstituteLogo");
	GameObject* bootBgObj = GameObject::Find("BootstrapBG");
	if(gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
		LoadNextScene();
	}

	if (logoObj)
	{
		m_slideshowComp = logoObj->GetComponent<ImageSlideshow>();
	}

	if (bootBgObj)
	{
		m_bootBg = bootBgObj->GetComponent<ImageComponent>();
	}
}

void GameInit::Update(float tick)
{
	if (!m_slideshowComp || !m_bootBg) return;

	if (0 <= elapsed && m_slideshowComp->IsFinished())
	{
		elapsed += tick;
		float t = std::clamp(elapsed / m_fadeDuration, 0.f, 1.f);

		// RGB�� �������� ����, ���Ĵ� ����
		const auto c0 = m_bootBgStartColor;
		const Mathf::Color4 target{ 0.f, 0.f, 0.f, c0.w };

		// ������ Lerp�� Color4 �����ε� ���ٸ� ������Ʈ-wise��
		Mathf::Color4 out{
			Mathf::Lerp(c0.x, target.x, t),
			Mathf::Lerp(c0.y, target.y, t),
			Mathf::Lerp(c0.z, target.z, t),
			Mathf::Lerp(c0.w, target.w, t) // ���ĵ� ���� �����ϰ� �ʹٸ� �� �� ����
		};
		m_bootBg->color = out;

		if (t >= 1.f) 
		{ 
			elapsed = -1;
			auto canvas = m_bootBg->GetOwnerCanvas();
			if (canvas)
			{
				canvas->SetCanvasOrder(0);
			}
		}
	}
}

void GameInit::LoadNextScene()
{
	m_gameManager->LoadNextScene();
}
