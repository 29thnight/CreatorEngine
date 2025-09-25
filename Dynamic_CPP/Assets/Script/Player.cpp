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
#include "WeaponSlotController.h"
#include "Bomb.h"
#include "HPBar.h"
#include "CurveIndicator.h"
#include "DebugLog.h"
#include "EntityMonsterA.h"
#include "EntityItemHeal.h"
#include "PlayerState.h"
#include "SlashEffect.h"
#include "SoundManager.h"
#include "SoundComponent.h"
#include "Core.Random.h"
#include "WeaponCapsule.h"
#include "EntityEleteMonster.h"
std::vector<std::string> dashSounds
{
	"Dodge 1_Movement_01",
	"Dodge 1_Movement_02",
	"Dodge 1_Movement_03",
	"Dodge 1_Slow_Armour_01",
	"Dodge 1_Slow_Armour_02",
	"Dodge 1_Slow_Armour_03",
	"Dodge 2_Movement_01",
	"Dodge 2_Movement_02",
	"Dodge 2_Movement_03"
};
std::vector<std::string> stepSounds
{
	"Step_Movement_Small_01",
	"Step_Movement_Small_02",
	"Step_Movement_Small_03"

};
std::vector<std::string> normalBulletSounds
{
	"Electric_Attack_01",
	"Electric_Attack_02",
	"Electric_Attack_03"


};
std::vector<std::string> specialBulletSounds
{
	"Electric_Skill_01",
	"Electric_Skill_02"
};
std::vector<std::string> MeleeChargeSounds
{
	"Nunchaku Attack_Skill_Bass_01",
	"Nunchaku Attack_Skill_Bass_02",
	"Nunchaku Attack_Skill_Bass_03"

};
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

	std::string ShootPosTagName = "ShootTag";
	std::string ActionSoundName = "PlayerActionSound";
	std::string MoveSoundName   = "PlayerMoveSound";
	for (auto& child : childred)
	{
		GameObject* childObj = GameObject::FindIndex(child);
		if (childObj)
		{
			if (childObj->m_tag == ShootPosTagName)
			{
				shootPosObj = childObj;
			}
			else if (childObj->RemoveSuffixNumberTag() == ActionSoundName)
			{
				m_ActionSound = childObj->GetComponent<SoundComponent>();
				
			}
			else if (childObj->RemoveSuffixNumberTag() == MoveSoundName)
			{
				m_MoveSound = childObj->GetComponent<SoundComponent>();
			}
		}
	}

	handSocket = m_animator->MakeSocket("handsocket", "Sword", aniOwner);

	GameObject* uiController{};
	if(0 == playerIndex)
	{
		GameObject* uiController = GameObject::Find("P1_UIController");
		if (uiController)
		{
			auto weaponSlotController = uiController->GetComponent<WeaponSlotController>();
			if (weaponSlotController)
			{
				weaponSlotController->m_AddWeaponHandle = m_AddWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::AddWeapon);
				weaponSlotController->m_UpdateDurabilityHandle = m_UpdateDurabilityEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateDurability);
				weaponSlotController->m_SetActiveHandle = m_SetActiveEvent.AddRaw(weaponSlotController, &WeaponSlotController::SetActive);
				weaponSlotController->m_UpdateChargingPersentHandle = m_ChargingWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateChargingPersent);
				weaponSlotController->m_EndChargingPersentHandle = m_EndChargingEvent.AddRaw(weaponSlotController, &WeaponSlotController::EndChargingPersent);
			}
		}

		auto HPbar = GameObject::Find("P1_HPBar");
		if (HPbar)
		{
			auto hpbar = HPbar->GetComponent<HPBar>();
			if (hpbar)
			{
				hpbar->targetIndex = player->m_index;
				m_currentHP = m_maxHP;
				hpbar->SetMaxHP(m_maxHP);
				hpbar->SetCurHP(m_currentHP);
			}
		}
	}
	else
	{
		GameObject* uiController = GameObject::Find("P2_UIController");
		if (uiController)
		{
			auto weaponSlotController = uiController->GetComponent<WeaponSlotController>();
			if (weaponSlotController)
			{
				weaponSlotController->m_AddWeaponHandle = m_AddWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::AddWeapon);
				weaponSlotController->m_UpdateDurabilityHandle = m_UpdateDurabilityEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateDurability);
				weaponSlotController->m_SetActiveHandle = m_SetActiveEvent.AddRaw(weaponSlotController, &WeaponSlotController::SetActive);
				weaponSlotController->m_UpdateChargingPersentHandle = m_ChargingWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateChargingPersent);
				weaponSlotController->m_EndChargingPersentHandle = m_EndChargingEvent.AddRaw(weaponSlotController, &WeaponSlotController::EndChargingPersent);
			}
		}

		auto HPbar = GameObject::Find("P2_HPBar");
		if (HPbar)
		{
			auto hpbar = HPbar->GetComponent<HPBar>();
			if (hpbar)
			{
				hpbar->targetIndex = player->m_index;
				m_currentHP = m_maxHP;
				hpbar->SetMaxHP(m_maxHP);
				hpbar->SetCurHP(m_currentHP);
				hpbar->SetPlayer2Texture();
			}
		}
	}

	Prefab* basicWeapon = PrefabUtilitys->LoadPrefab("WeaponBasic");
	Weapon* weapon = nullptr;
	if (basicWeapon && player)
	{
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(basicWeapon, "BasicWeapon");
		weapon = weaponObj->GetComponent<Weapon>();
		AddWeapon(weapon);
	}

	Prefab* meleeweapon = PrefabUtilitys->LoadPrefab("WeaponMelee");
	if (meleeweapon && player)
	{
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(meleeweapon, "meleeweapon");
		auto weapon = weaponObj->GetComponent<Weapon>();
		AddWeapon(weapon);
	}
	Prefab* rangeweapon = PrefabUtilitys->LoadPrefab("WeaponWand");
	if (rangeweapon && player)
	{
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(rangeweapon, "rangeweapon");
		auto weapon = weaponObj->GetComponent<Weapon>();
		AddWeapon(weapon);
	}
	Prefab* bombweapon = PrefabUtilitys->LoadPrefab("WeaponBomb");
	if (bombweapon && player)
	{
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(bombweapon, "bombweapon");
		auto weapon = weaponObj->GetComponent<Weapon>();
		AddWeapon(weapon);
	}

	dashObj = SceneManagers->GetActiveScene()->CreateGameObject("dasheffect").get();
	dashEffect = dashObj->AddComponent<EffectComponent>();
	dashEffect->Awake();
	dashEffect->m_effectTemplateName = "testdash";

	auto gmobj = GameObject::Find("GameManager");
	if (gmobj)
	{
		GM = gmobj->GetComponent<GameManager>();
		GM->PushEntity(this);
		GM->PushPlayer(this);
	}

	m_controller = player->GetComponent<CharacterControllerComponent>();

	m_controller->SetAutomaticRotation(true);
	player->SetLayer("Player");
	camera = GameObject::Find("Main Camera");

	Prefab* IndicatorPrefab = PrefabUtilitys->LoadPrefab("Indicator");
	if (IndicatorPrefab)
	{
		Indicator = PrefabUtilitys->InstantiatePrefab(IndicatorPrefab, "Indicator");
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(false);


	}

	Prefab* bombIndicatorPrefab = PrefabUtilitys->LoadPrefab("BombIndicator");
	if(bombIndicatorPrefab)
	{

		BombIndicator = PrefabUtilitys->InstantiatePrefab(bombIndicatorPrefab, "bombIndicator");
		BombIndicator->SetEnabled(false);
	}
	//idle에 move 도포함
	BitFlag idleBit;
	idleBit.Set(PlayerStateFlag::CanMove);
	idleBit.Set(PlayerStateFlag::CanAttack);
	idleBit.Set(PlayerStateFlag::CanGrab);
	idleBit.Set(PlayerStateFlag::CanSwap);
	idleBit.Set(PlayerStateFlag::CanDash);
	playerState["Idle"] = idleBit;

	BitFlag attackBit;
	attackBit.Set(PlayerStateFlag::CanAttack);
	playerState["Attack"] = attackBit;

	//피0 되서 스턴중에는 아무조작도 못함  //스턴일때 화면밖으로(스턴중에는 late update의 화면가두기 제외) 나가면 n초후 아시스 옆으로 위치이동
	BitFlag stunBit;
	playerState["Stun"] = stunBit;

	//grab은 폭탄잡기는 말고 단순 item 들고있을때
	BitFlag grabBit;
	grabBit.Set(PlayerStateFlag::CanMove);
	grabBit.Set(PlayerStateFlag::CanAttack);
	grabBit.Set(PlayerStateFlag::CanThrow);
	grabBit.Set(PlayerStateFlag::CanSwap);
	grabBit.Set(PlayerStateFlag::CanDash);
	playerState["Grab"] = grabBit;

	//throw는 폭탄던지기는 제외하고 단순 item 던질때
	BitFlag throwBit;
	throwBit.Set(PlayerStateFlag::CanMove);
	throwBit.Set(PlayerStateFlag::CanSwap);
	playerState["Throw"] = throwBit;

	//맞는중에는 아무조작도 불가능
	BitFlag hitBit;
	playerState["Hit"] =hitBit;

	//대쉬중에는 아무조작도 불가능
	BitFlag dashBit;
	playerState["Dash"] = dashBit;



	ChangeState("Idle");

	/*auto meshrenderers = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
	for(auto& meshrenderer : meshrenderers)
	{
		meshrenderer->m_Material = meshrenderer->m_Material->Instantiate(meshrenderer->m_Material, "cloneMat");
	}*/
	Debug->Log("Player Start");
	m_animator->SetUseLayer(1, false);
	m_maxHP = maxHP;
	m_currentHP = m_maxHP;
}

