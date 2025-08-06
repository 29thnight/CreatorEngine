#include "TestEffect.h"
#include "pch.h"
#include "EffectComponent.h"
#include "InputManager.h"
#include "SceneManager.h"
#include "Core.Random.h"

void TestEffect::Start()
{
	//for (int i = 0; i < 100; ++i) {
	//	auto obj = SceneManagers->GetActiveScene()->CreateGameObject(std::to_string(i));
	//	auto pos = m_pOwner->m_transform.GetWorldPosition();
	//	obj->m_transform.SetPosition(pos);
	//	auto objE = obj->AddComponent<EffectComponent>();
	//	objE->Awake();
	//	objE->PlayEffectByName("Dash4");
	//}
}

void TestEffect::Update(float tick)
{
	//currentT += tick;
	//
	//currentT += tick;
	//if (currentT > 0.1f)
	//{
	//	auto pos = m_pOwner->m_transform.GetWorldPosition();
	//	for (int i = 0; i < 100; ++i)
	//	{
	//		Random<float> randX(-10.0f, 10.0f);
	//		Random<float> randY(0.0f, 5.0f);
	//		Random<float> randZ(-10.0f, 10.0f);
	//		float x = randX();
	//		float y = randY();
	//		float z = randZ();
	//
	//		Mathf::Vector3 randomPos = static_cast<Mathf::Vector3>(pos) + Mathf::Vector3(x, y, z);
	//		GameObject::Find(std::to_string(i))->m_transform.SetPosition(randomPos);
	//	}
	//	currentT = 0.0f;
	//}
	//
	//
	//if(InputManagement->IsKeyDown(KeyBoard::K))
	//{
	//	auto comp = GetOwner()->GetComponent<EffectComponent>()->m_isEnabled = false;
	//}
	//
	//if (InputManagement->IsKeyPressed(KeyBoard::LeftArrow))
	//{
	//	GetOwner()->m_transform.AddPosition(Mathf::Vector3(-0.015, 0, 0));
	//}
	//
	//if (InputManagement->IsKeyPressed(KeyBoard::RightArrow))
	//{
	//	GetOwner()->m_transform.AddPosition(Mathf::Vector3(0.015, 0, 0));
	//}
	//
	//if (InputManagement->IsKeyPressed(KeyBoard::UpArrow))
	//{
	//	GetOwner()->m_transform.AddPosition(Mathf::Vector3(0, 0, 0.015));
	//}
	//
	//if (InputManagement->IsKeyPressed(KeyBoard::DownArrow))
	//{
	//	GetOwner()->m_transform.AddPosition(Mathf::Vector3(0, 0, -0.015));
	//}

}

