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

#include "CurveIndicator.h"
#include "DebugLog.h"
#include "EntityMonsterA.h"
#include "EntityItemHeal.h"
#include "PlayerState.h"
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

	std::string ShootPosObjName = "RangeShootPos";
	for (auto& child : childred)
	{
		GameObject* childObj = GameObject::FindIndex(child);
		if (childObj)
		{
			if (childObj->RemoveSuffixNumberTag() == ShootPosObjName)
			{
				shootPosObj = childObj;
				break;
			}
		}
	}


	handSocket = m_animator->MakeSocket("handsocket", "Sword", aniOwner);

	//TEST : UIController 현재는 테스트라 P1_UIController로 고정
	//TODO : 관련해서 플레이어 컴포넌트가 본인 인덱스를 알아야 함.
	GameObject* uiController = GameObject::Find("P1_UIController");
	if (uiController)
	{
		auto weaponSlotController = uiController->GetComponent<WeaponSlotController>();
		if (weaponSlotController)
		{
			weaponSlotController->m_awakeEventHandle = m_AddWeaponEvent.AddRaw(weaponSlotController, &WeaponSlotController::AddWeapon);
			weaponSlotController->m_UpdateDurabilityHandle = m_UpdateDurabilityEvent.AddRaw(weaponSlotController, &WeaponSlotController::UpdateDurability);
			weaponSlotController->m_SetActiveHandle = m_SetActiveEvent.AddRaw(weaponSlotController, &WeaponSlotController::SetActive);
		}
	}
	//~TEST

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
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(meleeweapon, "BasicWeapon");
		auto weapon = weaponObj->GetComponent<Weapon>();
		AddWeapon(weapon);
	}
	Prefab* rangeweapon = PrefabUtilitys->LoadPrefab("WeaponWand");
	if (rangeweapon && player)
	{
		GameObject* weaponObj = PrefabUtilitys->InstantiatePrefab(rangeweapon, "BasicWeapon");
		auto weapon = weaponObj->GetComponent<Weapon>();
		AddWeapon(weapon);
	}
	
	dashObj = SceneManagers->GetActiveScene()->CreateGameObject("dasheffect").get();
	dashEffect = dashObj->AddComponent<EffectComponent>();
	dashEffect->Awake();
	dashEffect->m_effectTemplateName = "testdash";

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

	Prefab* IndicatorPrefab = PrefabUtilitys->LoadPrefab("Indicator");
	if (IndicatorPrefab)
	{
		Indicator = PrefabUtilitys->InstantiatePrefab(IndicatorPrefab, "Indicator");
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(false);
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
	playerState["Grab"] = grabBit;
	grabBit.Set(PlayerStateFlag::CanMove);
	grabBit.Set(PlayerStateFlag::CanAttack);
	grabBit.Set(PlayerStateFlag::CanThrow);
	grabBit.Set(PlayerStateFlag::CanSwap);
	grabBit.Set(PlayerStateFlag::CanDash);

	//throw는 폭탄던지기는 제외하고 단순 item 던질때
	BitFlag throwBit;
	playerState["Throw"] =throwBit;
	throwBit.Set(PlayerStateFlag::CanMove);
	throwBit.Set(PlayerStateFlag::CanSwap);

	//맞는중에는 아무조작도 불가능
	BitFlag hitBit;
	playerState["Hit"] =hitBit;

	//대쉬중에는 아무조작도 불가능
	BitFlag dashBit;
	playerState["Dash"] = dashBit;

	ChangeState("Idle");

}

void Player::Update(float tick)
{
	m_controller->SetBaseSpeed(moveSpeed);
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	pos.y += 0.5;
	dashObj->m_transform.SetPosition(pos);

	if (catchedObject)
	{
		auto forward = GetOwner()->m_transform.GetForward();
		auto world = GetOwner()->m_transform.GetWorldPosition(); 
		XMVECTOR forwardVec = XMLoadFloat3(&forward); 
		XMVECTOR offsetPos = world + forwardVec * 1.0f;
		offsetPos.m128_f32[1] = 1.0f; 
		//&&&&& 포지션 소켓에 붙여서 옮겨야 할수도
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

				if (Indicator)
				{
					auto curveindicator = Indicator->GetComponent<CurveIndicator>();
					curveindicator->EnableIndicator(onIndicate);
					curveindicator->SetIndicator(myPos, asisPos, ThrowPowerY);
				}
				LOG("onIndicate!!!!!!!!!");
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
	
	if (m_curWeapon->IsBroken() && sucessAttack == true) //무기가 부셔졌고 현재 공격 애니메이션이 방금 끝났으면
	{
		DeleteCurWeapon();
	}

	if (sucessAttack == true) //매프레임 갱신?
	{
		sucessAttack = false;
	}
}

void Player::LateUpdate(float tick)
{

	if (isStun)
	{

	}
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

	//float x = XMVectorGetX(ndcPos);
	//float y = XMVectorGetY(ndcPos);
	//x = abs(x);
	//y = abs(y);

	//float clamp_limit = 0.9f;
	//if(x < clamp_limit && y < clamp_limit)
	//{
	//	return;
	//}

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
	//test sehwan
	{
		m_currentHP -= std::max(damage, 0);
		if (m_currentHP <= 0)
		{
			isStun = true;
			m_animator->SetParameter("OnStun", true);
		}
	}

	
	//엘리트 보스몹에게 피격시에만 피격 애니메이션 출력 및DropCatchItem();  그외는 단순 HP깍기 + 캐릭터 깜빡거리는 연출등
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
				isStun = true;
				m_animator->SetParameter("OnStun", true);
			}
		}
	}
}