void Player::Update(float tick)
{

	m_controller->SetBaseSpeed(moveSpeed);
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	pos.y += 0.5;
	dashObj->m_transform.SetPosition(pos);

	if (catchedObject)
	{
		UpdateChatchObject();
	}

	if (m_nearObject) {
		auto nearMesh = m_nearObject->GetComponent<MeshRenderer>();
		if (nearMesh)
			nearMesh->m_Material->m_materialInfo.m_bitflag = 16;
	}

	if (isAttacking == false && m_comboCount != 0) //&&&&& 콤보카운트 초기화시점 확인필요 지금 0.5초보다 늦게됨 
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
		m_curWeapon->chargingPersent = m_chargingTime / m_curWeapon->chgTime;
		if (!isChargeAttack && m_chargingTime >= m_curWeapon->chgTime) //차징시간이 무기 차징시간보다 길면
		{
			m_curWeapon->isCompleteCharge = true;
		}
		else
		{
			m_curWeapon->isCompleteCharge = false;
		}
		m_ChargingWeaponEvent.UnsafeBroadcast(m_curWeapon, m_weaponIndex);
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
	if (canChangeSlot == false)
	{
		SlotChangeCooldownElapsedTime += tick;
		if (SlotChangeCooldown <= SlotChangeCooldownElapsedTime)
		{
			canChangeSlot = true;
			SlotChangeCooldownElapsedTime = 0;
		}
	}
	if (m_curWeapon->IsBroken() && sucessAttack == true) //무기가 부셔졌고 현재 공격 애니메이션이 방금 끝났으면
	{
		DeleteCurWeapon();
	}
	if (sucessAttack == true) //매프레임 갱신?
	{
		sucessAttack = false;
	}
	if (sucessResurrection == true)
	{
		m_animator->SetParameter("OnResurrection", true);
		sucessResurrection = false;
	}

	if (BombIndicator)
	{
		BombIndicator->SetEnabled(onBombIndicate);
	}

	///test sehwan
	/*if(InputManagement->IsKeyDown('V'))
	{
		auto input = GetOwner()->GetComponent<PlayerInputComponent>();
		input->SetControllerVibration(3.f, 1.0, 1.0, 1.0, 1.0);
	}*/

	if (true == OnInvincibility)
	{
		GracePeriodElpasedTime += tick;
		if (curGracePeriodTime <= GracePeriodElpasedTime)
		{
			OnInvincibility = false;
			GracePeriodElpasedTime = 0.f;
		}
		else
		{
			//깜빡깜빡 tick당한번 or 0.n초당 규철이가 작성할예정
		}
	}




	if (m_animator)
	{
		m_animator->SetParameter("AttackSpeed", MultipleAttackSpeed);
	}
}

