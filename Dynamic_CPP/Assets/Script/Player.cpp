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
#include "PrefabUtility.h"
#include "CameraComponent.h"
#include "Bullet.h"
#include "NormalBullet.h"
#include "SpecialBullet.h"
#include "Bomb.h"
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
	//handSocket = m_animator->MakeSocket("handsocket", "Sword_g", aniOwner);
	

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
		//asis�� �Ÿ���� ����
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
	if (isAttacking == false && m_comboCount != 0) //&&&&& �޺�ī��Ʈ �ʱ�ȭ���� Ȯ���ʿ� ���� 0.5�ʺ��� �ʰԵ� 
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
			player->GetComponent<CharacterControllerComponent>()->EndKnockBack(); //&&&&&  �˹��̶�����  ���Լ� �̸������Ұ�
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
	//	// ���� ��ġ ��ȯ.
	//	GetOwner()->m_transform.SetPosition(worldpos);
	//	return;
	//}
	//XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	//float clamp_limit = 0.9f;
	//XMVECTOR clampedNdcPos = XMVectorClamp(
	//	ndcPos,
	//	XMVectorSet(-clamp_limit, -clamp_limit, 0.0f, 0.0f), // Z�� Ŭ�������� ����
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
		if (m_animator)
			m_animator->SetParameter("OnMove", true);
	}
	else
	{
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
		m_curWeapon->SetEnabled(true); //�̰� �ش���� state ->exit ������ �̵��ʿ�





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
	else //�������� ��ô
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
			m_curWeapon->SetEnabled(true); //�̰� �ش���� state ->exit ������ �̵��ʿ�
		m_animator->SetParameter("OnDrop", true);
	}
}

