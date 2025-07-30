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
#include "EntityItem.h"
#include "RaycastHelper.h"
#include "Skeleton.h"

#include "EffectComponent.h"
#include "TestEnemy.h"
#include "EffectComponent.h"
#include "BoxColliderComponent.h"
#include "GameManager.h"
void Player::Start()
{
	player = GetOwner();
	auto childred = player->m_childrenIndices;
	for (auto& child : childred)
	{
		auto animator = GameObject::FindIndex(child)->GetComponent<Animator>();
		if (animator)
		{
			m_animator = animator;
			break;
		}

	}
	if (!m_animator)
	{
		m_animator = player->GetComponent<Animator>();
	}
	//pad
	std::string MapName = "Player" + std::to_string(playerIndex);
	auto playerMap = SceneManagers->GetInputActionManager()->AddActionMap(MapName);
	//playerMap->AddButtonAction("Punch", 0, InputType::KeyBoard, KeyBoard::N, KeyState::Down, [this]() { Punch();});
	playerMap->AddValueAction("Move", playerIndex, InputValueType::Vector2, InputType::GamePad, { static_cast<size_t>(ControllerButton::LEFT_Thumbstick) },
		[this](Mathf::Vector2 _vector2) {Move(_vector2);});
	playerMap->AddButtonAction("StartAttack", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::X), KeyState::Down, [this]() {  StartAttack();});
	playerMap->AddButtonAction("AttackCharging", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::X), KeyState::Pressed, [this]() { Charging();});
	playerMap->AddButtonAction("Attack", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::X), KeyState::Released, [this]() { Attack();});
	playerMap->AddButtonAction("Dash", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::B), KeyState::Down, [this]() { Dash(); });
	playerMap->AddButtonAction("CatchAndThrow", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::A), KeyState::Down, [this]() {CatchAndThrow();});
	playerMap->AddButtonAction("DeleteWeapone", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::Y), KeyState::Down, [this]() {DeleteCurWeapon();});
	playerMap->AddButtonAction("SwapWeaponLeft", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::LEFT_SHOULDER), KeyState::Down, [this]() {SwapWeaponLeft();});
	playerMap->AddButtonAction("SwapWeaponRight", playerIndex, InputType::GamePad, static_cast<size_t>(ControllerButton::RIGHT_SHOULDER), KeyState::Down, [this]() {SwapWeaponRight();});
	playerMap->AddButtonAction("knockback", 0, InputType::KeyBoard, 'O', KeyState::Down, [this]() {TestKnockBack();});
	playerMap->AddButtonAction("stun", 0, InputType::KeyBoard, 'P', KeyState::Down, [this]() {TestStun();});
	//keyboard

	playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard, { 'A', 'D', 'S', 'W' },
		[this](Mathf::Vector2 _vector2) {Move(_vector2);});
	playerMap->AddButtonAction("Attack", 0, InputType::KeyBoard, 'K', KeyState::Down, [this]() {  Attack();});
	playerMap->AddButtonAction("AttackCharging", 0, InputType::KeyBoard, 'K', KeyState::Pressed, [this]() {});
	playerMap->AddButtonAction("ChargeAttack", 0, InputType::KeyBoard, 'K', KeyState::Released, [this]() {});
	playerMap->AddButtonAction("Dash", 0, InputType::KeyBoard, 'L', KeyState::Down, [this]() {Dash();});
	playerMap->AddButtonAction("CatchAndThrow", 0, InputType::KeyBoard, 'J', KeyState::Down, [this]() {CatchAndThrow();});
	playerMap->AddButtonAction("SwapWeaponLeft", 0, InputType::KeyBoard, 'Q', KeyState::Down, [this]() {SwapWeaponLeft();});
	playerMap->AddButtonAction("SwapWeaponRight", 0, InputType::KeyBoard, 'P', KeyState::Down, [this]() {SwapWeaponRight();});


	//m_animator = player->GetComponent<Animator>();
	//Socket* righthand = m_animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	//righthand->DetachAllObject();
	//righthand->m_offset = Mathf::Matrix::CreateTranslation(0.f,0.f,0.f) * Mathf::Matrix::CreateScale(0.015f, 0.015f, 0.015f);
	
	player->m_collisionType = 2;
	//playerMap->AddValueAction("Move", 0, InputValueType::Vector2, InputType::KeyBoard,
	//	{ /*KeyBoard::LeftArrow,KeyBoard::RightArrow,KeyBoard::DownArrow,KeyBoard::UpArrow*/
	//		KeyBoard::UpArrow,KeyBoard::DownArrow,KeyBoard::LeftArrow,KeyBoard::RightArrow,
	//	},
	//	[this](Mathf::Vector2 dir) { Move(dir);});
	//m_animator->m_Skeleton->m_animations[3].SetEvent("Player", "OnPunch", 0.353);


	GameManager* gm = GameObject::Find("GameManager")->GetComponent<GameManager>();
	gm->PushEntity(this);
	gm->PushPlayer(this);

	camera = GameObject::Find("Main Camera");
}

