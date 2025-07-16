#include "TestBehavior.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "InputManager.h"
#include "InputActionManager.h"
#include "pch.h"
#include <cmath>

void TestBehavior::Start()
{
	// Initialize the behavior
	// This is where you can set up any initial state or properties for the behavior
	// For example, you might want to set the position, rotation, or scale of the object
	// that this behavior is attached to.
	// You can also use this method to register any event listeners or perform any other
	// setup tasks that are needed before the behavior starts running.
	auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Player");
	playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard, { 'A', 'D', 'S', 'W'},
		[this](Mathf::Vector2 _vector2) {Move(_vector2);});
}

void TestBehavior::FixedUpdate(float fixedTick)
{
}

void TestBehavior::OnTriggerEnter(const Collision& collider)
{
}

void TestBehavior::OnTriggerStay(const Collision& collider)
{
}

void TestBehavior::OnTriggerExit(const Collision& collider)
{
}

void TestBehavior::OnCollisionEnter(const Collision& collider)
{
}

void TestBehavior::OnCollisionStay(const Collision& collider)
{
}

void TestBehavior::OnCollisionExit(const Collision& collider)
{
}

void TestBehavior::Update(float tick)
{
	SceneManagers->IsEditorSceneLoaded();

	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	Mathf::Vector3 dir = { moveDir.x, 0.f, moveDir.y };
	GetOwner()->m_transform.SetPosition(pos + 5.f * dir * tick);

	auto Player = GameObject::Find("Punch");
	if (Player)
	{
		Transform* playerTransform = Player->GetComponent<Transform>();
		if(playerTransform)
		{
			Mathf::Vector3 playerPosition = playerTransform->GetWorldPosition();
			Mathf::Vector3 followDistance = { 33.f, 50.f, 15.f };
			
			GetComponent<Transform>().SetPosition(playerPosition + followDistance);
		}
		else
		{
			std::cout << "Player Transform component not found." << std::endl;
		}
	}
	else
	{
		std::cout << "Player GameObject not found." << std::endl;
	}
}

void TestBehavior::LateUpdate(float tick)
{
}

void TestBehavior::Move(Mathf::Vector2 value)
{
	//std::cout << value.x << ", " << value.y << std::endl; 
	moveDir.x = value.x;
	moveDir.y = value.y;
}
