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
#include "EntityEnemy.h"

#include "CameraComponent.h"
void Player::Start()
{
	std::cout << Mathf::Vector3::Forward.z << std::endl;

	player = GetOwner();
	auto childred = player->m_childrenIndices;
	for (auto& child : childred)
	{
		auto animator = GameObject::FindIndex(child)->GetComponent<Animator>();

		if (animator)
		{
			m_animator = animator;
			aniOwener = GameObject::FindIndex(child);
			break;
		}

	}
	if (!m_animator)
	{
		m_animator = player->GetComponent<Animator>();
	}


	handSocket= m_animator->MakeSocket("handsocket","hand.R.002", aniOwener);

	

	std::string gumName = "Sword" + std::to_string(playerIndex +1);
	auto obj = GameObject::Find(gumName);
	if (obj && handSocket)
	{
		auto curweapon = obj->GetComponent<Weapon>();
		AddWeapon(curweapon);

	}
	auto curweapon = GameObject::Find("realSword");

	


	
	dashObj = SceneManagers->GetActiveScene()->CreateGameObject("Dashef").get();
	dashEffect = dashObj->AddComponent<EffectComponent>();
	dashEffect->Awake();
	dashEffect->m_effectTemplateName ="Dash";

	player->m_collisionType = 2;



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
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	pos.y += 0.5;
	dashObj->m_transform.SetPosition(pos);


	if (isDead)
	{
		m_animator->SetParameter("OnDead", true);
	}

	if (isAttacking)
	{
		attackElapsedTime += tick;
		auto controller = player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0,0 });
		if (attackElapsedTime >= attackTime)
		{
			attackElapsedTime = 0.f;
			isAttacking = false;
		}
	}
	if (catchedObject)
	{
		auto forward = GetOwner()->m_transform.GetForward(); // Vector3
		auto world = GetOwner()->m_transform.GetWorldPosition(); // XMVECTOR

		XMVECTOR forwardVec = XMLoadFloat3(&forward); // Vector3 → XMVECTOR

		XMVECTOR offsetPos = world + -forwardVec * 1.0f;
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
			dashEffect->StopEffect();
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

void Player::LateUpdate(float tick)
{
	CameraComponent* camComponent = camera->GetComponent<CameraComponent>();
	auto cam = camComponent->GetCamera();
	auto camViewProj = cam->CalculateView() * cam->CalculateProjection();
	auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	float w = XMVectorGetW(clipSpacePos);
	if (w < 0.001f) {
		// 원래 위치 반환.
		GetOwner()->m_transform.SetPosition(worldpos);
		return;
	}
	XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	float clamp_limit = 0.9f;
	XMVECTOR clampedNdcPos = XMVectorClamp(
		ndcPos,
		XMVectorSet(-clamp_limit, -clamp_limit, 0.0f, 0.0f), // Z는 클램핑하지 않음
		XMVectorSet(clamp_limit, clamp_limit, 1.0f, 1.0f)
	);
	XMVECTOR clampedClipSpacePos = XMVectorScale(clampedNdcPos, w);
	XMVECTOR newWorldPos = XMVector3TransformCoord(clampedClipSpacePos, invCamViewProj);

	GetOwner()->m_transform.SetPosition(newWorldPos);
}

void Player::SendDamage(Entity* sender, int damage)
{
	if (sender)
	{
		auto enemy = dynamic_cast<EntityEnemy*>(sender);
		if (enemy)
		{
			// hit
			m_currentHP -= std::max(damage, 0);
			DropCatchItem();
			//TestKnockBack();
			if (m_currentHP <= 0)
			{
				isDead = true;
			}
		}
	}
}

void Player::Move(Mathf::Vector2 dir)
{
	if (isStun || isKnockBack || !m_isCallStart || isDashing || isDead || isAttacking) return;
	auto controller = player->GetComponent<CharacterControllerComponent>();
	if (!controller) return;

	//auto worldRot = camera->m_transform.GetWorldQuaternion();
	//Vector3 right = XMVector3Rotate(Vector3::Right, worldRot);
	//Vector3 forward = XMVector3Cross(Vector3::Up, right);// XMVector3Rotate(Vector3::Forward, worldRot);

	//Vector2 moveDir = dir.x * Vector2(right.x, right.z) + - dir.y * Vector2(forward.x, forward.z);
	//moveDir.Normalize();


	controller->Move(dir);
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

	if (m_nearObject != nullptr && catchedObject ==nullptr)
	{

		auto rigidbody = m_nearObject->GetComponent<RigidBodyComponent>();

		m_animator->SetParameter("OnGrab", true);
		catchedObject = m_nearObject->GetComponent<EntityItem>();
		m_nearObject = nullptr;
		catchedObject->GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(true);
		catchedObject->SetThrowOwner(this);
		if (m_curWeapon)
			m_curWeapon->SetEnabled(false);
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
			m_curWeapon->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요
		m_animator->SetParameter("OnDrop", true);
	}
}

void Player::ThrowEvent()
{
	std::cout << "ThrowEvent" << std::endl;
	if (catchedObject) {
		catchedObject->SetThrowOwner(this);
		catchedObject->Throw(player->m_transform.GetForward(), 6.0f);
	}
	catchedObject = nullptr;
	m_nearObject = nullptr; //&&&&&
	if (m_curWeapon)
		m_curWeapon->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요
}

void Player::Dash()
{
	if (m_curDashCount >= dashAmount) return;   //최대 대시횟수만큼했으면 못함
	if (m_curDashCount != 0 && m_dubbleDashElapsedTime >= dubbleDashTime) return; //이미 대시했을떄 더블대시타임안에 다시안하면 못함
	dashEffect->Apply();
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

void Player::Attack1()
{
	AttackTarget.clear();



	isCharging = false;
	m_chargingTime = 0.f;

	if (isAttacking == false)
	{
		isAttacking = true;
		//if (m_comboCount == 0)
		{
			int gumNumber = playerIndex + 1;
			std::string gumName = "GumGi" + std::to_string(gumNumber);
			std::string effectName;
			effectName = "gg";
			auto obj = GameObject::Find(gumName);
			if (obj)
			{
				Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
				auto forward2 = GetOwner()->m_transform.GetForward();
				auto offset{ 2 };
				auto offset2 = -forward2 * offset;
				pos.x = pos.x + offset2.x;
				pos.y = 1;
				pos.z = pos.z + offset2.z;
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
						effect->ChangeEffect(effectName);
					}
				}
			}
			std::vector<HitResult> hits;
			auto world = player->m_transform.GetWorldPosition();
			world.m128_f32[1] += 0.5f;
			auto forward = player->m_transform.GetForward();
			Mathf::Vector3 dir = world;
			Mathf::Vector3 dir1 = dir;
			Mathf::Vector3 dir2 = dir;

			if (std::abs(dir.x) > std::abs(dir.z)) 
			{
				dir1.x += 0.5f; 
				dir2.x -= 0.5f; 
			}
			else 
			{
				dir1.z += 0.5f; 
				dir2.z -= 0.5f; 
			}


			
			int size = RaycastAll(world, -forward, 3.f, 1u, hits);
			std::vector<HitResult> hits1;
			int size1 = RaycastAll(world, dir1, 3.0f, 1u, hits1);
			std::vector<HitResult> hits2;
			int size2 = RaycastAll(world, dir2, 3.0f, 1u, hits2);

			hits.insert(hits.end(), hits1.begin(), hits1.end());
			hits.insert(hits.end(), hits2.begin(), hits2.end());


			for (int i = 0; i < size; i++)
			{
				auto object = hits[i].hitObject;
				if (object == GetOwner()) continue;
				if(Entity* entity = object->GetComponent<Entity>())
				{
					AttackTarget.insert(entity);
				}
			}
		}


		{
			for (auto& target : AttackTarget)
			{
				if (target)
				{
					target->SendDamage(this, 100);   //확인은 받는사람이 한다?
				}
			}
		}

		m_animator->SetParameter("Attack", true);
		std::cout << "Attack!!" << std::endl;
		DropCatchItem();
		m_comboCount++;
		m_comboElapsedTime = 0;
		attackElapsedTime = 0;
	}
}