void Player::LateUpdate(float tick)
{

	//if (isStun)
	//{
	//	
	//	CameraComponent* camComponent = camera->GetComponent<CameraComponent>();
	//	auto cam = camComponent->GetCamera();
	//	auto camViewProj = cam->CalculateView() * cam->CalculateProjection();
	//	auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	//	XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	//	XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	//	float w = XMVectorGetW(clipSpacePos);
	//	if (w < 0.001f) {
	//		// 원래 위치 반환.
	//		GetOwner()->m_transform.SetPosition(worldpos);
	//		return;
	//	}
	//	XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	//	float x = XMVectorGetX(ndcPos);
	//	float y = XMVectorGetY(ndcPos);
	//	x = abs(x);
	//	y = abs(y);

	//	float clamp_limit = 0.9f;
	//	if (x < clamp_limit && y < clamp_limit)
	//	{
	//		return;
	//	}
	//	{
	//		//이미화면밖임
	//		stunRespawnElapsedTime += tick;
	//		if (stunRespawnTime <= stunRespawnElapsedTime)
	//		{

	//			auto& asiss = GM->GetAsis();
	//			if (!asiss.empty())
	//			{
	//				auto asis = asiss[0]->GetOwner();
	//				Mathf::Vector3 asisPos = asis->m_transform.GetWorldPosition();
	//				Mathf::Vector3 asisForward = asis->m_transform.GetForward();

	//				Mathf::Vector3 newWorldPos = asisPos + Mathf::Vector3{5, 0, 5};

	//				GetOwner()->m_transform.SetPosition(newWorldPos);
	//				stunRespawnElapsedTime = 0;
	//			}
	//		}
	//	}
	//}
	//else
	//{
	//	CameraComponent* camComponent = camera->GetComponent<CameraComponent>();
	//	auto cam = camComponent->GetCamera();
	//	auto camViewProj = cam->CalculateView() * cam->CalculateProjection();
	//	auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	//	XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	//	XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	//	float w = XMVectorGetW(clipSpacePos);
	//	if (w < 0.001f) {
	//		// 원래 위치 반환.
	//		GetOwner()->m_transform.SetPosition(worldpos);
	//		return;
	//	}
	//	XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	//	float x = XMVectorGetX(ndcPos);
	//	float y = XMVectorGetY(ndcPos);
	//	x = abs(x);
	//	y = abs(y);

	//	float clamp_limit = 0.9f;
	//	if (x < clamp_limit && y < clamp_limit)
	//	{
	//		return;
	//	}

	//	XMVECTOR clampedNdcPos = XMVectorClamp(
	//		ndcPos,
	//		XMVectorSet(-clamp_limit, -clamp_limit, 0.0f, 0.0f), // Z는 클램핑하지 않음
	//		XMVectorSet(clamp_limit, clamp_limit, 1.0f, 1.0f)
	//	);
	//	XMVECTOR clampedClipSpacePos = XMVectorScale(clampedNdcPos, w);
	//	XMVECTOR newWorldPos = XMVector3TransformCoord(clampedClipSpacePos, invCamViewProj);

	//	GetOwner()->m_transform.SetPosition(newWorldPos);
	//}
	
}

void Player::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	
	//엘리트 보스몹에게 피격시에만 피격 애니메이션 출력 및DropCatchItem();  그외는 단순 HP깍기 + 캐릭터 깜빡거리는 연출등
	//OnHit();
	//Knockback({ testHitPowerX,testHitPowerY }); //떄린애가 knockbackPower 주기  
	if (sender)
	{
		if (true == IsInvincibility()) return;
		//auto enemy = dynamic_cast<EntityEnemy*>(sender);
		// hit
		//DropCatchItem();
		EntityEleteMonster* elete = dynamic_cast<EntityEleteMonster*>(sender);
		if (elete)
		{
			Transform* transform = GetOwner()->GetComponent<Transform>();
			Mathf::Vector3 myPos = transform->GetWorldPosition();
			Mathf::Vector3 dir =  hitinfo.attakerPos - myPos;
			dir.Normalize();
			dir.y = 0;
			float targetYaw = std::atan2(dir.z, dir.x) - (XM_PI / 2.0);
			targetYaw = -targetYaw;
			DirectX::SimpleMath::Quaternion lookQuat = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(targetYaw, 0, 0);
			transform->SetRotation(lookQuat);

			DirectX::SimpleMath::Vector3 baseForward(0, 0, 1); // 모델 기준 Forward
			DirectX::SimpleMath::Vector3 forward = DirectX::SimpleMath::Vector3::Transform(baseForward, lookQuat);
			if (m_animator)
			{
				m_animator->SetParameter("OnHit", true);
				//Mathf::Vector3 forward = player->m_transform.GetForward();
				Mathf::Vector3 horizontal = -forward * testHitPowerX;
				Mathf::Vector3 knockbackVeocity = Mathf::Vector3{ horizontal.x ,testHitPowerY ,horizontal.z };
				auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
				controller->TriggerForcedMove(knockbackVeocity);
			}
		}
		Damage(damage);
		SetInvincibility(HitGracePeriodTime);
	}
}

void Player::Heal(int healAmount)
{
	m_currentHP = std::min(m_currentHP + healAmount, m_maxHP);
	auto HPbar = GameObject::Find("P1_HPBar"); //이것도 P1인지 P2인지 알아야 함.
	if (HPbar)
	{
		auto hpbar = HPbar->GetComponent<HPBar>();
		if (hpbar)
		{
			hpbar->SetCurHP(m_currentHP);
		}
	}
}

void Player::ChangeState(std::string _stateName)
{
	curStateName = _stateName;
}

bool Player::CheckState(flag _flag)
{
	return playerState[curStateName].Test(_flag);
}

void Player::SetCurHP(int hp)
{
	m_currentHP = hp;
	if (m_currentHP <= 0)
	{
		isStun = true;
		m_animator->SetParameter("OnStun", true);
	}
	auto HPbar = GameObject::Find("P1_HPBar"); //이것도 P1인지 P2인지 알아야 함.
	if (HPbar)
	{
		auto hpbar = HPbar->GetComponent<HPBar>();
		if (hpbar)
		{
			hpbar->SetCurHP(m_currentHP);
		}
	}
}

