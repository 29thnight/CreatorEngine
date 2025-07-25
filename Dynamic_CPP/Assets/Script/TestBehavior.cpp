#include "TestBehavior.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "InputManager.h"
#include "InputActionManager.h"
#include "EffectComponent.h"
#include "pch.h"
#include <cmath>
GameObject* testObject = nullptr;
EffectComponent* effectComponent = nullptr;

void TestBehavior::Start()
{
	// Initialize the behavior
	// This is where you can set up any initial state or properties for the behavior
	// For example, you might want to set the position, rotation, or scale of the object
	// that this behavior is attached to.
	// You can also use this method to register any event listeners or perform any other
	// setup tasks that are needed before the behavior starts running.



	testObject = GetOwner();
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
}

void TestBehavior::LateUpdate(float tick)
{
}

void TestBehavior::Test()
{
	std::cout << "Test input" << std::endl;
}

void TestBehavior::Move(Mathf::Vector2 value)
{
}