void Player::Update(float tick)
{
	if (catchedObject)
	{
		auto forward = GetOwner()->m_transform.GetForward(); // Vector3
		auto world = GetOwner()->m_transform.GetWorldPosition(); // XMVECTOR

		XMVECTOR forwardVec = XMLoadFloat3(&forward); // Vector3 → XMVECTOR

		XMVECTOR offsetPos = world + forwardVec * 3.0f;
		offsetPos.m128_f32[1] = 3.0f; // Y 고정

		catchedObject->GetComponent<Transform>()->SetPosition(offsetPos);
		auto rb = catchedObject->GetComponent<RigidBodyComponent>();
		rb->SetAngularVelocity(Mathf::Vector3::Zero);
		rb->SetLinearVelocity(Mathf::Vector3::Zero);
	}


	if (m_nearObject) {
		auto nearMesh = m_nearObject->GetComponent<MeshRenderer>();
		if(nearMesh)
			nearMesh->m_Material->m_materialInfo.m_bitflag = 16;
	}


	if (m_comboCount != 0)
	{
		m_comboElapsedTime += tick;

		if (m_comboElapsedTime > m_comboTime)
		{
			m_comboCount = 0;
			m_comboElapsedTime = 0.f;
		}
	}
	
	if (isCharging)
	{
		m_chargingTime += tick;
	}

	if (m_curDashCount != 0)
	{
		m_dubbleDashElapsedTime += tick;
		m_dashCoolElapsedTime += tick;
		if (m_dashCoolElapsedTime >= dashCooldown)
		{
			m_curDashCount = 0;
			m_dubbleDashElapsedTime = 0.f;
		}
	}

	if (isDashing)
	{
		m_dashElapsedTime += tick;
		if (m_dashElapsedTime >= m_dashTime)
		{

			isDashing = false;
			m_dashElapsedTime = 0.f;
			player->GetComponent<CharacterControllerComponent>()->EndKnockBack(); //&&&&&  넉백이랑같이  쓸함수 이름수정할거
		}
		else
		{
			auto forward = player->m_transform.GetForward(); 
			auto controller = player->GetComponent<CharacterControllerComponent>();
			controller->Move({ forward.x ,forward.z });

		}

	}
	if (isStun)
	{
		
		stunTime -= tick;
		auto controller = player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0,0 });
		if (0.f >= stunTime)
		{
			isStun = false;
		}
	}


	if (isKnockBack)
	{
		KnockBackElapsedTime += tick;
		if (KnockBackElapsedTime >= KnockBackTime)
		{
			
			isKnockBack = false;
			KnockBackElapsedTime = 0.f;
			player->GetComponent<CharacterControllerComponent>()->EndKnockBack();
		}
		else
		{
			auto forward = player->m_transform.GetForward(); //맞은 방향에서 밀리게끔 수정
			auto controller = player->GetComponent<CharacterControllerComponent>();
			controller->Move({ -forward.x ,-forward.z});

		}
	}
}

void Player::OnDestroy()
{
	/*if (m_animator)
	{
		for (auto& socket : m_animator->m_Skeleton->m_sockets)
		{
			m_animator->m_Skeleton->DeleteSocket(socket->m_name);
		}
	}*/
	

}