void Player::Damage(int damage)
{
	m_currentHP -= std::max(damage, 0);
	if (m_currentHP <= 0)
	{
		isStun = true;
		m_animator->SetParameter("OnStun", true);
	}
	auto HPbar = GameObject::Find("P1_HPBar"); //이것도 P1인지 P2인지 알아야 함.
	if (HPbar)
	{
		auto hpbar = HPbar->GetComponent<HPBar>();
		if (hpbar)
		{
			hpbar->SetCurHP(m_currentHP);
		}
	}
}

void Player::Move(Mathf::Vector2 dir)
{
	if (OnMoveBomb)
	{
		MoveBombThrowPosition(dir);
	}
	else
	{
		CharacterMove(dir);
	}
}

void Player::CharacterMove(Mathf::Vector2 dir)
{
	if (!m_isCallStart) return;
	auto controller = player->GetComponent<CharacterControllerComponent>();
	if (!controller) return;
	if (false == CheckState(PlayerStateFlag::CanMove)) return;
	//m_animator->SetUseLayer(1, true);
	//auto worldRot = camera->m_transform.GetWorldQuaternion();
	//Vector3 right = XMVector3Rotate(Vector3::Right, worldRot);
	//Vector3 forward = XMVector3Cross(Vector3::Up, right);// XMVector3Rotate(Vector3::Forward, worldRot);

	//Vector2 moveDir = dir.x * Vector2(right.x, right.z) + - dir.y * Vector2(forward.x, forward.z);
	//moveDir.Normalize();
	controller->Move(dir);
	if (controller->IsOnMove() && dir.LengthSquared() > 1e-6f)
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

void Player::PlaySoundStep()
{
	if (m_MoveSound == nullptr) return;
	int rand = Random<int>(0, stepSounds.size() - 1).Generate();
	m_MoveSound->clipKey = stepSounds[rand];
	m_MoveSound->PlayOneShot();

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
	if (false == CheckState(PlayerStateFlag::CanGrab))  return;
	if (m_nearObject != nullptr && catchedObject == nullptr)
	{

		EntityItem* item = m_nearObject->GetComponent<EntityItem>();
		if (item)
		{
			m_animator->SetParameter("OnGrab", true);
			catchedObject = item;
			catchedObject->GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(true);
			catchedObject->SetThrowOwner(this);
		}


		/*switch (item->itemType)
		{
		case EItemType::Flower:
			Sound->playOneShot();
		case EItemType::Fruit:
			Sound->playOneShot();
		case EItemType::Mineral:
			Sound->playOneShot();
		case EItemType::Mushroom:
			Sound->playOneShot();
		}*/
		WeaponCapsule* weaponCapsule = m_nearObject->GetComponent<WeaponCapsule>();
		if (weaponCapsule)
		{
			weaponCapsule->CatchCapsule(this);
		}

		m_nearObject = nullptr;
	}
}

void Player::Throw()
{
	m_animator->SetParameter("OnThrow", true);
}

void Player::ThrowEvent()
{
	if (catchedObject) {
		catchedObject->Throw(this,player->m_transform.GetForward(), { ThrowPowerX,ThrowPowerY }, onIndicate);
	}
	catchedObject = nullptr;
	m_nearObject = nullptr; //&&&&&
	onIndicate = false;
	if (Indicator)
	{
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(onIndicate);
	}
}

void Player::UpdateChatchObject()
{
	auto forward = GetOwner()->m_transform.GetForward();
	auto world = GetOwner()->m_transform.GetWorldPosition();
	XMVECTOR forwardVec = XMLoadFloat3(&forward);
	XMVECTOR offsetPos = world + forwardVec * 1.0f;
	offsetPos.m128_f32[1] = 1.0f;
	//&&&&& 포지션 소켓에 붙여서 옮겨야 할수도
	catchedObject->GetOwner()->GetComponent<Transform>()->SetPosition(offsetPos);
	//asis와 거리계속 갱신
	if (GM)
	{
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

				if (Indicator)
				{
					auto curveindicator = Indicator->GetComponent<CurveIndicator>();
					curveindicator->EnableIndicator(onIndicate);
					curveindicator->SetIndicator(myPos, asisPos, ThrowPowerY);
				}
			}
			else
			{
				onIndicate = false;
				if (Indicator)
				{
					auto curveindicator = Indicator->GetComponent<CurveIndicator>();
					curveindicator->EnableIndicator(onIndicate);
				}
			}
		}
	}
}

void Player::DropCatchItem()
{
	onIndicate = false;
	if (Indicator)
	{
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(onIndicate);
	}
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
	if (m_curDashCount != 0 && m_dubbleDashElapsedTime >= dubbleDashTime) return; //이미 대시했을 더블대시타임안에 다시안하면 못함
	if (false == CheckState(PlayerStateFlag::CanDash))  return;
	//대쉬 애니메이션중엔 적통과
	m_animator->SetParameter("OnDash", true);
	Mathf::Vector3 forward = player->m_transform.GetForward();
	Mathf::Vector3 horizontal = forward * dashDistacne;
	Mathf::Vector3 knockbackVeocity = Mathf::Vector3{ horizontal.x ,0,horizontal.z };

	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
 	controller->TriggerForcedMove(knockbackVeocity);
	//isDashing = true;
	m_dashCoolElapsedTime = 0.f;
	m_dubbleDashElapsedTime = 0.f;
	m_curDashCount++;
}

void Player::PlaySoundDash()
{
	if (m_MoveSound == nullptr) return;
	int rand = Random<int>(0, dashSounds.size()-1).Generate();
	m_MoveSound->clipKey = dashSounds[rand];
	m_MoveSound->PlayOneShot();
}

