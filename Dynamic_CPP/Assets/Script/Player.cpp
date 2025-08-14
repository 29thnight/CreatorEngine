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
#include "Entity.h"

#include "CameraComponent.h"
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
			aniOwner = GameObject::FindIndex(child);
			break;
		}

	}
	if (!m_animator)
	{
		m_animator = player->GetComponent<Animator>();
	}


	handSocket= m_animator->MakeSocket("handsocket","hand.R.002", aniOwner);

	

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
		GM = gmobj->GetComponent<GameManager>();
		GM->PushEntity(this);
		GM->PushPlayer(this);
	}

	m_controller = player->GetComponent<CharacterControllerComponent>();
	camera = GameObject::Find("Main Camera");
}

void Player::Update(float tick)
{
	m_controller->SetBaseSpeed(moveSpeed);
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	pos.y += 0.5;
	dashObj->m_transform.SetPosition(pos);



	if (isDead)
	{
		m_animator->SetParameter("OnDead", true);
	}

	if (catchedObject)
	{
		auto forward = GetOwner()->m_transform.GetForward();
		auto world = GetOwner()->m_transform.GetWorldPosition(); 
		XMVECTOR forwardVec = XMLoadFloat3(&forward); 
		XMVECTOR offsetPos = world + -forwardVec * 1.0f;
		offsetPos.m128_f32[1] = 1.0f; 
		catchedObject->GetOwner()->GetComponent<Transform>()->SetPosition(offsetPos);


		//asis와 거리계속 갱신
		auto& asiss = GM->GetAsis();
		if (!asiss.empty())
		{
			auto asis = asiss[0]->GetOwner();
			Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 asisPos = asis->m_transform.GetWorldPosition();
			Mathf::Vector3 directionToAsis = asisPos - myPos;
			float distance = directionToAsis.Length();
			directionToAsis.Normalize();

			float dot = directionToAsis.Dot(GetOwner()->m_transform.GetForward());
			if (dot > cosf(Mathf::Deg2Rad * detectAngle * 0.5f))
			{
				onIndicate = true;
				std::cout << "onIndicate!!!!!!!!!" << std::endl;
			}
			else
			{
				onIndicate = false;
			}
		}

	}
	if (m_nearObject) {
		auto nearMesh = m_nearObject->GetComponent<MeshRenderer>();
		if (nearMesh)
			nearMesh->m_Material->m_materialInfo.m_bitflag = 16;
	}
	if (m_comboCount != 0)
	{
		m_comboElapsedTime += tick;

		if (m_comboElapsedTime > comboDuration)
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
			//dashEffect->StopEffect();
		}
		else
		{
			auto forward = player->m_transform.GetForward();
			auto controller = player->GetComponent<CharacterControllerComponent>();
			controller->Move({ -forward.x ,-forward.z });
			


		}

	}


}

