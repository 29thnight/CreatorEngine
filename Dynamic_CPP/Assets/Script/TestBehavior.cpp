#include "TestBehavior.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "InputManager.h"
#include "InputActionManager.h"

#include <cmath>

#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"
#include "pch.h"


BoxColliderComponent* boxCollider = nullptr;
RigidBodyComponent* rigidBody = nullptr;

void TestBehavior::Start()
{
	// Initialize the behavior
	// This is where you can set up any initial state or properties for the behavior
	// For example, you might want to set the position, rotation, or scale of the object
	// that this behavior is attached to.
	// You can also use this method to register any event listeners or perform any other
	// setup tasks that are needed before the behavior starts running.
	
	boxCollider = m_pOwner->GetComponent<BoxColliderComponent>();
	rigidBody = m_pOwner->GetComponent<RigidBodyComponent>();
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
	m_chargingTime += tick;
	m_chargingTime > 20.f ? m_chargingTime = 0.f : m_chargingTime;

	if (rigidBody)
	{
		bool istriger = rigidBody->IsTrigger();
		if (istriger)
		{
			std::cout << " state : trigger" << std::endl;
		}
		else {
			std::cout << " state : collision" << std::endl;
		}

		std::cout << " testValue : " << testValue << std::endl;
		std::cout << " m_chargingTime : " << m_chargingTime << std::endl;

		if (testValue< m_chargingTime)
		{
			std::cout << " testValue < m_chargingTime" << std::endl;
			if (!istriger)
			{
				std::cout << " set trigger" << std::endl;
				rigidBody->SetIsTrigger(true);
				
			}
		}

		if (testValue> m_chargingTime)
		{

			if (istriger)
			{
				rigidBody->SetIsTrigger(false);
			}
		}

	}


}

void TestBehavior::LateUpdate(float tick)
{
}

void TestBehavior::Move(Mathf::Vector2 value)
{
}
