#include "TestBehavior.h"
#include "InputManager.h"
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
	auto Player = GameObject::Find("Punch");
	if (Player)
	{
		Transform* playerTransform = Player->GetComponent<Transform>();
		if(playerTransform)
		{
			Mathf::Vector3 playerPosition = playerTransform->GetWorldPosition();
			Mathf::Vector3 followDistance = { 24.f, 35.f, 15.f };
			
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