void Player::SwapWeaponLeft()
{
	m_weaponIndex--;
	if (m_weaponIndex <= 0)
	{
		m_weaponIndex = 0;
	}
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
	if (m_weaponIndex >= 3)
	{
		m_weaponIndex = 3;
	}

	if ( m_weaponInventory.size() <= m_weaponIndex)
	{
		m_weaponIndex--;
	}
	if (m_curWeapon != nullptr)
	{
		m_curWeapon->SetEnabled(false);
		m_curWeapon = m_weaponInventory[m_weaponIndex];
		m_curWeapon->SetEnabled(true);
	}
}

void Player::AddWeapon(Weapon* weapon)
{
	if (m_weaponInventory.size() >= 4)
	{
		weapon->GetOwner()->Destroy();
		return;


		//리턴하고 던져진무기 죽이기
	}

	if (m_curWeapon)
	{
		m_curWeapon->SetEnabled(false);
	}
	m_weaponInventory.push_back(weapon);
	m_curWeapon = weapon;
	m_curWeapon->SetEnabled(true);
	handSocket->AttachObject(m_curWeapon->GetOwner());

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

void Player::OnRay()
{
	std::vector<HitResult> hits;
	auto world = player->m_transform.GetWorldPosition();
	world.m128_f32[1] += 0.35f;
	auto forward = player->m_transform.GetForward();
	int size = RaycastAll(world, -forward, 3.f, 1u, hits);
	//부채꼴로 여러방
	for (int i = 0; i < size; i++)
	{
		auto object = hits[i].hitObject;
		if (object == GetOwner()) continue;

		auto enemy = object->GetComponent<EntityEnemy>();
		if (enemy)
		{
			enemy->SendDamage(this, 100);
		}

		auto entityItem = object->GetComponent<EntityResource>();
		if (entityItem) {
			entityItem->SendDamage(this, 100);
		}
	}
}




void Player::OnTriggerEnter(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;

	auto weapon = collision.otherObj->GetComponent<Weapon>();
	if (weapon && weapon->OwnerPlayerIndex == playerIndex)
	{
		AddWeapon(weapon);
		weapon->ownerPlayer = nullptr;
		auto weaponrigid = weapon->GetOwner()->GetComponent<RigidBodyComponent>();
		if (weaponrigid)
		{
			weaponrigid->SetEnabled(false);
		}
	}


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