void Player::Heal(int healAmount)
{
	m_currentHP = std::max(m_currentHP + healAmount, m_maxHP);
}

void Player::ChangeState(std::string _stateName)
{
	curStateName = _stateName;
}

bool Player::CheckState(flag _flag)
{
	return playerState[curStateName].Test(_flag);
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
	m_animator->SetUseLayer(1, true);
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
	LOG("ThrowEvent");
	if (catchedObject) {
		//catchedObject->SetThrowOwner(this);
		catchedObject->Throw(this,player->m_transform.GetForward(), { ThrowPowerX,ThrowPowerY }, onIndicate);
	}
	catchedObject = nullptr;
	m_nearObject = nullptr; //&&&&&
	if (m_curWeapon)
		m_curWeapon->SetEnabled(true); //이건 해당상태 state ->exit 쪽으로 이동필요
	onIndicate = false;
	if (Indicator)
	{
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(onIndicate);
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
	DropCatchItem();
	//대쉬 애니메이션중엔 적통과
	m_animator->SetParameter("OnDash", true);
	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->SetKnockBack(dashDistacne, 0.f);

	//isDashing = true;
	m_dashCoolElapsedTime = 0.f;
	m_dubbleDashElapsedTime = 0.f;
	m_dashElapsedTime = 0.f;
	m_curDashCount++;
}

void Player::StartAttack()
{
	//isCharging = true;
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
				OnMoveBomb = true;
				bombThrowPositionoffset = { 0,0,0 };
				if (m_animator)
					m_animator->SetParameter("OnMove", false);

				//현재무기 감추거나 attach떼고 손에붙여서 날아가게?
			}
		}
	}
}

