#include "Player.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "InputManager.h"
#include "CharacterControllerComponent.h"
#include "Animator.h"
#include "Socket.h"
#include "pch.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
void Player::Start()
{
	player = GameObject::Find("Punch");

	auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Player");
	playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, [this]() { Punch();});
	//player->GetComponent<RigidBodyComponent>();

	playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::GamePad, { static_cast<size_t>(ControllerButton::LEFT_Thumbstick) },
		[this](Mathf::Vector2 _vector2) {Move(_vector2);});
	playerMap->AddButtonAction("Attack", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::X), KeyState::Down, [this]() {  Attack();});
	playerMap->AddButtonAction("AttackCharging", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::X), KeyState::Pressed, [this]() {});
	playerMap->AddButtonAction("ChargeAttack", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::X), KeyState::Released, [this]() {});
	playerMap->AddButtonAction("Dash", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::B), KeyState::Down, [this]() {  });
	playerMap->AddButtonAction("CatchAndThrow", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::A), KeyState::Down, [this]() {CatchAndThrow();});
	playerMap->AddButtonAction("SwapWeaponLeft", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::LEFT_SHOULDER), KeyState::Down, [this]() {SwapWeaponLeft();});
	playerMap->AddButtonAction("SwapWeaponRight", 0, InputType::GamePad, static_cast<size_t>(ControllerButton::RIGHT_SHOULDER), KeyState::Down, [this]() {SwapWeaponRight();});
	auto animator = player->GetComponent<Animator>();
	Socket* righthand = animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	//righthand->m_offset= DirectX::SimpleMath::Matrix::CreateTranslation(50.0f, 0.0f, 0.0f);
	
	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard,
	//	{ /*KeyBoard::LeftArrow,KeyBoard::RightArrow,KeyBoard::DownArrow,KeyBoard::UpArrow*/
	//		KeyBoard::UpArrow,KeyBoard::DownArrow,KeyBoard::LeftArrow,KeyBoard::RightArrow,
	//	},
	//	[this](Mathf::Vector2 dir) { Move(dir);});

}

void Player::Update(float tick)
{

	static float elasepdTime = 0.f;
	elasepdTime += tick;
	if (elasepdTime >= 4.f)
	{
		//SceneManagers->GetActiveScene()->CreateGameObject("newenwenw");
		elasepdTime = 0;
	}


}

void Player::Move(Mathf::Vector2 dir)
{
	//실행끝나도 Move함수에 대한 bind는 남아서 지울껀지 플래그처리할껀지 &&&&&
	player = GameObject::Find("Punch");
	auto controller = player->GetComponent<CharacterControllerComponent>();
	if (!controller) return;
	controller->Move(dir);
	auto animator = player->GetComponent<Animator>();
	if (controller->IsOnMove())
	{
		animator->SetParameter("OnMove", true);
	}
	else
	{
		animator->SetParameter("OnMove", false);
	}
}

void Player::CatchAndThrow()
{
	if (catchedObject)
	{
		Throw();
	}
	else
	{
		Catch();
	}
}

void Player::Catch()
{
	/*if (m_nearObject != nullptr)
	{ 
		player = GameObject::Find("Punch");
		auto animator = player->GetComponent<Animator>();
		Socket* righthand = animator->MakeSocket("RightHand", "mixamorig:RightHand");
		m_nearObject = GameObject::Find("plane");
		righthand->AttachObject(m_nearObject);
		catchedObject = m_nearObject;
	}*/

	player = GameObject::Find("Punch");
	auto animator = player->GetComponent<Animator>();
	Socket* righthand = animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	m_nearObject = GameObject::Find("Sting-Sword lowpoly");
	righthand->AttachObject(m_nearObject);
	catchedObject = m_nearObject;
}

void Player::Throw()
{
	player = GameObject::Find("Punch");
	auto animator = player->GetComponent<Animator>();
	Socket* righthand = animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	righthand->DetachObject(catchedObject);
	catchedObject = nullptr;
}

void Player::Attack()
{
	player = GameObject::Find("Punch");
	auto animator = player->GetComponent<Animator>();
	animator->SetParameter("Attack", true);
}

void Player::SwapWeaponLeft()
{
	m_weaponIndex--;
	//std::cout << "left weapon equipped" << std::endl;
	if (m_curWeapon != nullptr)
	{
		m_curWeapon->SetEnabled(false);
		m_curWeapon = m_weaponInventory[m_weaponIndex];
		m_curWeapon->SetEnabled(true);
	}
}

void Player::SwapWeaponRight()
{
	m_weaponIndex++;
	//std::cout << "right weapon equipped" << std::endl;
	if (m_curWeapon != nullptr)
	{
		m_curWeapon->SetEnabled(false);
		m_curWeapon = m_weaponInventory[m_weaponIndex];
		m_curWeapon->SetEnabled(true);
	}
}

void Player::Punch()
{
	std::cout << "ppppuuuunchhhhhhh" << std::endl;
}

void Player::OnCollisionEnter(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;

	

}

void Player::OnCollisionStay(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;

	if (collision.otherObj->ToString() == "Sting-Sword lowpoly")
	{
		m_nearObject = collision.otherObj;
		std::cout << "Sting-Sword lowpoly collision" << std::endl;
	}
	else
	{
		m_nearObject = nullptr;
	}
}

void Player::OnTriggerEnter(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;


	//m_nearObject = collision.otherObj;
	std::cout << collision.otherObj->ToString() << std::endl;
	

}
void Player::OnTriggerStay(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;

	if (collision.otherObj->ToString() == "Sting-Sword lowpoly")
	{
		m_nearObject = collision.otherObj;
		std::cout << "Sting-Sword lowpoly trigger" << std::endl;
	}
	else
	{
		m_nearObject = nullptr;
	}

}