void Player::StartAttack()
{
	//여기서 공격처리하고 차징시작 
	if (false == CheckState(PlayerStateFlag::CanAttack)) return;
	if (isAttacking == false || canMeleeCancel == true)
	{
		isChargeAttack = false;
		if (m_curWeapon)
		{
			if (m_curWeapon->isBreak == true) return; //현재무기 부서졌으면 리턴 -> Update에서 무기바꾸기로직으로
			if (m_curWeapon->itemType == ItemType::Melee || m_curWeapon->itemType == ItemType::Basic)
			{
				if (m_comboCount == 0)
				{
					m_animator->SetParameter("MeleeAttack1", true);
				}
				else if (m_comboCount == 1)
				{
					m_animator->SetParameter("MeleeAttack2", true); 
				}
				else if (m_comboCount == 2)
				{
					m_animator->SetParameter("MeleeAttack3", true);
				}
				canMeleeCancel = false;
			}
			if (m_curWeapon->itemType == ItemType::Range)
			{
				if (countRangeAttack != 0 && (countRangeAttack + 1) % countSpecialBullet == 0)
				{
					m_animator->SetParameter("RangeSpecialAttack", true); //원거리 스페셜공격 애니메이션으로 
				}
				else
				{
					m_animator->SetParameter("RangeAttack", true); //원거리 공격 애니메이션으로
				}
			}
			if (m_curWeapon->itemType == ItemType::Bomb)
			{
				bombThrowPositionoffset = { 0,0,0 };
				if (m_animator)
					m_animator->SetParameter("OnTargetBomb", true);
				//현재무기 감추거나 attach떼고 손에붙여서 날아가게?
			}
			startAttack = true; 
		}
	}
}

void Player::Charging()
{
	//폭탄이나 기본무기는 차징없음
	if (m_curWeapon->itemType == ItemType::Bomb || m_curWeapon->IsBasic()) return;

	
	//isAttacking이 false인대도 charging이 실행되면 차징중 --> chargeTime 상승
	if (startAttack == true && isAttacking == false)
	{
		startAttack = false;
		isCharging = true;    //true 일동안 chargeTime 상승중
	}
	//차징 이펙트용으로 chargeStart bool값으로 첫시작때만 effect->apply() 되게끔 넣기
}

void Player::ChargeAttack()  //정리되면 ChargeAttack() 으로 이름바꿀예정
{
	//여기선 차징시간이 넘으면 차징공격실행 아니면 아무것도없음 폭탄은 예외
	isCharging = false; 
	if (m_curWeapon->itemType == ItemType::Bomb)
	{
		m_animator->SetParameter("BombAttack", true); //폭탄 공격 애니메이션으로 //폭탄 내구도소모는 애니메이션 keyframe or bombAttack exit()로
		OnMoveBomb = false;

	}
	else //근거리 and 원거리 
	{
		if (m_chargingTime >= m_curWeapon->chgTime)  //무기별 차징시간 넘었으면
		{
			//차지공격나감
			isChargeAttack = true;
			m_curWeapon->isCompleteCharge = false;
			if (m_curWeapon->itemType == ItemType::Melee)
			{
				m_animator->SetParameter("MeleeChargeAttack", true);
			}
			else if (m_curWeapon->itemType == ItemType::Range)
			{
				m_animator->SetParameter("RangeChargeAttack", true); 
			}
		}
	}


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

void Player::EndAttack()
{
	isAttacking = false;
}

float Player::calculDamge(bool isCharge)
{

	float finalDamge = 0;
	finalDamge += Atk;
	
	//차징일경우 차지데미지  //크리티컬 배율은 맞은애가 따로계산 or 떄릴떄 확인해서 여기서 같이계산
	if (isCharge)
	{
		finalDamge += m_curWeapon->chgAckDmg;
	}
	else
	{
		finalDamge += m_curWeapon->itemAckDmg;
	}
	return finalDamge; //여기에 크리티컬있으면 곱해주기 
}

void Player::PlaySlashEvent()
{

	Prefab* SlashPrefab = PrefabUtilitys->LoadPrefab("SlashEffect1");
	if (SlashPrefab)
	{
		GameObject* SlashObj = PrefabUtilitys->InstantiatePrefab(SlashPrefab, "Slash");
		auto Slashscript = SlashObj->GetComponent<SlashEffect>();
		//현위치에서 offset줘서 정하기
		Mathf::Vector3 myForward = GetOwner()->m_transform.GetForward();
		Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
		float effectOffset = slash1Offset;

		m_ActionSound->clipKey = "Blackguard Sound - Shinobi Fight - Swing Whoosh ";
		if (isChargeAttack)
		{
			effectOffset = slashChargeOffset;
			int rand = Random<int>(0, MeleeChargeSounds.size() - 1).Generate();
			m_ActionSound->clipKey = MeleeChargeSounds[rand];
		}
		Mathf::Vector3 effectPos = myPos + myForward * effectOffset;
		SlashObj->GetComponent<Transform>()->SetPosition(effectPos);


		Mathf::Vector3 up = Mathf::Vector3::Up;
		Quaternion lookRot = Quaternion::CreateFromAxisAngle(up, 0); // 초기값
		lookRot = Quaternion::CreateFromRotationMatrix(Matrix::CreateWorld(Vector3::Zero, myForward, up));

		//Quaternion rot = Quaternion::CreateFromAxisAngle(up, XMConvertToRadians(180.f));
		//Quaternion finalRot = rot * lookRot;
		SlashObj->GetComponent<Transform>()->SetRotation(lookRot);


		Slashscript->Initialize();

		if (m_ActionSound)
		{
			m_ActionSound->PlayOneShot();
		}
	}



}

void Player::PlaySlashEvent2()
{
	Prefab* SlashPrefab = PrefabUtilitys->LoadPrefab("SlashEffect2");
	if (SlashPrefab)
	{
		GameObject* SlashObj = PrefabUtilitys->InstantiatePrefab(SlashPrefab, "Slash");
		auto Slashscript = SlashObj->GetComponent<SlashEffect>();
		//현위치에서 offset줘서 정하기
		Mathf::Vector3 myForward = GetOwner()->m_transform.GetForward();
		Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
		float effectOffset = slash2Offset;
		Mathf::Vector3 effectPos = myPos + myForward * effectOffset;
		SlashObj->GetComponent<Transform>()->SetPosition(effectPos);


		Mathf::Vector3 up = Mathf::Vector3::Up;
		Quaternion lookRot = Quaternion::CreateFromAxisAngle(up, 0); // 초기값
		lookRot = Quaternion::CreateFromRotationMatrix(Matrix::CreateWorld(Vector3::Zero, myForward, up));

		Quaternion rot = Quaternion::CreateFromAxisAngle(up, XMConvertToRadians(270.0f));
		Quaternion finalRot = rot * lookRot;
		SlashObj->GetComponent<Transform>()->SetRotation(finalRot);

		Slashscript->Initialize();
		if (m_ActionSound)
		{
			m_ActionSound->clipKey = "HD Audio - Unarmed Combat - Whoosh Quick";
			m_ActionSound->PlayOneShot();
		}
	}
}

void Player::PlaySlashEvent3()
{
	Prefab* SlashPrefab = PrefabUtilitys->LoadPrefab("SlashEffect3");
	if (SlashPrefab)
	{
		GameObject* SlashObj = PrefabUtilitys->InstantiatePrefab(SlashPrefab, "Slash");
		auto Slashscript = SlashObj->GetComponent<SlashEffect>();
		//현위치에서 offset줘서 정하기
		Mathf::Vector3 myForward = GetOwner()->m_transform.GetForward();
		Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
		Mathf::Vector3 effectPos = myPos;
		SlashObj->GetComponent<Transform>()->SetPosition(effectPos);


		Slashscript->Initialize();
		if (m_ActionSound)
		{
			m_ActionSound->clipKey = "Vadi Sound - Swift - Fast Rope Whoosh Whipping ";
			m_ActionSound->PlayOneShot();
		}
	}
}

bool Player::CheckResurrectionByOther()
{

	std::vector<HitResult> hits;
	OverlapInput reviveInfo;
	Transform transform = GetOwner()->m_transform;
	reviveInfo.layerMask = 1 << 5; //&&&&& Player만 체크하게 바꾸기
	reviveInfo.position = transform.GetWorldPosition();
	reviveInfo.rotation = transform.GetWorldQuaternion();
	PhysicsManagers->SphereOverlap(reviveInfo, ResurrectionRange, hits);

	for (auto& hit : hits)
	{
		auto object = hit.gameObject;
		if (object == GetOwner()) continue;

		auto otehrPlayer = object->GetComponent<Player>();
		if (otehrPlayer)
			return true;

	}

	return false;
}

void Player::Resurrection()
{
	isStun = false;
	sucessResurrection = true;
	Heal(ResurrectionHP);
	ResurrectionElapsedTime = 0;
	SetInvincibility(ResurrectionGracePeriod);
	
}

void Player::SetInvincibility(float _GracePeriodTime)
{
	OnInvincibility = true;
	curGracePeriodTime = _GracePeriodTime;
}

void Player::EndInvincibility()
{
	OnInvincibility = false;
	curGracePeriodTime = 0.f;
	GracePeriodElpasedTime = 0.f;
}

void Player::OnHit()
{
	if (m_animator)
	{
		m_animator->SetParameter("OnHit", true);
	}
}

void Player::Knockback(Mathf::Vector2 _KnockbackForce)
{
	Mathf::Vector3 forward = player->m_transform.GetForward();
	Mathf::Vector3 horizontal = -forward * _KnockbackForce.x;
	Mathf::Vector3 knockbackVeocity = Mathf::Vector3{ horizontal.x ,_KnockbackForce.y ,horizontal.z };

	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->TriggerForcedMove(knockbackVeocity);
}

void Player::SwapWeaponLeft()
{
	if (false == CheckState(PlayerStateFlag::CanSwap)) return;
	if (false == canChangeSlot) return;
	if (true == isCharging) return;
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
		m_SetActiveEvent.UnsafeBroadcast(m_weaponIndex);
		m_UpdateDurabilityEvent.UnsafeBroadcast(m_curWeapon, m_weaponIndex);
		canChangeSlot = false;
		countRangeAttack = 0;
		OnMoveBomb = false;
		onBombIndicate = false;
		m_comboCount = 0;

		/*startAttack = false;
		isAttacking = false;
		m_chargingTime = 0;
		isCharging = false;*/

		CancelChargeAttack();

		

		

		LOG("Swap Left" + std::to_string(m_weaponIndex));
	}
}

