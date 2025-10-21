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
#include "SFXPoolManager.h"
#include "SoundName.h"
#include "SwordHitEffect.h"
#include "PlayEffectAll.h"
#include "ObjectPoolManager.h"
#include "ObjectPool.h"
#include "ControllerVibration.h"
#include "SwordProjectile.h"
#include "SwordProjectileEffect.h"
#include "EntityMonsterTower.h"
#include "EntityMonsterBaseGate.h"
#include "GameInstance.h"
void Player::Awake()
{
	auto gmobj = GameObject::Find("GameManager");
	if (gmobj)
	{
		GM = gmobj->GetComponent<GameManager>();
		GM->PushEntity(this);
		GM->PushPlayer(this);
	}
}
void Player::Start()
{
	m_maxHitImpulseSize = 1.0f;
	m_maxHitImpulseDuration = 0.2f;

	HitImpulseStart();
	player = GetOwner();
	
	auto childred = player->m_childrenIndices;
	m_transform = player->GetComponent<Transform>();
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
	std::string SpecialActionSound = "PlayerSpecialActionSound";
	std::string DamageSound = "PlayerDamageSound";
	for (auto& child : childred)
	{
		GameObject* childObj = GameObject::FindIndex(child);
		if (childObj)
		{
			if (childObj->m_tag == ShootPosTagName)
			{
				shootPosObj = childObj;
			}
			else if (childObj->m_tag == ActionSoundName)
			{
				m_ActionSound = childObj->GetComponent<SoundComponent>();
				
			}
			else if (childObj->m_tag == MoveSoundName)
			{
				m_MoveSound = childObj->GetComponent<SoundComponent>();
			}
			else if (childObj->m_tag == SpecialActionSound)
			{
				m_SpecialActionSound = childObj->GetComponent<SoundComponent>();

			}
			else if (childObj->m_tag == DamageSound)
			{
				m_DamageSound = childObj->GetComponent<SoundComponent>();
			}
		}
	}

	handSocket = m_animator->MakeSocket("handsocket", "Sword", aniOwner);

	leftEarSokcet = m_animator->MakeSocket("leftEarsocket", "ear_L", aniOwner);
	rightEarSokcet = m_animator->MakeSocket("rightEarsocket", "ear_R", aniOwner);

	stunObj = SceneManagers->GetActiveScene()->CreateGameObject("StunEffect").get();
	if (stunObj)
	{
		stunEffect = stunObj->AddComponent<EffectComponent>();
		stunEffect->m_effectTemplateName = "Stun";
	}
	Prefab* healprefab = PrefabUtilitys->LoadPrefab("HealEffect");
	if (healprefab && player) 
	{
		GameObject* healEffcet = PrefabUtilitys->InstantiatePrefab(healprefab, "healeffcet");
		healEffect = healEffcet->GetComponentDynamicCast<EffectComponent>();
	}
	Prefab* chargeprefab = PrefabUtilitys->LoadPrefab("ChargeEffect");
	if (chargeprefab && player)
	{
		GameObject* chargeEffcetObj = PrefabUtilitys->InstantiatePrefab(chargeprefab, "chargeEffect");
		chargeEffect = chargeEffcetObj->GetComponentDynamicCast<EffectComponent>();
	}

	Prefab* Resurrprefab = PrefabUtilitys->LoadPrefab("ResurrectionEffect");
	if (Resurrprefab)
	{
		GameObject* ResurrpreObj = PrefabUtilitys->InstantiatePrefab(Resurrprefab, "resurrEffect");
		resurrectionEffect = ResurrpreObj->GetComponentDynamicCast<EffectComponent>();
	}


	if(0 == playerIndex)
	{
		m_uiController = GameObject::Find("P1_UIController");
		if (m_uiController)
		{
			auto weaponSlotController = m_uiController->GetComponent<WeaponSlotController>();
			if (weaponSlotController)
			{
				weaponSlotController->m_AddWeaponHandle = m_AddWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::AddWeapon);
				weaponSlotController->m_UpdateDurabilityHandle = m_UpdateDurabilityEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateDurability);
				weaponSlotController->m_SetActiveHandle = m_SetActiveEvent.AddRaw(weaponSlotController, &WeaponSlotController::SetActive);
				weaponSlotController->m_UpdateChargingPersentHandle = m_ChargingWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateChargingPersent);
				weaponSlotController->m_EndChargingPersentHandle = m_EndChargingEvent.AddRaw(weaponSlotController, &WeaponSlotController::EndChargingPersent);
			}
		}

		m_HPbar = GameObject::Find("P1_HPBar");
		if (m_HPbar)
		{
			auto hpbar = m_HPbar->GetComponent<HPBar>();
			if (hpbar)
			{
				hpbar->targetIndex = player->m_index;
				m_maxHP = maxHP;
				m_currentHP = m_maxHP;
				hpbar->SetMaxHP(m_maxHP);
				hpbar->SetCurHP(m_currentHP);
				hpbar->SetType(0);
				hpbar->SetTarget(player->shared_from_this());
				hpbar->Init();
			}
		}
	}
	else
	{
		m_uiController = GameObject::Find("P2_UIController");
		if (m_uiController)
		{
			auto weaponSlotController = m_uiController->GetComponent<WeaponSlotController>();
			if (weaponSlotController)
			{
				weaponSlotController->m_AddWeaponHandle = m_AddWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::AddWeapon);
				weaponSlotController->m_UpdateDurabilityHandle = m_UpdateDurabilityEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateDurability);
				weaponSlotController->m_SetActiveHandle = m_SetActiveEvent.AddRaw(weaponSlotController, &WeaponSlotController::SetActive);
				weaponSlotController->m_UpdateChargingPersentHandle = m_ChargingWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateChargingPersent);
				weaponSlotController->m_EndChargingPersentHandle = m_EndChargingEvent.AddRaw(weaponSlotController, &WeaponSlotController::EndChargingPersent);
			}
		}

		m_HPbar = GameObject::Find("P2_HPBar");
		if (m_HPbar)
		{
			auto hpbar = m_HPbar->GetComponent<HPBar>();
			if (hpbar)
			{
				hpbar->targetIndex = player->m_index;
				m_maxHP = maxHP;
				m_currentHP = m_maxHP;
				hpbar->SetMaxHP(m_maxHP);
				hpbar->SetCurHP(m_currentHP);
				hpbar->SetType(1);
				hpbar->SetTarget(player->shared_from_this());
				hpbar->Init();
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


	Prefab* run = PrefabUtilitys->LoadPrefab("run1");
	if (run && player) {
		for (int i = 0; i < 5; i++) {
			GameObject* runeffect = PrefabUtilitys->InstantiatePrefab(run, "runEff");
			EffectComponent* ef = runeffect->GetComponentDynamicCast<EffectComponent>();
			m_runEffects.push_back(ef);
		}
	}

	dashObj = SceneManagers->GetActiveScene()->CreateGameObject("dasheffect").get();
	if (dashObj)
	{
		dashEffect = dashObj->AddComponent<EffectComponent>();
		dashEffect->m_effectTemplateName = "testdash";
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

	Debug->Log("Player Start");
	m_animator->SetUseLayer(1, false);
	m_maxHP = maxHP;
	m_currentHP = m_maxHP;



	Prefab* SlashPrefab = PrefabUtilitys->LoadPrefab("SlashEffect1");
	if (SlashPrefab)
	{
		slash1 = PrefabUtilitys->InstantiatePrefab(SlashPrefab, "Slash1");
	}
	Prefab* SlashPrefab2 = PrefabUtilitys->LoadPrefab("SlashEffect2");
	if (SlashPrefab2)
	{
		slash2 = PrefabUtilitys->InstantiatePrefab(SlashPrefab2, "Slash2");
	}
	Prefab* SlashPrefab3 = PrefabUtilitys->LoadPrefab("SlashEffect3");
	if (SlashPrefab3)
	{
		slash3 = PrefabUtilitys->InstantiatePrefab(SlashPrefab3, "Slash3");
	}


	Prefab* LSlashPrefab = PrefabUtilitys->LoadPrefab("LSlashEffect1");
	if (LSlashPrefab)
	{
		Lslash1 = PrefabUtilitys->InstantiatePrefab(LSlashPrefab, "LSlash1");
	}
	Prefab* LSlashPrefab2 = PrefabUtilitys->LoadPrefab("LSlashEffect2");
	if (LSlashPrefab2)
	{
		Lslash2 = PrefabUtilitys->InstantiatePrefab(LSlashPrefab2, "LSlash2");
	}
	Prefab* LSlashPrefab3 = PrefabUtilitys->LoadPrefab("LSlashEffect3");
	if (LSlashPrefab3)
	{
		Lslash3 = PrefabUtilitys->InstantiatePrefab(LSlashPrefab3, "LSlash3");
	}

}

void Player::Update(float tick)
{
	Cheat(); 
	DetectResource();
	HitImpulseUpdate(tick);
	m_controller->SetBaseSpeed(moveSpeed);
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	if(dashObj)
	{
		Mathf::Vector3 dashPos = pos;
		dashPos.y += 0.5f;
		dashObj->m_transform.SetPosition(dashPos);
	}
	if (healEffect)
	{
		Mathf::Vector3 healPos = pos;
		healPos.y += 1.0f;
		healEffect->GetOwner()->m_transform.SetPosition(healPos);
	}
	if (chargeEffect)
	{
		Mathf::Vector3 chargePos = pos;
		chargePos.y += 0.7f;
		chargeEffect->GetOwner()->m_transform.SetPosition(chargePos);
	}
	if (resurrectionEffect)
	{
		Mathf::Vector3 resurrPos = pos;
		resurrPos.y += 1.4f;
		resurrectionEffect->GetOwner()->m_transform.SetPosition(resurrPos);
	}
	if (catchedObject)
	{
		UpdateChatchObject();
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
	if (isAttacking == false && canRapidfire)
	{
		rapidfireElapsedTime += tick;
		if (rapidfireElapsedTime >= rapidfireTime)
		{
			canRapidfire = false;
			rapidfireElapsedTime = 0;
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

	if (true == OnInvincibility)
	{
		GracePeriodElpasedTime += tick;
		if (curGracePeriodTime <= GracePeriodElpasedTime)
		{
			OnInvincibility = false;
			GracePeriodElpasedTime = 0.f;
			if (OnresurrectionEffect)
			{
				OnresurrectionEffect = false;
				resurrectionEffect->StopEffect();
			}
			if (onHit)
			{
				onHit = false;
			}
		}
		else
		{
			if(onHit) //맞아서 무적인경우
			{
				blinkElaspedTime += tick;
				if (blinkElaspedTime >= m_maxHitImpulseDuration)
				{
					HitImpulse();
					blinkElaspedTime = 0.f;
				}
			}

		}
	}

	if (m_animator)
	{
		m_animator->SetParameter("AttackSpeed", MultipleAttackSpeed);
	}

}

void Player::LateUpdate(float tick)
{

	if (GM&& GM->TestCameraControll == false)
	{
		if (isStun)
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

			float x = XMVectorGetX(ndcPos);
			float y = XMVectorGetY(ndcPos);
			x = abs(x);
			y = abs(y);

			float clamp_limit = 0.9f;
			if (x < clamp_limit && y < clamp_limit)
			{
				return;
			}
			{
				//이미화면밖임
				stunRespawnElapsedTime += tick;
				if (stunRespawnTime <= stunRespawnElapsedTime)
				{

					auto& asiss = GM->GetAsis();
					if (!asiss.empty())
					{
						auto asis = asiss[0]->GetOwner();
						Mathf::Vector3 asisPos = asis->m_transform.GetWorldPosition();
						Mathf::Vector3 asisForward = asis->m_transform.GetForward();

						Mathf::Vector3 newWorldPos = asisPos + Mathf::Vector3{ 5, 0, 5 };

						GetOwner()->m_transform.SetPosition(newWorldPos);
						stunRespawnElapsedTime = 0;
					}
				}
			}
		}
		else
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

			float x = XMVectorGetX(ndcPos);
			float y = XMVectorGetY(ndcPos);
			x = abs(x);
			y = abs(y);

			float clamp_limit = 0.9f;
			if (x < clamp_limit && y < clamp_limit)
			{
				return;
			}

			XMVECTOR clampedNdcPos = XMVectorClamp(
				ndcPos,
				XMVectorSet(-clamp_limit, -clamp_limit, 0.0f, 0.0f), // Z는 클램핑하지 않음
				XMVectorSet(clamp_limit, clamp_limit, 1.0f, 1.0f)
			);
			XMVECTOR clampedClipSpacePos = XMVectorScale(clampedNdcPos, w);
			XMVECTOR newWorldPos = XMVector3TransformCoord(clampedClipSpacePos, invCamViewProj);

			GetOwner()->m_transform.SetPosition(newWorldPos);
		}
	}
	
}

void Player::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{

	//엘리트 보스몹에게 피격시에만 피격 애니메이션 출력 및DropCatchItem();  그외는 단순 HP깍기 + 캐릭터 깜빡거리는 연출등
	//OnHit();
	//Knockback({ testHitPowerX,testHitPowerY }); //떄린애가 knockbackPower 주기  
	if (sender)
	{
		if (true == IsInvincibility()) return;
		if (true == isStun) return;
		//auto enemy = dynamic_cast<EntityEnemy*>(sender);
		// hit
		//DropCatchItem();
		EntityEleteMonster* elete = dynamic_cast<EntityEleteMonster*>(sender);
		Damage(damage);
		if (elete && isStun == false) //엘리트고 아직 죽지않았으면
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
				HitKnockbackPower = hitinfo.KnockbackForce;
				HItKnockbackTime = hitinfo.KnockbackTime;
				Mathf::Vector3 horizontal = -forward * HitKnockbackPower.x;
				Mathf::Vector3 knockbackVeocity = Mathf::Vector3{ horizontal.x ,HitKnockbackPower.y ,horizontal.z };
				auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
				controller->TriggerForcedMove(knockbackVeocity);

				if (true == GameInstance::GetInstance()->IsViveEnabled())
				{
					auto input = GetOwner()->GetComponent<PlayerInputComponent>();
					if (GM)
					{
						auto data = GM->GetControllerVibration();
						if (data)
						{
							float power = data->PlayerHitPower;
							float time = data->PlayerHitTime;
							input->SetControllerVibration(time, power, power, power, power);
						}
					}
				}
			}
		}

		if (m_DamageSound)
		{
			int rand = Random<int>(0, DamageSounds.size() - 1).Generate();
			m_DamageSound->clipKey = DamageSounds[rand];
			m_DamageSound->PlayOneShot();
		}
		SetInvincibility(HitGracePeriodTime);
	}
}

void Player::Heal(int healAmount)
{
	m_currentHP = std::min(m_currentHP + healAmount, m_maxHP);
	if (m_HPbar)
	{
		auto hpbar = m_HPbar->GetComponent<HPBar>();
		if (hpbar)
		{
			hpbar->SetCurHP(m_currentHP);
		}
	}
	healEffect->Apply();
	//힐 이펙트 출력
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
	if (m_HPbar)
	{
		auto hpbar = m_HPbar->GetComponent<HPBar>();
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
	if (m_HPbar)
	{
		auto hpbar = m_HPbar->GetComponent<HPBar>();
		if (hpbar)
		{
			hpbar->SetCurHP(m_currentHP);
		}
	}
	onHit = true;
	HitImpulse();
}

void Player::Move(Mathf::Vector2 dir)
{
	if (!m_isCallStart) return;
	if(GM && GM->TestCameraControll)
	{ 
		return;
	}
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

	m_runIndex = (m_runIndex + 1) % m_runEffects.size();
	auto pos = GetOwner()->m_transform.GetWorldPosition();
	pos += -GetOwner()->m_transform.GetForward() * 0.3f;
	pos.m128_f32[1] += 0.3f;
	m_runEffects[m_runIndex]->GetOwner()->m_transform.SetWorldPosition(pos);
	m_runEffects[m_runIndex]->Apply();
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

		EntityItem* item = m_nearObject->GetComponentDynamicCast<EntityItem>();
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
		WeaponCapsule* weaponCapsule = m_nearObject->GetComponentDynamicCast<WeaponCapsule>();
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
			if (dot > cosf(Mathf::Deg2Rad * detectAngle * 0.5f) && detectDistance < distance)
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
					if (canRapidfire)
					{
						m_animator->SetParameter("RangeAttack", true); //원거리 공격 애니메이션으로
					}
					else
					{
						m_animator->SetParameter("RangeAttackReady", true);
					}
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

		if (chargeEffect)
		{
			chargeEffect->Apply();
		}
		if (m_SpecialActionSound)
		{
				int rand = Random<int>(0, MeleeChargingSounds.size() - 1).Generate();
				m_SpecialActionSound->clipKey = MeleeChargingSounds[rand];
				m_SpecialActionSound->PlayOneShot();
		}
	}
}

void Player::ChargeAttack()  
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
		if (chargeEffect)
		{
			chargeEffect->StopEffect();
		}
		if (m_chargingTime >= m_curWeapon->chgTime)  //무기별 차징시간 넘었으면
		{
			//차지공격나감
			isChargeAttack = true;
			m_curWeapon->isCompleteCharge = false;


			if (true == GameInstance::GetInstance()->IsViveEnabled())
			{
				auto input = GetOwner()->GetComponent<PlayerInputComponent>();
				if (GM)
				{
					auto data = GM->GetControllerVibration();
					if (data)
					{
						float power = data->PlayerChargePower;
						float time = data->PlayerChargeTime;
						input->SetControllerVibration(time, power);
					}
				}
			}
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
	if (isChargeAttack == true) return;
	startRay = true;
}

void Player::EndRay()
{
	if (isChargeAttack == true) return;
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

	if (isChargeAttack == false)
	{
		
		GameObject* Slash = nullptr;
		if (m_curWeapon->GetItemType() == ItemType::Basic)
		{
			Slash = slash1;
		}
		else if (m_curWeapon->GetItemType() == ItemType::Melee)
		{
			Slash = Lslash1;
		}
		if (!Slash) return;
		auto Slashscript = Slash->GetComponent<SlashEffect>();
		Mathf::Vector3 myForward = GetOwner()->m_transform.GetForward();
		Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
		float effectOffset = slash1Offset;
		Mathf::Vector3 effectPos = myPos + myForward * effectOffset;
		effectPos.y += 0.9f;
		Slash->GetComponent<Transform>()->SetPosition(effectPos);


		Mathf::Vector3 up = Mathf::Vector3::Up;
		Quaternion lookRot = Quaternion::CreateFromAxisAngle(up, 0); // 초기값
		lookRot = Quaternion::CreateFromRotationMatrix(Matrix::CreateWorld(Vector3::Zero, myForward, up));

		Slash->GetComponent<Transform>()->SetRotation(lookRot);
		Slashscript->Initialize();

		if (m_ActionSound)
		{
			m_ActionSound->clipKey = "Blackguard Sound - Shinobi Fight - Swing Whoosh ";
			m_ActionSound->PlayOneShot();
		}
	}
}

void Player::PlaySlashEvent2()
{
	if (slash2)
	{
		GameObject* Slash = nullptr;
		if (m_curWeapon->GetItemType() == ItemType::Basic)
		{
			Slash = slash2;
		}
		else if (m_curWeapon->GetItemType() == ItemType::Melee)
		{
			Slash = Lslash2;
		}
		if (!Slash) return;
		auto Slashscript = Slash->GetComponent<SlashEffect>();
		//현위치에서 offset줘서 정하기
		Mathf::Vector3 myForward = GetOwner()->m_transform.GetForward();
		Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
		float effectOffset = slash2Offset;
		Mathf::Vector3 effectPos = myPos + myForward * effectOffset;
		effectPos.y += 0.9f;
		Slash->GetComponent<Transform>()->SetPosition(effectPos);


		Mathf::Vector3 up = Mathf::Vector3::Up;
		Quaternion lookRot = Quaternion::CreateFromAxisAngle(up, 0); // 초기값
		lookRot = Quaternion::CreateFromRotationMatrix(Matrix::CreateWorld(Vector3::Zero, myForward, up));

		Quaternion rot = Quaternion::CreateFromAxisAngle(up, XMConvertToRadians(270.0f));
		Quaternion finalRot = rot * lookRot;
		Slash->GetComponent<Transform>()->SetRotation(finalRot);

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
	if (Lslash3)
	{
		GameObject* Slash = nullptr;
		if (m_curWeapon->GetItemType() == ItemType::Melee)
		{
			Slash = Lslash3;
		}
		if (!Slash) return;
		auto Slashscript = Slash->GetComponent<SlashEffect>();
		//현위치에서 offset줘서 정하기
		Mathf::Vector3 myForward = GetOwner()->m_transform.GetForward();
		Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
		Mathf::Vector3 effectPos = myPos;
		effectPos.y += 0.9f;
		Slash->GetComponent<Transform>()->SetPosition(effectPos);


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
		if (otehrPlayer && otehrPlayer->isStun == false)
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
	if (resurrectionEffect)
	{
		OnresurrectionEffect = true;
		resurrectionEffect->Apply();
	}
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

void Player::SendKnockBack(Entity* sender,Mathf::Vector2 _KnockbackForce)
{
	Mathf::Vector3 forward = player->m_transform.GetForward();
	Mathf::Vector3 horizontal = -forward * _KnockbackForce.x;
	Mathf::Vector3 knockbackVeocity = Mathf::Vector3{ horizontal.x ,_KnockbackForce.y ,horizontal.z };

	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->TriggerForcedMove(knockbackVeocity);
}

void Player::Cheat()
{
	if (InputManagement->IsKeyDown('1'))
	{
		Prefab* meleeweapon = PrefabUtilitys->LoadPrefab("WeaponMelee");
		if (meleeweapon && player)
		{
			GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(meleeweapon, "meleeweapon");
			auto weapon = weaponObj->GetComponent<Weapon>();
			AddWeapon(weapon);
		}
	}
	if (InputManagement->IsKeyDown('2'))
	{
		Prefab* rangeweapon = PrefabUtilitys->LoadPrefab("WeaponWand");
		if (rangeweapon && player)
		{
			GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(rangeweapon, "rangeweapon");
			auto weapon = weaponObj->GetComponent<Weapon>();
			AddWeapon(weapon);
		}
	}
	if (InputManagement->IsKeyDown('3'))
	{
		Prefab* bombweapon = PrefabUtilitys->LoadPrefab("WeaponBomb");
		if (bombweapon && player)
		{
			GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(bombweapon, "bombweapon");
			auto weapon = weaponObj->GetComponent<Weapon>();
			AddWeapon(weapon);
		}
	}
	
}

void Player::DetectResource()
{
	std::vector<HitResult> hits;
	OverlapInput RangeInfo;
	RangeInfo.layerMask = 1 << 8 | 1 << 9 ; 
	Transform transform = GetOwner()->m_transform;
	RangeInfo.position = transform.GetWorldPosition();
	PhysicsManagers->SphereOverlap(RangeInfo, detectRadius, hits);

	for (auto& hit : hits)
	{
		auto object = hit.gameObject;
		if (object == GetOwner()) continue;
		if (auto entity = object->GetComponentDynamicCast<Entity>())
		{
			entity->OnOutLine();

		}
	}
}

void Player::SwapWeaponInternal(int dir)
{
	if (false == CheckState(PlayerStateFlag::CanSwap)) return;
	if (false == canChangeSlot) return;
	if (true == isCharging) return;
	if (m_weaponInventory.empty()) return;

	int adjustedDirection = dir;
	if (playerIndex == 1)
	{
		adjustedDirection *= -1;
	}

	const int maxInventoryIndex = static_cast<int>(m_weaponInventory.size()) - 1;
	const int maxAllowedIndex = std::max(0, std::min(3, maxInventoryIndex));
	int preIndex = m_weaponIndex;
	m_weaponIndex = std::clamp(m_weaponIndex + adjustedDirection, 0, maxAllowedIndex);
	bool isChange = false;
	if (preIndex != m_weaponIndex)
		isChange = true;
	if (isChange == false) return;
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
		canRapidfire = false;

		CancelChargeAttack();

		if (m_ActionSound)
		{
			int rand = Random<int>(0, weaponSwapSounds.size() - 1).Generate();
			m_ActionSound->clipKey = weaponSwapSounds[rand];
			m_ActionSound->PlayOneShot();
		}

		if (adjustedDirection < 0)
		{
			LOG("Swap Left" + std::to_string(m_weaponIndex));
		}
		else
		{
			LOG("Swap Right" + std::to_string(m_weaponIndex));
		}
	}
}

void Player::SwapWeaponLeft()
{
	SwapWeaponInternal(-1);
}

void Player::SwapWeaponRight()
{
	SwapWeaponInternal(1);
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

		if (m_ActionSound)
		{
			int rand = Random<int>(0, weaponSwapSounds.size() - 1).Generate();
			m_ActionSound->clipKey = weaponSwapSounds[rand];
			m_ActionSound->PlayOneShot();
		}
	}
	CancelChargeAttack();
	countRangeAttack = 0;
	m_comboCount = 0;
	canRapidfire = false;
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
	weapon->Initialize();
	m_weaponInventory.push_back(weapon);
	m_AddWeaponEvent.UnsafeBroadcast(weapon, m_weaponInventory.size() - 1);
	//m_UpdateDurabilityEvent.UnsafeBroadcast(weapon, m_weaponIndex);
	handSocket->AttachObject(weapon->GetOwner());
	if (1 >= prevSize)
	{
		if (m_curWeapon)
		{
			m_curWeapon->SetEnabled(false);
		}

		m_curWeapon = weapon;
		m_curWeapon->SetEnabled(true);
		m_UpdateDurabilityEvent.UnsafeBroadcast(weapon, m_weaponIndex);
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
		if (m_curWeapon->itemType != ItemType::Bomb) //basic인건 위에서 검사하니까 붐이아닌지만 체크
		{
			int rand = Random<int>(0, WeaponBreakSounds.size() - 1).Generate();
			m_SpecialActionSound->clipKey = WeaponBreakSounds[rand];
			m_SpecialActionSound->PlayOneShot();
		}


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
	canRapidfire = false;
	if (chargeEffect)
	{
		chargeEffect->StopEffect();
	}
}

void Player::MoveBombThrowPosition(Mathf::Vector2 dir)
{
	m_controller->Move({ 0,0 });

	float offsetX = bombMoveSpeed * dir.x;
	float offsetZ = bombMoveSpeed * dir.y;
	bombThrowPositionoffset.x += offsetX;
	bombThrowPositionoffset.z += offsetZ;

	bombThrowPositionoffset.x = std::clamp(bombThrowPositionoffset.x, -MaxThrowDistance, MaxThrowDistance);
	bombThrowPositionoffset.z = std::clamp(bombThrowPositionoffset.z, -MaxThrowDistance, MaxThrowDistance);
	Transform* transform = GetOwner()->GetComponent<Transform>();
	Mathf::Vector3 pos = transform->GetWorldPosition();



	bombThrowPosition = pos + bombThrowPositionoffset;



	Mathf::Vector3 targetdir = bombThrowPosition - pos;
	targetdir.Normalize();
	float yaw = atan2(targetdir.x, targetdir.z); // z가 앞, x가 옆일 때
	Quaternion rotation = Quaternion::CreateFromAxisAngle(Mathf::Vector3::Up, yaw);


	transform->SetWorldRotation(rotation);

	Mathf::Vector3 forwardRayOrgion = pos;

	std::vector<HitResult>  forwardhits;
	float forwardDistance = std::max(std::abs(bombThrowPositionoffset.x), std::abs(bombThrowPositionoffset.z));
	unsigned int forwardLayerMask = 1 << 11;
	int size = RaycastAll(forwardRayOrgion, targetdir,forwardDistance, forwardLayerMask, forwardhits);
	float min = 0;
	for (auto& forwardHit : forwardhits)
	{
		if (forwardHit.gameObject->m_tag != "Wall")
		{
			continue;
		}
		if (min == 0)
		{
			min = std::abs((forwardRayOrgion - forwardHit.point).Length());


			bombThrowPosition = forwardHit.point;

		}
		else
		{
			float newMin = std::abs((forwardRayOrgion - forwardHit.point).Length());

			if (newMin < min)
			{
				min = newMin;
				bombThrowPosition = forwardHit.point;
				
			}
		}

	}
	

	Mathf::Quaternion bombrotat{};
	if (camera)
	{
		Mathf::Vector3 UpPos = bombThrowPosition;  //가장위
		UpPos.y += 100.f;
		Mathf::Vector3 dir = bombThrowPosition - UpPos;

		Mathf::Vector3 rayOrigin = UpPos;
		dir.Normalize();
		std::vector<HitResult> hits;

		float distacne = 100.0f;

		unsigned int layerMask = 1 << 11;
		bombThrowPosition.y = pos.y + 0.1f;
		int size = RaycastAll(rayOrigin, dir, distacne, layerMask, hits);
		
		for (auto& hit : hits)
		{
			if (hit.gameObject->m_tag == "Wall")
			{
				continue;
			}

			bombThrowPosition.y = hit.point.y + 0.1f;
		}

	}

	//
	//onIndicate = true;
	if (BombIndicator)
	{
		BombIndicator->m_transform.SetPosition(bombThrowPosition);
	}

	



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
	rayOrigin.y +=  0.5f;
	
	float distacne = 2.0f;
	if (m_curWeapon)
	{
		distacne = m_curWeapon->itemAckRange; 
	}
	if (isChargeAttack)
		distacne = m_curWeapon->chgRange;
	float damage = calculDamge(isChargeAttack);

	unsigned int layerMask = 1 << 0 | 1 << 8 | 1 << 10 | 1<< 15;

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
		auto object = hit.gameObject;
		if (object == nullptr || object == GetOwner()) continue;
		auto entity = object->GetComponentDynamicCast<Entity>();
		if (entity) {
			auto [iter, inserted] = AttackTarget.insert(entity);
			HitInfo hitinfo;
			hitinfo.itemType = m_curWeapon->itemType;
			hitinfo.hitPos = hit.point;
			hitinfo.attakerPos = GetOwner()->m_transform.GetWorldPosition();
			hitinfo.hitNormal = hit.normal;
			hitinfo.KnockbackForce = { m_curWeapon->itemKnockback ,0,m_curWeapon->itemKnockback };
			if (inserted) (*iter)->SendDamage(this, damage, hitinfo);
			if (GM)
			{
				auto pool = GM->GetSFXPool();
				if (pool)
				{
					int rand = Random<int>(0, MeleeStrikeSounds.size() - 1).Generate();
					pool->PlayOneShot(MeleeStrikeSounds[rand]);
				}
			}

			
		}
	}
}

void Player::MeleeChargeAttack()
{
	if (!GM || GM->GetObjectPoolManager() == nullptr) return;
	if (isChargeAttack == false) return;
	auto poolmanager = GM->GetObjectPoolManager();
	auto swordProjec = poolmanager->GetSwordProjectile();
	GameObject* SwordObj = swordProjec->Pop();
	if (SwordObj)
	{
		SwordProjectile* Projectile = SwordObj->GetComponentDynamicCast<SwordProjectile>();
		Mathf::Vector3  myPos = player->m_transform.GetWorldPosition();
		Mathf::Vector3  myForward = player->m_transform.GetForward();

		Mathf::Vector3 effectPos = myPos + myForward * slashChargeOffset;
		effectPos.y += 0.9f;


		Mathf::Vector3 up = Mathf::Vector3::Up;
		Quaternion lookRot = Quaternion::CreateFromAxisAngle(up, 0); // 초기값
		lookRot = Quaternion::CreateFromRotationMatrix(Matrix::CreateWorld(Vector3::Zero, myForward, up));
		SwordObj->GetComponent<Transform>()->SetRotation(lookRot);



		if (m_curWeapon)
		{
			Projectile->Initialize(this, effectPos, player->m_transform.GetForward(), calculDamge(true));

		}
		int rand = Random<int>(0, MeleeChargeSounds.size() - 1).Generate();
		if (m_ActionSound)
		{
			m_ActionSound->clipKey = MeleeChargeSounds[rand];
			m_ActionSound->PlayOneShot();
		}
	}
}

void Player::RangeAttack()
{
	//원거리 무기 일때 에임보정후 발사
	auto playerPos = GetOwner()->m_transform.GetWorldPosition();
	float distance;
	nearTarget = false;
	inRangeEnemy.clear();
	std::unordered_set<Entity*> enemis; // 몹들만담기
	curTarget = nullptr;
	nearDistance = FLT_MAX;
	//inRangeEnemy 담기
	//

	std::vector<HitResult> hits;
	OverlapInput RangeInfo;
	RangeInfo.layerMask = 1 << 8 | 1 << 10 | 1<< 15;
	Transform transform = GetOwner()->m_transform;
	RangeInfo.position = transform.GetWorldPosition();
	RangeInfo.rotation = transform.GetWorldQuaternion();
	PhysicsManagers->SphereOverlap(RangeInfo, rangedAutoAimRange, hits);

	for (auto& hit : hits)
	{
		auto object = hit.gameObject;
		if (object == GetOwner()) continue;
		if (auto entity = object->GetComponentDynamicCast<Entity>())  
		{
			Mathf::Vector3 myPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 enemyPos = object->m_transform.GetWorldPosition();
			Mathf::Vector3 directionToEnemy = enemyPos - myPos;
			directionToEnemy.Normalize();
			float dot = directionToEnemy.Dot(GetOwner()->m_transform.GetForward());
			if (dot > cosf(Mathf::Deg2Rad * rangeAngle * 0.5f))
			{
				auto [iter, inserted] = inRangeEnemy.insert(entity);
				if (entity->GetOwner()->m_layer == "Enemy" || entity->GetOwner()->m_layer == "EnemyHome")
				{
					auto [iter, inserted] = enemis.insert(entity);
				}
				
			}
		}
	}
	if (enemis.empty())
	{
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
	}
	else
	{
		for (auto enemy : enemis)
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

		float length = (targetPos - myPos).Length();
		length = std::abs(length);
		if (length <= 2.0f)
		{
			nearTarget = true;
		}
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
	if (!GM || GM->GetObjectPoolManager() == nullptr) return;
	auto poolmanager = GM->GetObjectPoolManager();
	auto normalBullets = poolmanager->GetNormalBulletPool();
	GameObject* bulletObj = normalBullets->Pop();
	Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
	Mathf::Vector3 shootPos = pos;

	if (bulletObj)
	{
		NormalBullet* bullet = bulletObj->GetComponent<NormalBullet>();
		if (shootPosObj)
		{
			shootPos = shootPosObj->m_transform.GetWorldPosition();
		}
		
		if (nearTarget)
		{
			shootPos.x = pos.x;
			shootPos.z = pos.z;
		}
		if (m_curWeapon)
		{
			bullet->Initialize(this, shootPos, player->m_transform.GetForward(), calculDamge());
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
	if (!GM || GM->GetObjectPoolManager() == nullptr) return;
	auto poolmanager = GM->GetObjectPoolManager();
	auto specialBullets = poolmanager->GetSpecialBulletPool();
	GameObject* bulletObj = specialBullets->Pop();
	Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
	Mathf::Vector3 shootPos = pos;


	if (bulletObj)
	{
		SpecialBullet* bullet = bulletObj->GetComponent<SpecialBullet>();
		

		if (shootPosObj)
		{
			shootPos = shootPosObj->m_transform.GetWorldPosition();
		}
		if (nearTarget)
		{
			shootPos.x = pos.x;
			shootPos.z = pos.z;
		}
		if (m_curWeapon)
		{
			bullet->Initialize(this, shootPos, player->m_transform.GetForward(), calculDamge(true));
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
	if (!GM || GM->GetObjectPoolManager() == nullptr) return;
	auto poolmanager = GM->GetObjectPoolManager();
	auto normalBullets = poolmanager->GetNormalBulletPool();
	Mathf::Vector3  pos = player->m_transform.GetWorldPosition();
	Mathf::Vector3 shootPos = pos;
	std::vector<GameObject*> chargePool;
	for (int i = 0; i < 5; i++)
	{
		GameObject* bulletObj = normalBullets->Pop();
		chargePool.push_back(bulletObj);
	}
	if (!chargePool.empty())
	{
		int halfCount = m_curWeapon->ChargeAttackBulletCount / 2;


		
		if (shootPosObj)
		{
			shootPos = shootPosObj->m_transform.GetWorldPosition();
		}

		if (nearTarget)
		{
			shootPos.x = pos.x;
			shootPos.z = pos.z;
		}
		Mathf::Vector3 OrgionShootDir = player->m_transform.GetForward();

		if (m_curWeapon)
		{
			for (int i = -halfCount; i <= halfCount; i++)
			{
				if (chargePool.empty()) return;
				NormalBullet* bullet = chargePool.back()->GetComponent<NormalBullet>();
				chargePool.pop_back();
				int Shootangle = m_curWeapon->ChargeAttackBulletAngle * i;
				Mathf::Vector3 ShootDir = XMVector3TransformNormal(OrgionShootDir,
					XMMatrixRotationY(XMConvertToRadians(Shootangle)));
				bullet->Initialize(this, shootPos, ShootDir, m_curWeapon->chgAckDmg);
			}
		}

		if (m_ActionSound)
		{
			m_ActionSound->clipKey = RangeChargeSounds[0];
			m_ActionSound->PlayOneShot();
		}
	}




}

void Player::ThrowBomb()
{

	if (!GM || GM->GetObjectPoolManager() == nullptr) return;
	auto poolmanager = GM->GetObjectPoolManager();
	auto bombs = poolmanager->GetBombPool();
	GameObject* bombObj = bombs->Pop();
	
	if (bombObj)
	{
		
		Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
		bombObj->GetComponent<Transform>()->SetPosition(pos);
		Bomb* bomb = bombObj->GetComponent<Bomb>();
		bomb->ThrowBomb(this, pos, bombThrowPosition, m_curWeapon->bombThrowDuration,m_curWeapon->bombRadius, calculDamge());
		onBombIndicate = false;
		m_curWeapon->SetEnabled(false);
		
		if(m_ActionSound)
		{
			m_ActionSound->clipKey = "Weapon_Whoosh_Low_2";
			m_ActionSound->PlayOneShot();
		}
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
		m_nearObject = nullptr;
	}
}


void PlayHitEffect(GameObject* _hitowner, HitInfo hitinfo)
{
	Prefab* HitPrefab = nullptr;
	if (hitinfo.itemType == ItemType::Basic || hitinfo.itemType == ItemType::Melee)
	{
		//근접공격타격이벤트
		if(hitinfo.isCritical == false)
			HitPrefab = PrefabUtilitys->LoadPrefab("SwordHitEffect");
		else
			HitPrefab = PrefabUtilitys->LoadPrefab("CriticaHitEffect");

		if (HitPrefab)
		{
			GameObject* HitObj = PrefabUtilitys->InstantiatePrefab(HitPrefab, "HitEffect");
			auto swordHitEffect = HitObj->GetComponent<PlayEffectAll>();
			Transform* hitTransform = HitObj->GetComponent<Transform>();
			hitTransform->SetPosition(hitinfo.hitPos);
			Vector3 normal = hitinfo.hitNormal;
			normal.Normalize();

			// 보조 업 벡터 (노말이랑 평행하지 않게 선택)
			Vector3 up = Vector3::UnitY;
			if (fabsf(up.Dot(normal)) > 0.99f)
				up = Vector3::UnitX;

			// 오른쪽 벡터
			Vector3 right = up.Cross(normal);
			right.Normalize();

			// 다시 업 보정
			up = normal.Cross(right);
			up.Normalize();

			// 회전행렬 → 쿼터니언
			Matrix rotMat;
			rotMat.Right(right);
			rotMat.Up(up);
			rotMat.Forward(normal);

			Quaternion rot = Quaternion::CreateFromRotationMatrix(rotMat);
			hitTransform->SetRotation(rot);
			swordHitEffect->Initialize();
		}

	}
	else if (hitinfo.itemType == ItemType::Range)
	{
		if (hitinfo.bulletType == BulletType::Normal)
		{
			if (hitinfo.isCritical == false)
				 HitPrefab = PrefabUtilitys->LoadPrefab("BulletNormalHit");
			else
				 HitPrefab = PrefabUtilitys->LoadPrefab("CriticaHitEffect");
			if (HitPrefab)
			{
				GameObject* HirObj = PrefabUtilitys->InstantiatePrefab(HitPrefab, "HitEffect");
				auto rangeHitEffect = HirObj->GetComponent<PlayEffectAll>();
				Transform* hitTransform = HirObj->GetComponent<Transform>();
				hitTransform->SetPosition(hitinfo.hitPos);

				rangeHitEffect->Initialize();
			}
		}
		else
		{
			if (hitinfo.isCritical == false)
				HitPrefab = PrefabUtilitys->LoadPrefab("BulletSpecialHit");
			else
				HitPrefab = PrefabUtilitys->LoadPrefab("CriticaHitEffect");
			if (HitPrefab)
			{
				GameObject* HirObj = PrefabUtilitys->InstantiatePrefab(HitPrefab, "HitEffect");
				auto rangeHitEffect = HirObj->GetComponent<PlayEffectAll>(); 
				Transform* hitTransform = HirObj->GetComponent<Transform>();
				hitTransform->SetPosition(hitinfo.hitPos);

				rangeHitEffect->Initialize();
			}
		}
	}
}
