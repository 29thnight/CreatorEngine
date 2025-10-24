#include "TestEffect.h"
#include "pch.h"
#include "InputManager.h"
#include "EffectComponent.h"
#include "SceneManager.h"

void TestEffect::Start()
{
	if (!m_Effect)
	{
		m_Effect = SceneManagers->GetActiveScene()->CreateGameObject("asd").get();
		m_Effect->AddComponent<EffectComponent>()->Awake();
		m_Effect->GetComponent<EffectComponent>()->m_effectTemplateName = "1";
	}

}

void TestEffect::Update(float tick)
{
	if (InputManagement->IsKeyDown(KeyBoard::V))
	{
		m_Effect->GetComponent<EffectComponent>()->Apply();
	}
}