void Player::SwapWeaponRight()
{
	if (false == CheckState(PlayerStateFlag::CanSwap)) return;
	if (false == canChangeSlot) return;
	if (true == isCharging) return;
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
		m_SetActiveEvent.UnsafeBroadcast(m_weaponIndex);
		m_UpdateDurabilityEvent.UnsafeBroadcast(m_curWeapon, m_weaponIndex);
		canChangeSlot = false;
		countRangeAttack = 0;
		OnMoveBomb = false;
		onBombIndicate = false;
		m_comboCount = 0;

		/*startAttack = false;
		isAttacking = false;
		m_chargingTime = 0;
		isCharging = false;*/

		CancelChargeAttack(); 

		LOG("Swap Right" + std::to_string(m_weaponIndex));
	}
}

void Player::SwapBasicWeapon()
{
	m_weaponIndex = 0;
	if (m_curWeapon != nullptr)
	{
		m_curWeapon->SetEnabled(false);
		m_curWeapon = m_weaponInventory[m_weaponIndex];
		m_SetActiveEvent.UnsafeBroadcast(m_weaponIndex);
		m_curWeapon->SetEnabled(true);
		canChangeSlot = false;
	}
	countRangeAttack = 0;
	m_comboCount = 0;
}

void Player::AddMeleeWeapon()
{
	Prefab* meleeWeapon = PrefabUtilitys->LoadPrefab("WeaponMelee");
	if (meleeWeapon)
	{
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(meleeWeapon, "MeleeWeapon");
		if(weaponObj)
		{
			auto weapon = weaponObj->GetComponent<Weapon>();
			if (weapon)
			{
				AddWeapon(weapon);
			}
		}
	}
}