void Player::Dash()
{
	if (m_curDashCount >= dashAmount) return;   //�ִ� ���Ƚ����ŭ������ ����
	if (m_curDashCount != 0 && m_dubbleDashElapsedTime >= dubbleDashTime) return; //�̹� ��������� ������Ÿ�Ӿȿ� �ٽþ��ϸ� ����
	dashEffect->Apply();
	if (m_curDashCount == 0)
	{
		std::cout << "Dash  " << std::endl;
	}
	else if (m_curDashCount == 1)
	{
		std::cout << "Dubble Dash  " << std::endl;
	}

	//�뽬 �ִϸ��̼��߿� �����
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
	//���⼭ ����ó���ϰ� ��¡���� 
	if (isAttacking == false || canMeleeCancel == true)
	{
		isAttacking = true;
		DropCatchItem();
		//if (m_curWeapon)
		//{
		//	if (m_curWeapon->itemType == ItemType::Meely || m_curWeapon->itemType == ItemType::Basic)
		//	{
		//		m_animator->SetParameter("Attack", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
		//		//m_animator->SetUseLayer(0,false);
		//		std::cout << "Melee Attack!!" << std::endl;
		//	}


		//	if (m_curWeapon->itemType == ItemType::Range)
		//	{
		//		m_animator->SetParameter("RangeAttack", true); //���Ÿ� ���� �ִϸ��̼�����
		//		//m_animator->SetUseLayer(0,false);
		//		std::cout << "RangeAttack!!" << std::endl;
		//		ShootNormalBullet(); //���Ÿ����� Ű������ �̺�Ʈ���ֱ�
		//	}

		//	if (m_curWeapon->itemType == ItemType::Explosion)
		//	{
		//		m_animator->SetParameter("BombAttack", true); //��ź ���� �ִϸ��̼�����
		//		//m_animator->SetUseLayer(0,false);
		//		std::cout << "BombAttack!!" << std::endl;
		//	}


		//	m_comboCount++;
		//	m_comboElapsedTime = 0;
		//	attackElapsedTime = 0;
		//	if (m_curWeapon->CheckDur() == true)
		//	{
		//		std::cout << "weapon break" << std::endl;
		//	}
		//}

		if (m_comboCount == 0)
		{
			m_animator->SetParameter("MeleeAttack1", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
			std::cout << "MeleeAttack1" << std::endl;
			canMeleeCancel = false;
			//m_comboCount++;
		}
		else if (m_comboCount == 1)
		{
			m_animator->SetParameter("MeleeAttack2", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
			std::cout << "MeleeAttack2" << std::endl;
			canMeleeCancel = false;
			//m_comboCount++;
		}
		else if (m_comboCount == 2)
		{
			m_animator->SetParameter("MeleeAttack3", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
			std::cout << "MeleeAttack3" << std::endl;
			canMeleeCancel = false;
		}
	}
}

void Player::Charging()
{
	if (m_chargingTime >= minChargedTime)
	{
		std::cout << "charginggggggg" << std::endl;
	}
	//m_animator->SetParameter("Charging", true); //��¡�߿� ������� ����Ʈ ��� Idle or Move �ִϸ��̼� ����

}

void Player::Attack1()
{
	//���⼱ ��¡�ð��� ������ ��¡���ݸ� ����
	//�ٰŸ��� ū����Ʈ + 1,2,3Ÿ�� ���Ѿִϸ��̼��� �ϳ�  ,,, ���Ÿ��� ��ä�÷� ������ �߻�
	isCharging = false;
	std::cout << m_chargingTime << " second charging" << std::endl;


	if (m_chargingTime >= minChargedTime)
	{
		//�������ݳ���
	}

	//if (isAttacking == false || canMeleeCancel == true)
	//{
	//	isAttacking = true;
	//	DropCatchItem();
	//	//if (m_curWeapon)
	//	//{
	//	//	if (m_curWeapon->itemType == ItemType::Meely || m_curWeapon->itemType == ItemType::Basic)
	//	//	{
	//	//		m_animator->SetParameter("Attack", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
	//	//		//m_animator->SetUseLayer(0,false);
	//	//		std::cout << "Melee Attack!!" << std::endl;
	//	//	}


	//	//	if (m_curWeapon->itemType == ItemType::Range)
	//	//	{
	//	//		m_animator->SetParameter("RangeAttack", true); //���Ÿ� ���� �ִϸ��̼�����
	//	//		//m_animator->SetUseLayer(0,false);
	//	//		std::cout << "RangeAttack!!" << std::endl;
	//	//		ShootNormalBullet(); //���Ÿ����� Ű������ �̺�Ʈ���ֱ�
	//	//	}

	//	//	if (m_curWeapon->itemType == ItemType::Explosion)
	//	//	{
	//	//		m_animator->SetParameter("BombAttack", true); //��ź ���� �ִϸ��̼�����
	//	//		//m_animator->SetUseLayer(0,false);
	//	//		std::cout << "BombAttack!!" << std::endl;
	//	//	}


	//	//	m_comboCount++;
	//	//	m_comboElapsedTime = 0;
	//	//	attackElapsedTime = 0;
	//	//	if (m_curWeapon->CheckDur() == true)
	//	//	{
	//	//		std::cout << "weapon break" << std::endl;
	//	//	}
	//	//}

	//	if (m_comboCount == 0)
	//	{
	//		m_animator->SetParameter("MeleeAttack1", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
	//		std::cout << "MeleeAttack1" << std::endl;
	//		canMeleeCancel = false;
	//		//m_comboCount++;
	//	}
	//	else if (m_comboCount == 1)
	//	{
	//		m_animator->SetParameter("MeleeAttack2", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
	//		std::cout << "MeleeAttack2" << std::endl;
	//		canMeleeCancel = false;
	//		//m_comboCount++;
	//	}
	//	else if (m_comboCount == 2)
	//	{
	//		m_animator->SetParameter("MeleeAttack3", true); //�ٰŸ����� �ִϸ��̼����� //���� �����Լ��� �ִϸ��̼� behavior�� Ű������ �̺�Ʈ���� ����
	//		std::cout << "MeleeAttack3" << std::endl;
	//		canMeleeCancel = false;
	//	}
	//}

	m_chargingTime = 0.f;
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


		//�����ϰ� ���������� ���̱�
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
	if (!m_curWeapon || m_curWeapon == m_weaponInventory[0]) //�⺻����
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


void Player::Cancancel()
{
	canMeleeCancel = true;
	if (m_comboCount < 2)
	{
		m_comboCount++;
		m_comboElapsedTime = 0.f;
	}
	else
	{
		m_comboCount = 0;
		m_comboElapsedTime = 0.f;
	}
}

void Player::ChangeAutoTarget(Mathf::Vector2 dir)
{
	if (inRangeEnemy.empty()) //������� �ܼ� ĳ���� ȸ��
	{

	}
	else //��������� �Է¹��⿡���� ���� ������� Ÿ������
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
	bombThrowPosition.x += dir.x;
	bombThrowPosition.z += dir.y;

	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	Mathf::Vector3 targetPos = pos + bombThrowPosition;

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
		
		float distacne = 2.0f;
		if (m_curWeapon)
		{
			distacne = m_curWeapon->itemAckRange;
		}
		int size = RaycastAll(rayOrigin, direction, distacne, 1u, hits);

		float angle = XMConvertToRadians(15.0f);
		Vector3 leftDir = Vector3::Transform(direction, Matrix::CreateRotationY(-angle));
		leftDir.Normalize();
		Vector3 rightDir = Vector3::Transform(direction, Matrix::CreateRotationY(angle));
		rightDir.Normalize();
		std::vector<HitResult> leftHits;
		int leftSize = RaycastAll(rayOrigin, leftDir, distacne, 1u, leftHits);
		std::vector<HitResult> rightHits;
		int rightSize = RaycastAll(rayOrigin, rightDir, distacne, 1u, rightHits);
		std::vector<HitResult> allHits;
		allHits.reserve(size + leftSize + rightSize);
		allHits.insert(allHits.end(), hits.begin(), hits.end());
		allHits.insert(allHits.end(), leftHits.begin(), leftHits.end());
		allHits.insert(allHits.end(), rightHits.begin(), rightHits.end());
		for (auto& hit : allHits)
		{
			auto object = hit.gameObject;
			if (object == GetOwner()) continue;

			if (auto entity = object->GetComponent<EntityEnemy>())
			{
				auto [iter, inserted] = AttackTarget.insert(entity);
				if (inserted) (*iter)->SendDamage(this, 100);
			}

			if (auto mineral = object->GetComponent<EntityResource>())
			{
				auto [iter, inserted] = AttackTarget.insert(mineral);
				if (inserted) (*iter)->SendDamage(this, 100);
			}
		}
}

void Player::ShootBullet()
{
	//���Ÿ� ���� �϶� ���Ӻ����� �߻�
	auto playerPos = GetOwner()->m_transform.GetWorldPosition();
	float distance;
	
	for (auto enemy : inRangeEnemy)
	{
		if (enemy)
		{
			auto enemyPos = enemy->GetOwner()->m_transform.GetWorldPosition();
			XMVECTOR diff = XMVectorSubtract(playerPos, enemyPos);
			XMVECTOR distSqVec = XMVector3LengthSq(diff);
			XMStoreFloat(&distance, distSqVec);
			
			if (distance < nearDistance)
			{
				nearDistance = distance;
				curTarget = enemy;
				
			}
			
			
		}
	}
	if (curTarget)
	{
		//���Ÿ� ����
	}

	nearDistance = FLT_MAX;
}

void Player::ShootNormalBullet()
{
	Prefab* bulletprefab = PrefabUtilitys->LoadPrefab("NormalBullet");
	if (bulletprefab && player)
	{
		GameObject* bulletObj = PrefabUtilitys->InstantiatePrefab(bulletprefab, "bullet");
		NormalBullet* bullet = bulletObj->GetComponent<NormalBullet>();
		Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
		if (m_curWeapon)
		{
			bullet->Initialize(this, pos, -player->m_transform.GetForward(), m_curWeapon->itemAckDmg);
		}

	}
}

void Player::ShootSpecialBullet()
{
	//Todo:: pool����ã�� ������ �����տ��� ����
	Prefab* bulletprefab = PrefabUtilitys->LoadPrefab("SpecialBullet");
	if (bulletprefab && player)
	{
		GameObject* bulletObj = PrefabUtilitys->InstantiatePrefab(bulletprefab, "specialbullet");
		SpecialBullet* bullet = bulletObj->GetComponent<SpecialBullet>();
		Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
		if (m_curWeapon)
		{
			bullet->Initialize(this, pos, -player->m_transform.GetForward(), m_curWeapon->itemAckDmg);
		}

	}

	
}

void Player::ThrowBomb()
{
	Prefab* bombprefab = PrefabUtilitys->LoadPrefab("Bomb");
	//bomb->ThrowBomb(this, bombThrowPosition);
	//bomb �� �����ո���ɷ� �޾ƿ��Բ� ���� or weaponPool �ʿ�
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
