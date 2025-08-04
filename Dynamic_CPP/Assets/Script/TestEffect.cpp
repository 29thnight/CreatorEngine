#include "TestEffect.h"
#include "pch.h"
#include "EffectComponent.h"
#include "InputManager.h"
#include "SceneManager.h"
#include "Core.Random.h"

void TestEffect::Start()
{
}

void TestEffect::Update(float tick)
{
	Random<float> randX(-10.0f, 10.0f);
	Random<float> randY(0.0f, 5.0f);
	Random<float> randZ(-10.0f, 10.0f);

	if (InputManagement->IsKeyDown(KeyBoard::P))
	{
		float x = randX();  // ¶Ç´Â randX.Generate();
		float y = randY();
		float z = randZ();

		auto obj = SceneManagers->GetActiveScene()->CreateGameObject("asd");
		obj->m_transform.SetPosition(Mathf::Vector3(x, y, z));
		auto objE = obj->AddComponent<EffectComponent>();
		objE->Awake();
		objE->PlayEffectByName("Dash2");
	}

	if(InputManagement->IsKeyDown(KeyBoard::K))
	{
		auto comp = GetOwner()->GetComponent<EffectComponent>()->m_isEnabled = false;
	}

	if (InputManagement->IsKeyPressed(KeyBoard::LeftArrow))
	{
		GetOwner()->m_transform.AddPosition(Mathf::Vector3(-0.05, 0, 0));
	}

	if (InputManagement->IsKeyPressed(KeyBoard::RightArrow))
	{
		GetOwner()->m_transform.AddPosition(Mathf::Vector3(0.05, 0, 0));
	}

	if (InputManagement->IsKeyPressed(KeyBoard::UpArrow))
	{
		GetOwner()->m_transform.AddPosition(Mathf::Vector3(0, 0, 0.05));
	}

	if (InputManagement->IsKeyPressed(KeyBoard::DownArrow))
	{
		GetOwner()->m_transform.AddPosition(Mathf::Vector3(0, 0, -0.05));
	}

}