bool Player::AddWeapon(Weapon* weapon)
{
	if (!weapon) return false;

	if (m_weaponInventory.size() >= 4)
	{
		weapon->GetOwner()->Destroy();
		return false;

		//TODO : 리턴하고 던져진무기 땅에떨구기 지금은 Destory인대 바꿔야함&&&&&
	}

	weapon->SetEnabled(false);
	int prevSize = m_weaponInventory.size();
	m_weaponInventory.push_back(weapon);
	m_AddWeaponEvent.UnsafeBroadcast(weapon, m_weaponInventory.size() - 1);
	m_UpdateDurabilityEvent.UnsafeBroadcast(weapon, m_weaponIndex);
	handSocket->AttachObject(weapon->GetOwner());
	if (1 >= prevSize)
	{
		if (m_curWeapon)
		{
			m_curWeapon->SetEnabled(false);
		}

		m_curWeapon = weapon;
		m_curWeapon->SetEnabled(true);
		m_SetActiveEvent.UnsafeBroadcast(m_weaponInventory.size() - 1);
		m_weaponIndex = m_weaponInventory.size() - 1;
	}
	return true;
}

void Player::DeleteCurWeapon()
{
	if (!m_curWeapon || m_curWeapon == m_weaponInventory[0]) //기본무기
		return;

	auto it = std::find(m_weaponInventory.begin(), m_weaponInventory.end(), m_curWeapon);

	m_curWeapon->GetOwner()->Destroy();
	if (it != m_weaponInventory.end())
	{
		SwapBasicWeapon();
		handSocket->DetachObject((*it)->GetOwner());
		m_weaponInventory.erase(it);
		for (int i = 1; i < 4; ++i)
		{
			if (i < m_weaponInventory.size())
			{
				m_AddWeaponEvent.UnsafeBroadcast(m_weaponInventory[i], i);
			}
			else
			{
				m_AddWeaponEvent.UnsafeBroadcast(nullptr, i);
			}
		}
	}
}

void Player::FindNearObject(GameObject* _gameObject)
{
	GameObject* gameObject = nullptr;
	if (_gameObject->GetComponent<EntityItem>() != nullptr)
	{
		gameObject = _gameObject;
	}
	if (_gameObject->GetComponent<WeaponCapsule>() != nullptr)
	{
		gameObject = _gameObject;
	}
	if (gameObject == nullptr)  return;
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

void Player::Cancancel()
{
	if (isChargeAttack) return; 

	canMeleeCancel = true;
	int maxCombo = (m_curWeapon->itemType == ItemType::Basic) ? 2 : 3;

	m_comboCount = (m_comboCount + 1) % maxCombo;
}

void Player::CancelChargeAttack()
{
	startAttack = false;
	isAttacking = false;
	m_chargingTime = 0;
	isCharging = false;
}

void Player::MoveBombThrowPosition(Mathf::Vector2 dir)
{
	m_controller->Move({ 0,0 });

	float offsetX = bombMoveSpeed * dir.x;
	float offsetZ = bombMoveSpeed * dir.y;
	bombThrowPositionoffset.x += offsetX;
	bombThrowPositionoffset.z += offsetZ;

	Transform* transform = GetOwner()->GetComponent<Transform>();
	Mathf::Vector3 pos = transform->GetWorldPosition();
	bombThrowPosition = pos + bombThrowPositionoffset;
	onIndicate = true;
	if (BombIndicator)
	{
		BombIndicator->m_transform.SetPosition(bombThrowPosition);
	}

	Mathf::Vector3 targetdir = bombThrowPosition - pos;
	targetdir.Normalize();
	float yaw = atan2(targetdir.x, targetdir.z); // z가 앞, x가 옆일 때
	Quaternion rotation = Quaternion::CreateFromAxisAngle(Mathf::Vector3::Up, yaw);
	

	transform->SetWorldRotation(rotation);
	


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
		distacne = m_curWeapon->itemAckRange; //사거리도 차지면 다름
	}
	float damage = calculDamge(isChargeAttack);

	unsigned int layerMask = 1 << 0 | 1 << 8 | 1 << 10;

	int size = RaycastAll(rayOrigin, direction, distacne, layerMask, hits);

	constexpr float angle = XMConvertToRadians(15.0f);
	Vector3 leftDir = Vector3::Transform(direction, Matrix::CreateRotationY(-angle));
	leftDir.Normalize();
	Vector3 rightDir = Vector3::Transform(direction, Matrix::CreateRotationY(angle));
	rightDir.Normalize();
	std::vector<HitResult> leftHits;
	int leftSize = RaycastAll(rayOrigin, leftDir, distacne, layerMask, leftHits);
	std::vector<HitResult> rightHits;
	int rightSize = RaycastAll(rayOrigin, rightDir, distacne, layerMask, rightHits);
	std::vector<HitResult> allHits;
	allHits.reserve(size + leftSize + rightSize);
	allHits.insert(allHits.end(), hits.begin(), hits.end());
	allHits.insert(allHits.end(), leftHits.begin(), leftHits.end());
	allHits.insert(allHits.end(), rightHits.begin(), rightHits.end());
	for (auto& hit : allHits)
	{
		Mathf::Vector3 pos = hit.point;
		auto object = hit.gameObject;
		if (object == nullptr || object == GetOwner()) continue;
		auto entity = object->GetComponentDynamicCast<Entity>();
		if (entity) {
			auto [iter, inserted] = AttackTarget.insert(entity);
			if (inserted) (*iter)->SendDamage(this, damage);
		}
	}
}

void Player::RangeAttack()
{
	//원거리 무기 일때 에임보정후 발사
	auto playerPos = GetOwner()->m_transform.GetWorldPosition();
	float distance;

	inRangeEnemy.clear();
	curTarget = nullptr;
	nearDistance = FLT_MAX;
	//inRangeEnemy 담기
	//

	std::vector<HitResult> hits;
	OverlapInput RangeInfo;
	RangeInfo.layerMask = 1 << 8 | 1 << 10;; //일단 다떄림
	Transform transform = GetOwner()->m_transform;
	RangeInfo.position = transform.GetWorldPosition();
	RangeInfo.rotation = transform.GetWorldQuaternion();
	PhysicsManagers->SphereOverlap(RangeInfo, rangedAutoAimRange, hits);

	for (auto& hit : hits)
	{
		auto object = hit.gameObject;
		if (object == GetOwner()) continue;
		if (auto enemy = object->GetComponentDynamicCast<Entity>())  
		{
			Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 enemyPos = object->m_transform.GetWorldPosition();
			Mathf::Vector3 directionToEnemy = enemyPos - myPos;
			directionToEnemy.Normalize();
			float dot = directionToEnemy.Dot(GetOwner()->m_transform.GetForward());
			if (dot > cosf(Mathf::Deg2Rad * rangeAngle * 0.5f))
			{
				auto [iter, inserted] = inRangeEnemy.insert(enemy);
			}
		}
	}

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
		//원거리 공격
		Transform* transform = GetOwner()->GetComponent<Transform>();

		Mathf::Vector3 myPos = transform->GetWorldPosition();
		Mathf::Vector3 targetPos = curTarget->GetOwner()->m_transform.GetWorldPosition();
		DirectX::SimpleMath::Vector3 dir = targetPos - myPos;
		dir.y = 0; // 상하 회전 무시
		dir.Normalize();
		float targetYaw = std::atan2(dir.z, dir.x) - (XM_PI / 2.0);
		targetYaw = -targetYaw;
		DirectX::SimpleMath::Quaternion lookQuat = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(targetYaw, 0, 0);
		transform->SetRotation(lookQuat);

	}


	nearDistance = FLT_MAX;
}

