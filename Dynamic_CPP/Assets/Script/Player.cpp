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
#include "BoxColliderComponent.h"
#include "EntityResource.h"
#include "Weapon.h"
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




	auto handsocket = GameObject::Find("SwordSocket");


	auto curweapon = GameObject::Find("realSword");

	if (handsocket)
	{
		if (curweapon)
		{
			handsocket->AddChild(curweapon);
			auto basicWeapon = curweapon->GetComponent<Weapon>();
			AddWeapon(basicWeapon);
		}
	}



	

	//handsocket.


	player->m_collisionType = 2;
	//m_animator->m_Skeleton->m_animations[5].SetEvent("Throw", "Player", "ThrowEvent", 0.25);


	auto gmobj = GameObject::Find("GameManager");
	if (gmobj)
	{
		auto gm = gmobj->GetComponent<GameManager>();
		gm->PushEntity(this);
		gm->PushPlayer(this);
	}

	camera = GameObject::Find("Main Camera");
}

void Player::Update(float tick)
{
	if (catchedObject)
	{
		auto forward = GetOwner()->m_transform.GetForward(); // Vector3
		auto world = GetOwner()->m_transform.GetWorldPosition(); // XMVECTOR

		XMVECTOR forwardVec = XMLoadFloat3(&forward); // Vector3 → XMVECTOR

		XMVECTOR offsetPos = world + forwardVec * 1.0f;
		offsetPos.m128_f32[1] = 1.0f; // Y 고정

		catchedObject->GetOwner()->GetComponent<Transform>()->SetPosition(offsetPos);
	}
	if (m_nearObject) {
		auto nearMesh = m_nearObject->GetComponent<MeshRenderer>();
		if (nearMesh)
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
			controller->Move({ -forward.x ,-forward.z });

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
			controller->Move({ -forward.x ,-forward.z });
		}
	}

}

void Player::Move(Mathf::Vector2 dir)
{
	if (isStun || isKnockBack || !m_isCallStart || isDashing) return;
	auto controller = player->GetComponent<CharacterControllerComponent>();
	if (!controller) return;

	auto worldRot = camera->m_transform.GetWorldQuaternion();
	Vector3 right = XMVector3Rotate(Vector3::Right, worldRot);
	Vector3 forward = XMVector3Cross(Vector3::Up, right);// XMVector3Rotate(Vector3::Forward, worldRot);

	Vector2 moveDir = dir.x * Vector2(right.x, right.z) + - dir.y * Vector2(forward.x, forward.z);
	moveDir.Normalize();


	controller->Move(moveDir);
	if (controller->IsOnMove())
	{
		/*if (m_curWeapon)
			m_curWeapon->GetOwner()->SetEnabled(false);*/
		if (m_animator)
			m_animator->SetParameter("OnMove", true);
	}
	else
	{
		/*if (m_curWeapon)
			m_curWeapon->GetOwner()->SetEnabled(true);*/
		if (m_animator)
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

		auto rigidbody = m_nearObject->GetComponent<RigidBodyComponent>();

		m_animator->SetParameter("OnGrab", true);
		catchedObject = m_nearObject->GetComponent<EntityItem>();
		m_nearObject = nullptr;
		catchedObject->GetOwner()->GetComponent<BoxColliderComponent>()->SetColliderType(EColliderType::TRIGGER);
		catchedObject->SetThrowOwner(this);
		if (m_curWeapon)
			m_curWeapon->GetOwner()->SetEnabled(false);
	}
}

void Player::Throw()
{
	m_animator->SetParameter("OnThrow", true);
}

void Player::DropCatchItem()
{
	if (catchedObject != nullptr)
	{
		if (catchedObject) {
			catchedObject->Drop(player->m_transform.GetForward(), 2.0f);
		}

		catchedObject = nullptr;
		m_nearObject = nullptr; //&&&&&
		if (m_curWeapon)
			m_curWeapon->GetOwner()->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요
	}
}

void Player::ThrowEvent()
{
	if (catchedObject) {
		catchedObject->SetThrowOwner(this);
		catchedObject->Throw(player->m_transform.GetForward(), 6.0f);
	}
	catchedObject = nullptr;
	m_nearObject = nullptr; //&&&&&
	if (m_curWeapon)
		m_curWeapon->GetOwner()->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요
}

void Player::Dash()
{
	if (m_curDashCount >= dashAmount) return;   //최대 대시횟수만큼했으면 못함
	if (m_curDashCount != 0 && m_dubbleDashElapsedTime >= dubbleDashTime) return; //이미 대시했을떄 더블대시타임안에 다시안하면 못함

	if (m_curDashCount == 0)
	{
		std::cout << "Dash  " << std::endl;
	}
	else if (m_curDashCount == 1)
	{
		std::cout << "Dubble Dash  " << std::endl;
	}

	//대쉬 애니메이션중엔 적통과
	m_animator->SetParameter("OnDash", true);
	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();

	DropCatchItem();
	isDashing = true;

	controller->SetKnockBack(m_dashPower, 0.f);
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


	if (m_comboCount == 0)
	{
		int gumNumber = playerIndex + 1;
		std::string gumName = "GumGi" + std::to_string(gumNumber);
		auto obj = GameObject::Find(gumName);
		if (obj)
		{
			auto pos = GetOwner()->m_transform.GetWorldPosition();
			auto forward2 = GetOwner()->m_transform.GetForward();
			auto offset{ 2 };
			auto offset2 = -forward2 * offset;
			pos.m128_f32[0] = pos.m128_f32[0] + offset2.x;
			pos.m128_f32[1] = 1;
			pos.m128_f32[2] = pos.m128_f32[2] + offset2.z;

			XMMATRIX lookAtMat = XMMatrixLookToRH(XMVectorZero(), forward2, XMVectorSet(0, 1, 0, 0));
			Quaternion swordRotation = Quaternion::CreateFromRotationMatrix(lookAtMat);
			obj->m_transform.SetPosition(pos);

			obj->m_transform.SetRotation(swordRotation);
			obj->m_transform.UpdateWorldMatrix();
			if (obj)
			{
				auto effect = obj->GetComponent<EffectComponent>();
				if (effect)
				{
					effect->Apply();
				}
			}
		}
		std::vector<HitResult> hits;
		auto world = player->m_transform.GetWorldPosition();
		world.m128_f32[1] += 0.5f;
		auto forward = player->m_transform.GetForward();
		int size = RaycastAll(world, -forward, 10.f, 1u, hits);

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
				entityItem->Attack(this, 100);
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
	DropCatchItem();
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

void Player::AddWeapon(Weapon* weapon)
{
	if (m_weaponInventory.size() >= 4) return;

	m_weaponInventory.push_back(weapon);
	m_curWeapon = weapon;
	m_curWeapon->GetOwner()->SetEnabled(true);

}

void Player::DeleteCurWeapon()
{
	if (!m_curWeapon)
		return;

	auto it = std::find(m_weaponInventory.begin(), m_weaponInventory.end(), m_curWeapon);

	if (it != m_weaponInventory.end())
	{
		m_weaponInventory.erase(it);
		m_curWeapon->GetOwner()->SetEnabled(false);
		m_curWeapon = nullptr;
	}
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
	player->GetComponent<CharacterControllerComponent>()->SetKnockBack(KnockBackForce, KnockBackForceY);
	m_animator->SetParameter("OnMove", false);
}

void Player::FindNearObject(GameObject* gameObject)
{
	if (gameObject->GetComponent<EntityItem>() == nullptr) return;
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
		if (nearMesh)
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
