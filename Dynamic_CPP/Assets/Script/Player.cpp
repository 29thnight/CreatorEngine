#include "Player.h"
#include "SceneManager.h"
#include "InputActionManager.h"
#include "InputManager.h"
#include "CharacterControllerComponent.h"
#include "Animator.h"
#include "Socket.h"
#include "pch.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "RigidBodyComponent.h"
void Player::Start()
{
	player = GameObject::Find("Punch");

	auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap("Player");
	playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, [this]() { Punch();});
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
	righthand->m_offset = Mathf::Matrix::CreateTranslation(20.f,0.f,0.f) * Mathf::Matrix::CreateScale(0.05f, 0.05f, 0.05f);
	
	
	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard,
	//	{ /*KeyBoard::LeftArrow,KeyBoard::RightArrow,KeyBoard::DownArrow,KeyBoard::UpArrow*/
	//		KeyBoard::UpArrow,KeyBoard::DownArrow,KeyBoard::LeftArrow,KeyBoard::RightArrow,
	//	},
	//	[this](Mathf::Vector2 dir) { Move(dir);});

}

void Player::Update(float tick)
{
	if(m_nearObject)
		m_nearObject->GetComponent<MeshRenderer>()->m_Material->m_materialInfo.m_bitflag = 16;
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


	if (m_nearObject != nullptr)
	{
		player = GameObject::Find("Punch");
		auto animator = player->GetComponent<Animator>();
		Socket* righthand = animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
		righthand->AttachObject(m_nearObject);
		auto rigidbody = m_nearObject->GetComponent<RigidBodyComponent>();
		rigidbody->SetBodyType(EBodyType::STATIC);
		catchedObject = m_nearObject;
		m_nearObject = nullptr;
	}
}

void Player::Throw()
{
	player = GameObject::Find("Punch");
	auto animator = player->GetComponent<Animator>();
	Socket* righthand = animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	righthand->DetachObject(catchedObject);
	auto rigidbody = catchedObject->GetComponent<RigidBodyComponent>();
	rigidbody->SetBodyType(EBodyType::DYNAMIC);
	rigidbody->SetLockAngularX(false);
	rigidbody->SetLockAngularY(false);
	rigidbody->SetLockAngularZ(false);
	rigidbody->SetLockLinearX(false);
	rigidbody->SetLockLinearY(false);
	rigidbody->SetLockLinearZ(false);
	auto& transform = GetOwner()->m_transform;
	auto q = transform.GetWorldMatrix();
	auto rotationOnly = q;
	rotationOnly.r[3] = XMVectorSet(0, 0, 0, 1); // 위치 성분 제거
	rotationOnly.r[0] = XMVector3Normalize(rotationOnly.r[0]); // X축 정규화 (스케일 제거)
	rotationOnly.r[1] = XMVector3Normalize(rotationOnly.r[1]); // Y축 정규화
	rotationOnly.r[2] = XMVector3Normalize(rotationOnly.r[2]); // Z축 정규화

	auto forward = -Mathf::Vector3::TransformNormal(Mathf::Vector3::Forward, rotationOnly);
	//Mathf::Vector3 forward = Mathf::Vector3::Transform(Mathf::Vector3::UnitZ, q);
	//auto forward = -Mathf::Vector3::TransformNormal(Mathf::Vector3::Forward, q);
	forward.Normalize();
	forward = -forward;
	DirectX::SimpleMath::Vector3 upward(0, 1, 0);

	DirectX::SimpleMath::Vector3 throwDir = forward * ThrowPowerX + upward * ThrowPowerY;
	throwDir.Normalize();
	float impulseStrength = 1.0f;
	DirectX::SimpleMath::Vector3 finalImpulse = throwDir * impulseStrength;
	// 4. 힘 적용
	//rigidbody->SetImpulseForce(finalImpulse);
	rigidbody->AddForce({ forward.x * ThrowPowerX ,ThrowPowerY, forward.z * ThrowPowerX }, EForceMode::IMPULSE);
	std::cout << "awdwadadwad" << std::endl;
	catchedObject = nullptr;
	m_nearObject = nullptr; //&&&&&
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

void Player::FindNearObject(GameObject* gameObject)
{
	auto playerPos = GetOwner()->m_transform.GetWorldPosition();
	auto objectPos = gameObject->m_transform.GetWorldPosition();
	XMVECTOR diff = XMVectorSubtract(playerPos, objectPos);
	XMVECTOR distSqVec = XMVector3LengthSq(diff);
	float distance;
	XMStoreFloat(&distance, distSqVec);
	if (m_nearObject == nullptr)
	{
		m_nearObject = gameObject;
		m_nearDistance = distance;
	}
	else
	{
		if (distance < m_nearDistance)
		{
			m_nearObject = gameObject;
			m_nearDistance = distance;
		}
	}
	
}




void Player::OnTriggerEnter(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;


}
void Player::OnTriggerStay(const Collision& collision)
{
	std::cout << "player muunga boodit him trigger" << collision.otherObj->m_name.ToString().c_str() << std::endl;
	if (collision.otherObj->m_tag == "Respawn")
	{
		
	}
	else
	{

		FindNearObject(collision.otherObj);
		
	}
}

void Player::OnTriggerExit(const Collision& collision)
{
	if (m_nearObject == collision.otherObj)
	{
		m_nearObject->GetComponent<MeshRenderer>()->m_Material->m_materialInfo.m_bitflag = 0;
		m_nearObject = nullptr;
		//abc
	}
}

void Player::OnCollisionEnter(const Collision& collision)
{
	std::cout << " Player OnCollisionEnter" << std::endl;
}

void Player::OnCollisionStay(const Collision& collision)
{
	std::cout << "player muunga boodit him" << collision.otherObj->m_name.ToString().c_str() << std::endl;
	if (collision.otherObj->m_tag == "Respawn")
	{

	}
	else
	{
		FindNearObject(collision.otherObj);

	}
}

void Player::OnCollisionExit(const Collision& collision)
{
	if (m_nearObject == collision.otherObj)
	{
		m_nearObject->GetComponent<MeshRenderer>()->m_Material->m_materialInfo.m_bitflag = 0;
		m_nearObject = nullptr;
	}
}