void Player::ShootBullet()
{

	if (isChargeAttack)  //차지어택은 1 , 3 , 5 등 홀수갯수 발사 메인타겟 좌우 각도로 한발씩
	{
		ShootChargeBullet();
		countRangeAttack = 0;
	}
	else
	{
		if (countRangeAttack != 0 && (countRangeAttack + 1) % countSpecialBullet == 0)
		{
			ShootSpecialBullet();
			countRangeAttack = 0;
		}
		else
		{
			ShootNormalBullet();
			countRangeAttack++;
		}
	}
	
}

void Player::ShootNormalBullet()
{
	Prefab* bulletprefab = PrefabUtilitys->LoadPrefab("BulletNormal");
	if (bulletprefab && player)
	{
		GameObject* bulletObj = PrefabUtilitys->InstantiatePrefab(bulletprefab, "bullet");
		NormalBullet* bullet = bulletObj->GetComponent<NormalBullet>();
		Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
		if (shootPosObj)
		{
			pos = shootPosObj->m_transform.GetWorldPosition();
		}
		
		if (m_curWeapon)
		{
			bullet->Initialize(this, pos, player->m_transform.GetForward(), calculDamge());
		}

		if (m_ActionSound)
		{
			int rand = Random<int>(0, normalBulletSounds.size() - 1).Generate();
			m_ActionSound->clipKey = normalBulletSounds[rand];
			m_ActionSound->PlayOneShot();
		}
	}
}

void Player::ShootSpecialBullet()
{
	//Todo:: pool에서찾고 없으면 프리팹에서 생성
	Prefab* bulletprefab = PrefabUtilitys->LoadPrefab("BulletSpecial");
	if (bulletprefab && player)
	{
		GameObject* bulletObj = PrefabUtilitys->InstantiatePrefab(bulletprefab, "specialbullet");
		SpecialBullet* bullet = bulletObj->GetComponent<SpecialBullet>();
		Mathf::Vector3  pos = player->m_transform.GetWorldPosition();

		if (shootPosObj)
		{
			pos = shootPosObj->m_transform.GetWorldPosition();
		}
		if (m_curWeapon)
		{
			bullet->Initialize(this, pos, player->m_transform.GetForward(), calculDamge(true));
		}
		int rand = Random<int>(0, specialBulletSounds.size() - 1).Generate();
		if (m_ActionSound)
		{
			m_ActionSound->clipKey = specialBulletSounds[rand];
			m_ActionSound->PlayOneShot();
		}
	}

	
}

void Player::ShootChargeBullet()
{
	Prefab* bulletprefab = PrefabUtilitys->LoadPrefab("BulletNormal");
	Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
	if (bulletprefab && player)
	{
		int halfCount = m_curWeapon->ChargeAttackBulletCount / 2;
		if (shootPosObj)
		{
			pos = shootPosObj->m_transform.GetWorldPosition();
		}
		Mathf::Vector3 OrgionShootDir = player->m_transform.GetForward();

		if (m_curWeapon)
		{
			for (int i = -halfCount; i <= halfCount; i++)
			{
				GameObject* bulletObj = PrefabUtilitys->InstantiatePrefab(bulletprefab, "bullet");
				NormalBullet* bullet = bulletObj->GetComponent<NormalBullet>();
				int Shootangle = m_curWeapon->ChargeAttackBulletAngle * i;
				Mathf::Vector3 ShootDir = XMVector3TransformNormal(OrgionShootDir,
					XMMatrixRotationY(XMConvertToRadians(Shootangle)));
				bullet->Initialize(this, pos, ShootDir, m_curWeapon->chgAckDmg);
			}
		}
	}

}

void Player::ThrowBomb()
{
	Prefab* bombprefab = PrefabUtilitys->LoadPrefab("Bomb");
	if (bombprefab)
	{
		GameObject* bombObj = PrefabUtilitys->InstantiatePrefab(bombprefab, "Bomb");
		Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
		bombObj->GetComponent<Transform>()->SetPosition(pos);
		Bomb* bomb = bombObj->GetComponent<Bomb>();
		bomb->ThrowBomb(this, pos, bombThrowPosition, calculDamge());
		onBombIndicate = false;
		m_curWeapon->SetEnabled(false);
	}

}


void Player::OnTriggerEnter(const Collision& collision)
{
	if (collision.thisObj == collision.otherObj)
		return;

	auto healItem = collision.otherObj->GetComponent<EntityItemHeal>();
	if (healItem && healItem->CanHeal() ==true)
	{
		Heal(healItem->GetHealAmount());
		healItem->Use();
	}
	


}
void Player::OnTriggerStay(const Collision& collision)
{
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



void Player::TestHit()
{
	DropCatchItem();
	if (m_animator)
	{
		m_animator->SetParameter("OnHit", true);
	}

	Mathf::Vector3 forward = player->m_transform.GetForward();

	Mathf::Vector3 horizontal = -forward * testHitPowerX;
	Mathf::Vector3 knockbackVeocity = Mathf::Vector3{ horizontal.x ,testHitPowerY ,horizontal.z };
	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->TriggerForcedMove(knockbackVeocity);
	//넉백이 끝날떄까지 x z testHitPowerX  // y testHitPowerY;

}
