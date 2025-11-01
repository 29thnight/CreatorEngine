#include "ImageButton.h"
#include "ImageComponent.h"
#include "GameInstance.h"
#include "UIButton.h"
#include "pch.h"
#include "SFXPoolManager.h"
#include "GameManager.h"
void ImageButton::Start()
{
	m_imageComponent = GetComponent<ImageComponent>();

	auto GMobj = GameObject::Find("GameManager");
	if (GMobj)
	{
		GM = GMobj->GetComponent<GameManager>();
	}
	//GameInstance::GetInstance()->LoadScene("LoadingScene");
}

void ImageButton::Update(float tick)
{
	if (!m_uiButton)
	{
		m_uiButton = GetComponent<UIButton>();
		if (m_uiButton)
		{
			m_uiButton->SetClickFunction([&]()
				{
				if (GM)
				{
					auto pool = GM->GetSFXPool();
					if (pool)
					{
						pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("ButtonClick"));
					}
				}
				ClickFunction(); });
		}
	}

	if (m_imageComponent)
	{
		if(!m_isTintChange)
		{
			if (m_imageComponent->IsNavigationThis())
			{
				if(m_imageComponent->IsEnabled()) 
				{
					return;
				}

				if (GM)
				{
					auto pool = GM->GetSFXPool();
					if (pool)
					{
						pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("ButtonSelect"));
					}
				}
				m_imageComponent->SetEnabled(true);
			}
			else
			{
				m_imageComponent->SetEnabled(false);
			}
		}
		else
		{
			if (m_imageComponent->IsNavigationThis())
			{
				if (m_imageComponent->color == Mathf::Color4(1.f, 1.f, 1.f, 1.f))
				{
					return;
				}

				if (GM)
				{
					auto pool = GM->GetSFXPool();
					if (pool)
					{
						pool->PlayOneShot(GameInstance::GetInstance()->GetSoundName()->GetSoudNameRandom("ButtonSelect"));
					}
				}
				m_imageComponent->color = Mathf::Color4(1.f, 1.f, 1.f, 1.f);
			}
			else
			{
				m_imageComponent->color = Mathf::Color4(0.8f, 0.8f, 0.8f, 1.f);
			}
		}
	}
}

void ImageButton::ClickFunction()
{
	//테스트 기본
	GameInstance::GetInstance()->SwitchScene("CharSelectScene");
}