void Player::Charging()
{
	//폭탄이나 기본무기는 차징없음
	if (m_curWeapon->itemType == ItemType::Bomb || m_curWeapon->IsBasic()) return;
	//isAttacking이 false인대도 charging이 실행되면 차징중 --> chargeTime 상승
	if (isAttacking == false)
	{
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
			if (m_curWeapon->itemType == ItemType::Melee)
			{
				m_animator->SetParameter("MeleeChargeAttack", true);
			}
			else if (m_curWeapon->itemType == ItemType::Range)
			{
					m_animator->SetParameter("RangeChargeAttack", true); //원거리 공격 애니메이션으로 
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

float Player::calculDamge(bool isCharge,int _chargeCount)
{
	float finalDamge = 0;
	finalDamge += Atk;
	finalDamge += m_curWeapon->itemAckDmg;
	if (isCharge)
	{
		finalDamge += m_curWeapon->chgDmgscal * _chargeCount;
	}
	return finalDamge; //여기에 크리티컬있으면 곱해주기 
}

bool Player::CheckResurrectionByOther()
{

	std::vector<HitResult> hits;
	OverlapInput reviveInfo;
	Transform transform = GetOwner()->m_transform;
	reviveInfo.layerMask = 1u; //&&&&& Player만 체크하게 바꾸기
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
	m_animator->SetParameter("OnResurrection", true);
	Heal(ResurrectionHP);
	ResurrectionElapsedTime = 0;
	
	//m_currentHP = ResurrectionHP;
}

void Player::OnHit()
{
	DropCatchItem();
	if (m_animator)
	{
		m_animator->SetParameter("OnHit", true);
	}
}

void Player::Knockback(Mathf::Vector2 KnockbackpowerXY)
{
	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->SetKnockBack(KnockbackpowerXY.x, KnockbackpowerXY.y);
}



void Player::SwapWeaponLeft()
{
	if (false == CheckState(PlayerStateFlag::CanSwap)) return;
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
		m_SetActiveEvent.Broadcast(m_weaponIndex);
		m_UpdateDurabilityEvent.Broadcast(m_curWeapon, m_weaponIndex);
	}
	countRangeAttack = 0;
	m_comboCount = 0;
}

void Player::SwapWeaponRight()
{
	if (false == CheckState(PlayerStateFlag::CanSwap)) return;
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
		m_SetActiveEvent.Broadcast(m_weaponIndex);
		m_UpdateDurabilityEvent.Broadcast(m_curWeapon, m_weaponIndex);
	}
	countRangeAttack = 0;
}

void Player::SwapBasicWeapon()
{
	m_weaponIndex = 0;
	if (m_curWeapon != nullptr)
	{
		m_curWeapon->SetEnabled(false);
		m_curWeapon = m_weaponInventory[m_weaponIndex];
		m_SetActiveEvent.Broadcast(m_weaponIndex);
		m_curWeapon->SetEnabled(true);
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

void Player::AddWeapon(Weapon* weapon)
{
	if (!weapon) return;

	if (m_weaponInventory.size() >= 4)
	{
		weapon->GetOwner()->Destroy();
		return;

		//TODO : 리턴하고 던져진무기 땅에떨구기 지금은 Destory인대 바꿔야함&&&&&
	}

	weapon->SetEnabled(false);
	int prevSize = m_weaponInventory.size();
	m_weaponInventory.push_back(weapon);
	m_AddWeaponEvent.Broadcast(weapon, m_weaponInventory.size() - 1);
	m_UpdateDurabilityEvent.Broadcast(weapon, m_weaponIndex);
	handSocket->AttachObject(weapon->GetOwner());
	if (1 >= prevSize)
	{
		if (m_curWeapon)
		{
			m_curWeapon->SetEnabled(false);
		}

		m_curWeapon = weapon;
		m_curWeapon->SetEnabled(true);
		m_SetActiveEvent.Broadcast(m_weaponInventory.size() - 1);
		m_weaponIndex = m_weaponInventory.size() - 1;
	}
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
				m_AddWeaponEvent.Broadcast(m_weaponInventory[i], i);
			}
			else
			{
				m_AddWeaponEvent.Broadcast(nullptr, i);
			}
		}
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

void Player::Cancancel()
{
	if (isChargeAttack) return; 

	canMeleeCancel = true;
	int maxCombo = (m_curWeapon->itemType == ItemType::Basic) ? 2 : 3;

	m_comboCount = (m_comboCount + 1) % maxCombo;
	std::cout << "CobmoCount =====" << m_comboCount  << std::endl;
}

void Player::MoveBombThrowPosition(Mathf::Vector2 dir)
{
	m_controller->Move({ 0,0 });
	bombThrowPositionoffset.x += dir.x;
	bombThrowPositionoffset.z += dir.y;

	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	bombThrowPosition = pos + bombThrowPositionoffset;
	onIndicate = true;
	if (Indicator)
	{
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(onIndicate);
		curveindicator->SetIndicator(pos, bombThrowPosition, ThrowPowerY);
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
	rayOrigin.y = 0.5f;
	
	float distacne = 2.0f;
	if (m_curWeapon)
	{
		distacne = m_curWeapon->itemAckRange; //사거리도 차지면 다름
	}
	float damage = calculDamge(isChargeAttack, chargeCount);

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
			if (object == nullptr || object == GetOwner()) continue;
			auto entity = object->GetComponentDynamicCast<Entity>();
			if (entity) {
				auto [iter, inserted] = AttackTarget.insert(entity);
				if (inserted) (*iter)->SendDamage(this, 100);
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
	RangeInfo.layerMask = 1u; //일단 다떄림
	Transform transform = GetOwner()->m_transform;
	RangeInfo.position = transform.GetWorldPosition();
	RangeInfo.rotation = transform.GetWorldQuaternion();
	PhysicsManagers->SphereOverlap(RangeInfo, rangeDistacne, hits);

	for (auto& hit : hits)
	{
		auto object = hit.gameObject;
		if (object == GetOwner()) continue;
		if (auto enemy = object->GetComponent<EntityMonsterA>())  //entity로 묶어서 받아지는지 확인필요 --- 자원오브젝트,몬스터 A B C 등 묶어서
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
			bullet->Initialize(this, pos, player->m_transform.GetForward(), m_curWeapon->itemAckDmg);
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
			bullet->Initialize(this, pos, player->m_transform.GetForward(), m_curWeapon->itemAckDmg);
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
				bullet->Initialize(this, pos, ShootDir, m_curWeapon->chgDmgscal);
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
		bomb->ThrowBomb(this, pos,bombThrowPosition);
	}

	onIndicate = false;
	if (Indicator)
	{
		auto curveindicator = Indicator->GetComponent<CurveIndicator>();
		curveindicator->EnableIndicator(onIndicate);
	}
	//bomb->ThrowBomb(this, bombThrowPosition);
	//bomb 을 프리팹만든걸로 받아오게끔 수정 or weaponPool 필요 
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
	//LOG("player muunga boodit him trigger" << collision.otherObj->m_name.ToString().c_str());
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


void Player::TestKillPlayer()
{
	SendDamage(nullptr,100);
}

void Player::TestHit()
{
	DropCatchItem();
	if (m_animator)
	{
		m_animator->SetParameter("OnHit", true);
	}

	auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->SetKnockBack(testHitPowerX, testHitPowerY);

}