void Player::Move(Mathf::Vector2 dir)
{
	if (isStun || isKnockBack) return;
	auto controller = player->GetComponent<CharacterControllerComponent>();
	if (!controller) return;
	
	auto worldRot = camera->m_transform.GetWorldQuaternion();
	Vector3 right = XMVector3Rotate(Vector3::Right, worldRot);
	Vector3 forward = XMVector3Cross(Vector3::Up, right);// XMVector3Rotate(Vector3::Forward, worldRot);

	Vector2 moveDir = dir.x * Vector2(right.x, right.z) + -dir.y * Vector2(forward.x, forward.z);
	moveDir.Normalize();

	/*if (dir.Length() > 0.00001f)
	{
		m_animator->SetParameter("OnMove", true);
	}
	else
	{
		m_animator->SetParameter("OnMove", false);
	}*/



	controller->Move(moveDir);
	if (controller->IsOnMove())
	{
		if(m_curWeapon)
			m_curWeapon->SetEnabled(false);
		m_animator->SetParameter("OnMove", true);
	}
	else
	{
		if (m_curWeapon)
			m_curWeapon->SetEnabled(true);
		m_animator->SetParameter("OnMove", false);
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
	if (m_nearObject != nullptr)
	{
		//Socket* righthand = m_animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
		//righthand->AttachObject(m_nearObject);
		auto rigidbody = m_nearObject->GetComponent<RigidBodyComponent>();
		//rigidbody->SetBodyType(EBodyType::STATIC);
		
		catchedObject = m_nearObject;
		m_nearObject = nullptr;
		//catchedObject->GetComponent<BoxColliderComponent>()->SetColliderEnabled(false);
		if (m_curWeapon)
			m_curWeapon->SetEnabled(false);
	}
}

void Player::Throw()
{
	//Socket* righthand = m_animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	//righthand->DetachObject(catchedObject);
	//auto rigidbody = catchedObject->GetComponent<RigidBodyComponent>();
	//rigidbody->SetBodyType(EBodyType::DYNAMIC);
	//auto& transform = GetOwner()->m_transform;
	//auto forward  = transform.GetForward();
	//rigidbody->AddForce({ forward.x * ThrowPowerX ,ThrowPowerY, forward.z * ThrowPowerX }, EForceMode::IMPULSE);

	// 아시스를 캐싱하는 방식으로 추후 수정 필요
	Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
	auto asis = GameObject::Find("Asis");
	Mathf::Vector3 asisPos = asis->m_transform.GetWorldPosition();
	Mathf::Vector3 directionToAsis = asisPos - myPos;
	float distance = directionToAsis.Length();
	directionToAsis.Normalize();

	float dot = directionToAsis.Dot(GetOwner()->m_transform.GetForward());
	if (dot > cosf(Mathf::Deg2Rad * detectAngle * 0.5f)) {
		auto item = catchedObject->GetComponent<EntityItem>();
		if (item) {
			item->SetThrowOwner(this);
		}
		catchedObject = nullptr;
		m_nearObject = nullptr; //&&&&&
		if (m_curWeapon)
			m_curWeapon->SetEnabled(true);
	}
}

void Player::Dash()
{
	if (m_curDashCount >= dashAmount ) return;   //최대 대시횟수만큼했으면 못함
	if (m_curDashCount != 0 && m_dubbleDashElapsedTime >= dubbleDashTime) return; //이미 대시했을떄 더블대시타임안에 다시안하면 못함

	if (m_curDashCount == 0)
	{
		std::cout << "Dash  " << std::endl;
	}
	else if (m_curDashCount == 1)
	{
		std::cout << "Dubble Dash  " << std::endl;
	}
	else if (m_curDashCount == 2)
	{
		std::cout << "Tripple Dash  " << std::endl;
	}
	else
	{
		std::cout << "multiple Dash  " << std::endl;
	}
	
	//대쉬 애니메이션중엔 적통과
	//m_animator->SetParameter("Dash", true);
	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();

	isDashing = true;

	controller->SetKnockBack(m_dashPower, 0.f);
	m_animator->SetParameter("OnMove", false);
	//controller->AddFinalMultiplierSpeed(m_dashPower);
	m_dashCoolElapsedTime = 0.f;
	m_dubbleDashElapsedTime = 0.f;
	m_dashElapsedTime = 0.f;
	m_curDashCount++;
}

void Player::StartAttack()
{
	isCharging = true;
}

void Player::Charging()
{
	if (m_chargingTime >= 0.7f)
	{
		std::cout << "charginggggggg" << std::endl;
	}
	//m_animator->SetParameter("Charging", true);

}

void Player::Attack()
{
	isCharging = false;
	m_chargingTime = 0.f;

	auto gg = SceneManagers->GetActiveScene()->CreateGameObject("gumgi");

	auto ggg = gg->AddComponent<EffectComponent>();
	
	ggg->PlayEffectByName("gumgi2");
	
	if (m_comboCount == 0)
	{
		auto obj = SceneManagers->GetActiveScene()->CreateGameObject("gumgi");
		if (obj)
		{
			auto effect = obj->AddComponent<EffectComponent>();
			if (effect)
			{

			}
		}
		std::vector<HitResult> hits;
		auto world = player->m_transform.GetWorldPosition();
		world.m128_f32[1] += 0.5f;
		auto forward = player->m_transform.GetForward();
		int size = RaycastAll(world, forward, 10.f, 1u, hits);

		for (int i = 0; i < size; i++) 
		{
			auto object = hits[i].hitObject;
			if (object == GetOwner()) continue;

			std::cout << object->m_name.data() << std::endl;
			auto enemy = object->GetComponent<TestEnemy>();
			if (enemy)
			{
				enemy->curHP -= 10.f;
				std::cout << enemy->curHP << std::endl;
				auto rigid = enemy->GetOwner()->GetComponent<RigidBodyComponent>();
				
				rigid->AddForce({ forward.x * AttackPowerX,AttackPowerY,forward.z * AttackPowerX }, EForceMode::IMPULSE);
			}
			
			auto entityItem = object->GetComponent<Entity>();
			if (entityItem) {
				entityItem->Attack(this, 10);
			}
		}
	}
	else if (m_comboCount == 1)
	{

	}
	else if (m_comboCount == 2)
	{

	}
	m_animator->SetParameter("Attack", true);
	std::cout << "Attack!!" << std::endl;
	std::cout << m_comboCount << "Combo Attack " << std::endl;
	m_comboCount++;
	m_comboElapsedTime = 0;

}

void Player::SwapWeaponLeft()
{
	//m_weaponIndex--;
	//std::cout << "left weapon equipped" << std::endl;
	/*if (m_curWeapon != nullptr)
	{
		m_curWeapon->SetEnabled(false);
		m_curWeapon = m_weaponInventory[m_weaponIndex];
		m_curWeapon->SetEnabled(true);
	}*/
}

void Player::SwapWeaponRight()
{
	//m_weaponIndex++;
	////std::cout << "right weapon equipped" << std::endl;
	//if (m_curWeapon != nullptr)
	//{
	//	m_curWeapon->SetEnabled(false);
	//	m_curWeapon = m_weaponInventory[m_weaponIndex];
	//	m_curWeapon->SetEnabled(true);
	//}
}

void Player::AddWeapon(GameObject* weapon)
{
	if (m_weaponInventory.size() >= 4) return;

	m_weaponInventory.push_back(weapon);
	m_curWeapon = weapon;
	m_curWeapon->SetEnabled(true);
	//Socket* righthand = m_animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
	//righthand->AttachObject(m_curWeapon);
	
}

void Player::DeleteCurWeapon()
{
	if (!m_curWeapon)
		return;

	auto it = std::find(m_weaponInventory.begin(), m_weaponInventory.end(), m_curWeapon);

	if (it != m_weaponInventory.end())
	{
		m_weaponInventory.erase(it);
		m_curWeapon->SetEnabled(false);
		//Socket* righthand = m_animator->MakeSocket("RightHand", "mixamorig:RightHandThumb1");
		//righthand->DetachAllObject();
		m_curWeapon = nullptr;    
	}
}



void Player::OnPunch()
{
	std::cout << "ppppuuuunchhhhhhh" << std::endl;
}

void Player::TestStun()
{
	isStun = true;
	stunTime = 1.5f;
	m_animator->SetParameter("OnMove", false);
	
}

void Player::TestKnockBack()
{
	isKnockBack = true;
	KnockBackTime = 0.5f;
	player->GetComponent<CharacterControllerComponent>()->SetKnockBack(KnockBackForce,KnockBackForceY);
	m_animator->SetParameter("OnMove", false);
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
	//std::cout << "player muunga boodit him trigger" << collision.otherObj->m_name.ToString().c_str() << std::endl;
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
		auto nearMesh = m_nearObject->GetComponent<MeshRenderer>();
		if(nearMesh)
			nearMesh->m_Material->m_materialInfo.m_bitflag = 0;
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
	//std::cout << "player muunga boodit him" << collision.otherObj->m_name.ToString().c_str() << std::endl;
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
		auto nearMesh = m_nearObject->GetComponent<MeshRenderer>();
		if (nearMesh)
			nearMesh->m_Material->m_materialInfo.m_bitflag = 0;
		m_nearObject = nullptr;
	}
}