void Player::LateUpdate(float tick)
{
	//CameraComponent* camComponent = camera->GetComponent<CameraComponent>();
	//auto cam = camComponent->GetCamera();
	//auto camViewProj = cam->CalculateView() * cam->CalculateProjection();
	//auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	//XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	//XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	//float w = XMVectorGetW(clipSpacePos);
	//if (w < 0.001f) {
	//	// 원래 위치 반환.
	//	GetOwner()->m_transform.SetPosition(worldpos);
	//	return;
	//}
	//XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	//float clamp_limit = 0.9f;
	//XMVECTOR clampedNdcPos = XMVectorClamp(
	//	ndcPos,
	//	XMVectorSet(-clamp_limit, -clamp_limit, 0.0f, 0.0f), // Z는 클램핑하지 않음
	//	XMVectorSet(clamp_limit, clamp_limit, 1.0f, 1.0f)
	//);
	//XMVECTOR clampedClipSpacePos = XMVectorScale(clampedNdcPos, w);
	//XMVECTOR newWorldPos = XMVector3TransformCoord(clampedClipSpacePos, invCamViewProj);

	//GetOwner()->m_transform.SetPosition(newWorldPos);
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

void Player::ThrowEvent()
{
	std::cout << "ThrowEvent" << std::endl;
	if (catchedObject) {
		catchedObject->SetThrowOwner(this);
		catchedObject->Throw(player->m_transform.GetForward(), { ThrowPowerX,ThrowPowerY });
	}
	catchedObject = nullptr;
	m_nearObject = nullptr; //&&&&&
	if (m_curWeapon)
		m_curWeapon->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요





	Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
	auto asis = GameObject::Find("Asis");
	Mathf::Vector3 asisPos = asis->m_transform.GetWorldPosition();
	Mathf::Vector3 directionToAsis = asisPos - myPos;
	float distance = directionToAsis.Length();
	directionToAsis.Normalize();

	float dot = directionToAsis.Dot(GetOwner()->m_transform.GetForward());
	if (onIndicate) 
	{
		if (catchedObject)
		{
			auto item = catchedObject->GetOwner()->GetComponent<EntityItem>();
			if (item) {
				item->SetThrowOwner(this);
			}
			catchedObject = nullptr;
			m_nearObject = nullptr; //&&&&&
			if (m_curWeapon)
				m_curWeapon->SetEnabled(true);
		}
	}
	else //유도없이 투척
	{

	}
}


void Player::DropCatchItem()
{
	if (catchedObject != nullptr)
	{
		if (catchedObject) {
			catchedObject->Drop(player->m_transform.GetForward(), { DropPowerX,DropPowerY });
		}

		catchedObject = nullptr;
		m_nearObject = nullptr; //&&&&&
		if (m_curWeapon)
			m_curWeapon->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요
		m_animator->SetParameter("OnDrop", true);
	}
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

	controller->SetKnockBack(dashDistacne, 0.f);
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

	isCharging = false;
	std::cout << m_chargingTime << " second charging" << std::endl;
	m_chargingTime = 0.f;

	if (isAttacking == false)
	{
		isAttacking = true;
		m_animator->SetParameter("Attack", true);
		std::cout << "Attack!!" << std::endl;
		DropCatchItem();
		m_comboCount++;
		m_comboElapsedTime = 0;
		attackElapsedTime = 0;
	}
}

void Player::StartRay()
{
	startRay = true;
}

void Player::EndRay()
{
	startRay = false;
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
	if (!m_curWeapon || m_curWeapon == m_weaponInventory[0]) //기본무기
		return;

	auto it = std::find(m_weaponInventory.begin(), m_weaponInventory.end(), m_curWeapon);

	if (it != m_weaponInventory.end())
	{
		SwapWeaponLeft();
		m_weaponInventory.erase(it); 
	}
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

void Player::OnBuff()
{
	Buff(m_curWeapon);
}

void Player::Buff(Weapon* weapon)
{
	
	if (weapon)
	{
		
	}
	

	DeleteCurWeapon();
}


void Player::ChangeAutoTarget(Mathf::Vector2 dir)
{
	if (inRangeEnemy.empty()) //비었으면 단순 캐릭터 회전
	{

	}
	else //들어있으면 입력방향에따라 다음 가까운적 타겟으로
	{

		for (auto& enemy : inRangeEnemy)
		{
			if (curTarget == enemy) continue;
			if (dir.x > 0.1f)
			{
				//
			}
			if (dir.x < -0.1f)
			{
				//
			}
			if (dir.y > 0.1f)
			{
				//
			}
			if (dir.y < -0.1f)
			{
				//
			}
		}
	}
	
}

void Player::MoveBombThrowPosition(Mathf::Vector2 dir)
{

}

void Player::MeleeAttack()
{
		Mathf::Vector3 rayOrigin = GetOwner()->m_transform.GetWorldPosition();
		XMMATRIX handlocal = handSocket->transform.GetLocalMatrix();
		Mathf::Vector3 handPos = handlocal.r[3];
		Mathf::Vector3 direction = handPos - rayOrigin;
		direction.y = 0;
		direction.Normalize();
		std::vector<HitResult> hits;
		rayOrigin.y = 0.5f;
		int size = RaycastAll(rayOrigin, direction, 5.f, 1u, hits);

		for (int i = 0; i < size; i++)
		{
			auto object = hits[i].gameObject;
			if (object == GetOwner()) continue;

			auto entity = object->GetComponent<Entity>();
			auto [iter, inserted] = AttackTarget.insert(entity);
			if (inserted)  
			{
				if (*iter) 
				{
					(*iter)->SendDamage(this, 1);
					//(*iter)->SendKnockBack(this, {1,0});
				}
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
	FindNearObject(collision.otherObj);
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
}

void Player::OnCollisionStay(const Collision& collision)
{
	FindNearObject(collision.otherObj);
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
