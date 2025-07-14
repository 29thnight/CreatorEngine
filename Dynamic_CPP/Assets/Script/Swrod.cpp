#include "Swrod.h"
#include "pch.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "InputActionManager.h"
#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"
void Swrod::Start()
{
	std::cout << "gum start!!!" << std::endl;
	GameObject* sword = GameObject::Find("Sword");
	//auto box = sword->GetComponent<BoxColliderComponent>();
	//box->SetExtents({ 6,3,24 });
	//box->SetColliderType(EColliderType::TRIGGER);
	//auto rigidbody = sword->GetComponent<RigidBodyComponent>();
	//rigidbody->SetLockAngularX(true);
	//rigidbody->SetLockAngularY(true);
	//rigidbody->SetLockAngularZ(true);
	//rigidbody->SetLockLinearX(true);
	//rigidbody->SetLockLinearY(true);
	//rigidbody->SetLockLinearZ(true);
}

void Swrod::OnTriggerEnter(const Collision& collision)
{
	std::cout << "gum trigger enter!!!" << std::endl;
}

void Swrod::OnTriggerStay(const Collision& collision)
{
	std::cout << "gum trigger stay!!!" << std::endl;
}

void Swrod::OnTriggerExit(const Collision& collision)
{
	std::cout << "gum trigger exit!!!" << std::endl;
}

void Swrod::OnCollisionEnter(const Collision& collision)
{
	std::cout << "gum OnCollisionEnter !!!" << std::endl;
}

void Swrod::OnCollisionStay(const Collision& collision)
{
	std::cout << "gum OnCollisionStay !!!" << std::endl;
}

void Swrod::OnCollisionExit(const Collision& collision)
{
	std::cout << "gum OnCollisionExit !!!" << std::endl;
}


void Swrod::Update(float tick)
{
}

