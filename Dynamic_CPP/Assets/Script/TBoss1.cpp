#include "TBoss1.h"
#include "pch.h"
#include "CharacterControllerComponent.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include "RigidBodyComponent.h"
#include "Animator.h"
#include <utility>
#include "Core.Random.h"
#include "BP003.h"
#include "BP001.h"
#include "GameManager.h"
#include "Player.h"
#include "CriticalMark.h"
#include "Weapon.h"
#include "PlayEffectAll.h"
#include "TimeSystem.h"
void TBoss1::Start()
{
	BT = m_pOwner->GetComponent<BehaviorTreeComponent>();
	BB = BT->GetBlackBoard();
	m_rigid = m_pOwner->GetComponent<RigidBodyComponent>();

	//춘식이는 블렉보드에서 포인트 잡아놓을꺼다. 나중에 바꾸던가 말던가
	bool hasChunsik = BB->HasKey("Chunsik");
	if (hasChunsik) {
		m_chunsik = BB->GetValueAsGameObject("Chunsik");
	}

	auto childred = m_pOwner->m_childrenIndices;
	for (auto& child : childred)
	{
		auto animator = GameObject::FindIndex(child)->GetComponent<Animator>();

		if (animator)
		{
			m_animator = animator;
			break;
		}

	}
	if (m_animator) {
		m_anicontroller = m_animator->m_animationControllers[0].get();
	}

	std::string markTag = "CriticalMark";
	for (auto& child : childred)
	{
		auto Obj = GameObject::FindIndex(child);

		if (Obj->m_tag == markTag)
		{
			m_criticalMark = Obj->GetComponent<CriticalMark>();
			break;
		}

	}

	//prefab load
	Prefab* BP001Prefab = PrefabUtilitys->LoadPrefab("Boss1BP001Obj");
	Prefab* BP003Prefab = PrefabUtilitys->LoadPrefab("Boss1BP003Obj");

	////1번 패턴 투사체 최대 10개
	for (size_t i = 0; i < 10; i++) {
		std::string Projectilename = "BP001Projectile" +std::to_string(i);
		GameObject* Prefab1 = PrefabUtilitys->InstantiatePrefab(BP001Prefab, Projectilename);
		Prefab1->SetEnabled(false);
		BP001Objs.push_back(Prefab1);
	}
	
	////3번 패턴 장판은 최대 16개
	for (size_t i = 0; i < 16; i++)
	{
		std::string Floorname = "BP003Floor" +std::to_string(i);
		GameObject* Prefab2 = PrefabUtilitys->InstantiatePrefab(BP003Prefab, Floorname);
		Prefab2->SetEnabled(false);
		BP003Objs.push_back(Prefab2);
	}

	int playersCount = 0;
	GameManager* GameManagers = GameObject::Find("GameManager")->GetComponent<GameManager>();
	if (GameManagers) {
		auto players = GameManagers->GetPlayers();
		playersCount = players.size();
		if (players.size() > 0) {
			Player1 = players[0]->GetOwner();
			if (players.size() > 1) {
				Player2 = players[1]->GetOwner();
			}
		}
	}

	SetupPatternItemData(playersCount);

	m_maxHP = m_MaxHp;
	m_currentHP = m_maxHP;

	

	//blackboard initialize
	BB->SetValueAsString("State", m_state); //현제 상태
	BB->SetValueAsString("Identity", m_identity); //고유 아이덴티티

	BB->SetValueAsInt("MaxHP", m_maxHP); //최대 체력
	BB->SetValueAsInt("CurrHP", m_currentHP); //현재 체력

	BB->SetValueAsFloat("IdleTime", m_idleTime); //대기 시간

	if (Player1) {
		BB->SetValueAsGameObject("1P", Player1->ToString());
	}
	if (Player2) {
		BB->SetValueAsGameObject("2P", Player2->ToString());
	}

	Prefab* deadPrefab = PrefabUtilitys->LoadPrefab("EnemyDeathEffect");
	if (deadPrefab)
	{
		deadObj = PrefabUtilitys->InstantiatePrefab(deadPrefab, "DeadEffect");
		deadObj->SetEnabled(false);
	}
	HitImpulseStart();

}

void TBoss1::Update(float tick)
{
	std::cout << "[TBoss1::Update] 프레임 시작. 현재 페이즈: " << GetPatternPhaseToString(m_patternPhase) << std::endl;
	HitImpulseUpdate(tick);
	//test code  ==> todo : 필요에 따라 최초 타겟과 타겟을 변경하는 내용 ==> 변경기준 카운트? 거리? 랜덤?
	//1. 보스 기준으로 가장 가까운 플레이어
	//2. 이전 타겟과의 선택 횟수 차이가 1 이하인지 확인 (eB_TargetNumLimit)
	
	if (!m_target) {
		SelectTarget();
	}

	hazardTimer += tick;
	//패턴이 없을때만 회전 패턴중 필요시 내부에서 회전 시키기
	if (m_activePattern == EPatternType::None) {
		RotateToTarget();
	}

	// 1. Handle generic, time-based phase transitions
	UpdatePatternPhase(tick);

	// 2. Handle pattern-specific logic for the current phase
	UpdatePatternAction(tick);


	//UpdatePattern(tick);
}

std::string TBoss1::GetPatternTypeToString(EPatternType type)
{
	switch (type)
	{	
	case TBoss1::EPatternType::None:
		return "none";
	case TBoss1::EPatternType::BP0011:
		return "BP0011";
	case TBoss1::EPatternType::BP0012:
		return "BP0012";
	case TBoss1::EPatternType::BP0013:
		return "BP0013";
	case TBoss1::EPatternType::BP0014:
		return "BP0014";
	case TBoss1::EPatternType::BP0021:
		return "BP0021";
	case TBoss1::EPatternType::BP0022:
		return "BP0022";
	case TBoss1::EPatternType::BP0031:
		return "BP0031";
	case TBoss1::EPatternType::BP0032:
		return "BP0032";
	case TBoss1::EPatternType::BP0033:
		return "BP0033";
	case TBoss1::EPatternType::BP0034:
		return "BP0034";
	default:
		return "error";
	}
}

std::string TBoss1::GetPatternPhaseToString(EPatternPhase phase)
{
	switch (phase) {
	case TBoss1::EPatternPhase::Inactive:
		return "Inactive";
	case TBoss1::EPatternPhase::Warning:
		return "Warning";
	case TBoss1::EPatternPhase::Spawning:
		return "Spawning";
	case TBoss1::EPatternPhase::Move:
		return "Move";
	case TBoss1::EPatternPhase::Action:
		return "Action";
	case TBoss1::EPatternPhase::WaitForObjects:
		return "WaitForObjects";
	case TBoss1::EPatternPhase::ComboInterval:
		return "Action";
	case TBoss1::EPatternPhase::Waiting:
		return "Waiting";
	default:
		return "error";
	}
}

void TBoss1::RotateToTarget()
{
	SimpleMath::Quaternion rot = m_pOwner->m_transform.GetWorldQuaternion();
	if (m_target)
	{
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Transform* targetTransform = m_target->GetComponent<Transform>();
		if (targetTransform) {
			Mathf::Vector3 targetpos = targetTransform->GetWorldPosition();
			Mathf::Vector3 dir = targetpos - pos;
			dir.y = 0.f;
			dir.Normalize();
			SimpleMath::Quaternion lookRot = SimpleMath::Quaternion::CreateFromRotationMatrix(SimpleMath::Matrix::CreateLookAt(Mathf::Vector3::Zero, -dir, Mathf::Vector3::Up));
			lookRot.Inverse(lookRot);
			//rot = SimpleMath::Quaternion::Slerp(rot, lookRot, 0.2f);
			//m_pOwner->m_transform.SetRotation(rot);
			m_pOwner->m_transform.SetRotation(lookRot);
		}
	}
}

void TBoss1::ShootIndexProjectile(int index, Mathf::Vector3 pos, Mathf::Vector3 dir)
{
	if (index < 0 || index >= BP001Objs.size()) return; // 인덱스 범위 검사
	GameObject* obj = BP001Objs[index];
	BP001* script = obj->GetComponent<BP001>();
	obj->SetEnabled(true);
	script->Initialize(this, pos, dir, BP001Damage, BP001RadiusSize, BP001Delay, BP001Speed);
	script->isAttackStart = true;
}

void TBoss1::ShootProjectile()
{
	if (pattenIndex < 0 || pattenIndex >= BP001Objs.size()) return; // 인덱스 범위 검사
	GameObject* obj = BP001Objs[pattenIndex];
	BP001* script = obj->GetComponent<BP001>();
	obj->SetEnabled(true);
	script->Initialize(this, projectilePos, projectileDir, BP001Damage, BP001RadiusSize, BP001Delay, BP001Speed);
	script->isAttackStart = true;
}



void TBoss1::SweepAttackDir(Mathf::Vector3 pos, Mathf::Vector3 dir)
{
	//데미지 판정 ==> 박스 스윕으로 처리 할까? 
	//범위 : BP002Dist, BP002Widw --> 모션 보이는 것보다 크거나 작을수 있음
	Mathf::Vector3 boxHalfExtents(BP002Widw * 0.5f, 2.0f, BP002Dist * 0.5f); // 높이 2.0f는 임의로 설정
	Mathf::Vector3 boxCenter = pos + dir * (BP002Dist * 0.5f); // 박스 중심 위치
	Mathf::Quaternion boxOrientation = Mathf::Quaternion::CreateFromRotationMatrix(Mathf::Matrix::CreateLookAt(Mathf::Vector3::Zero, dir, Mathf::Vector3::Up));
	std::vector<HitResult> hitObjects;
	SweepInput input;
	input.direction = dir;
	input.distance = BP002Dist;
	input.startPosition = pos;
	input.startRotation = boxOrientation;
	input.layerMask = ~0; // 모든 레이어 검사
	int hitCount = PhysicsManagers->BoxSweep(input, boxHalfExtents, hitObjects);
	isAttacked = true;
	if (hitCount > 0)
	{
		for (const auto& hit : hitObjects)
		{
			GameObject* hitObject = hit.gameObject;
			if (hitObject && hitObject->m_tag == "Player")
			{
				// 플레이어에게 데미지 적용
				Entity* entity = hitObject->GetComponentDynamicCast<Entity>();
				if (entity)
				{
					HitInfo hitInfo;
					hitInfo.hitPos = hit.point;
					hitInfo.hitNormal = hit.normal;
					hitInfo.attakerPos = pos;
					hitInfo.KnockbackForce = { KnockbackDistacneX ,KnockbackDistacneY };
					//hitInfo.KnockbackForce
					//hitInfo.bulletType
					//hitInfo.itemType
					entity->SendDamage(this, BP002Damage, hitInfo);
				}
			}
		}
	}
}

void TBoss1::SweepAttack()
{
	Mathf::Vector3 pos = m_pOwner->m_transform.GetWorldPosition();
	Mathf::Vector3 dir = m_pOwner->m_transform.GetForward();

	Mathf::Vector3 boxHalfExtents(BP002Widw * 0.5f, 2.0f, BP002Dist * 0.5f); // 높이 2.0f는 임의로 설정
	Mathf::Vector3 boxCenter = pos + dir * (BP002Dist * 0.5f); // 박스 중심 위치
	Mathf::Quaternion boxOrientation = Mathf::Quaternion::CreateFromRotationMatrix(Mathf::Matrix::CreateLookAt(Mathf::Vector3::Zero, dir, Mathf::Vector3::Up));
	std::vector<HitResult> hitObjects;
	SweepInput input;
	input.direction = dir;
	input.distance = BP002Dist;
	input.startPosition = pos;
	input.startRotation = boxOrientation;
	input.layerMask = 1 << 5; // 플레이어 레이어만 검사
	int hitCount = PhysicsManagers->BoxSweep(input, boxHalfExtents, hitObjects);
	isAttacked = true;
	if (hitCount > 0)
	{
		for (const auto& hit : hitObjects)
		{
			GameObject* hitObject = hit.gameObject;
			if (hitObject == this->GetOwner()) {
				continue;
			}
			// 플레이어에게 데미지 적용
			Entity* entity = hitObject->GetComponentDynamicCast<Entity>();
			if (entity)
			{
				HitInfo hitInfo;
				hitInfo.hitPos = hit.point;
				hitInfo.hitNormal = hit.normal;
				hitInfo.attakerPos = pos;
				//hitInfo.KnockbackForce
				//hitInfo.bulletType
				//hitInfo.itemType
				entity->SendDamage(this, BP002Damage, hitInfo);
			}
			
		}
	}
}


void TBoss1::MoveToChunsik(float tick)
{
	if (!isMoved)
	{
		if (!isBurrow) {
			Burrow();
		}
		else {
			burrowTimer += tick;
			if (burrowTimer >= 2.f) {
				ProtrudeChunsik();
				burrowTimer = 0.f;
			}
		}
	}
}

void TBoss1::BurrowMove(float tick)
{
	if(!isMoved)
	{
		if (!isBurrow) {
			
			Burrow();
		}
		else {
			burrowTimer += tick;
			if (burrowTimer >= 2.f) {
				Protrude();
				burrowTimer = 0.f;
			}
		}
	}
}

void TBoss1::OnWarningFinished()
{
	// 2. 애니메이터의 다음 상태 전환을 허가합니다.
	std::cout << "[TBoss1::OnWarningFinished] 진입. 현재 페이즈: " << GetPatternPhaseToString(m_patternPhase) << std::endl;
	if (m_animator) m_animator->SetParameter("CanTransitionToAttack", true);
	if (m_activePattern == EPatternType::BP0022) {
		m_patternPhase = EPatternPhase::Move;
	}
	else {
		m_patternPhase = EPatternPhase::Spawning;
	}
	std::cout << "[TBoss1::OnWarningFinished] 종료. 새로운 페이즈: " << GetPatternPhaseToString(m_patternPhase)<< std::endl;
}

void TBoss1::OnAttackActionFinished()
{
	if (m_animator) m_animator->SetParameter("CanTransitionToAttack", false);
	switch (m_activePattern)
	{
	case EPatternType::BP0013: 
	{
		pattenIndex++;
		if (pattenIndex < 3)
		{
			m_patternPhase = EPatternPhase::ComboInterval;
			BPTimer = 0.f;
		}
		else
		{
			m_patternPhase = EPatternPhase::Waiting;
			BPTimer = 0.f;
		}
		break;
	}
	case EPatternType::BP0022:
	{
		pattenIndex++;
		if (pattenIndex < 5) // 5회 공격 콤보
		{
			m_patternPhase = EPatternPhase::Move; // 다음은 이동 단계
			BPTimer = 0.f; // 이동 시간 측정용 타이머 (필요시)
			isMoved = false;
		}
		else
		{
			m_patternPhase = EPatternPhase::Waiting;
			BPTimer = 0.f;
		}
		break;
	}
	default:
	{
		m_patternPhase = EPatternPhase::Waiting;
		BPTimer = 0.f;
		break;
	}
	}
}

void TBoss1::OnMoveFinished()
{
	m_patternPhase = EPatternPhase::Spawning;
}

//void TBoss1::StartPattern(EPatternType type)
//{
//	if (m_activePattern != EPatternType::None) return; // 다른 패턴이 실행 중이면 무시
//
//	m_activePattern = type;
//
//	// 시작할 패턴 타입에 따라 적절한 초기화 함수를 호출합니다.
//	switch (type)
//	{
//	case EPatternType::BP0034:
//		BP0034();
//		break;
//
//		// 다른 패턴들의 case를 여기에 추가...
//	}
//}

void TBoss1::UpdatePatternPhase(float tick)
{
	std::cout << "m_patternPhase : " + GetPatternPhaseToString(m_patternPhase) << std::endl;
	switch (m_patternPhase)
	{
	case EPatternPhase::ComboInterval:
	{
		BPTimer += tick;
		if (BPTimer >= ComboIntervalTime)
		{
			StartNextComboAttack();
		}
		break;
	}
	case EPatternPhase::Waiting:
	{
		BPTimer += tick;
		if (BPTimer >= AttackDelayTime)
		{
			EndPattern();
		}
		break;
	}
	// Warning -> Spawning transition is now handled by OnWarningFinished()
	default:
		break;
	}
}

void TBoss1::UpdatePatternAction(float tick)
{
	std::cout << "m_activePattern : " + GetPatternTypeToString(m_activePattern) << std::endl;
	switch (m_activePattern)
	{
	case EPatternType::BP0011:
		Update_BP0011(tick);
		break;
	case EPatternType::BP0013: 
		Update_BP0013(tick);
		break;

	case EPatternType::BP0021:
		Update_BP0021(tick);
		break;
	case EPatternType::BP0022:
		Update_BP0022(tick);
		break;

	case EPatternType::BP0031:
		Update_BP0031(tick);
		break;
	case EPatternType::BP0032:
		Update_BP0032(tick);
		break;
	case EPatternType::BP0033:
		Update_BP0033(tick);
		break;
	case EPatternType::BP0034:
		Update_BP0034(tick);
		break;

		// ... other patterns ...
	default:
		break;
	}
}


void TBoss1::Update_BP0011(float tick)
{
	switch (m_patternPhase)
	{
		case EPatternPhase::Warning:
			RotateToTarget(); //전조 모션중 타겟을 바라보게
			
			if (m_anicontroller && m_anicontroller->m_curState) {
				std::string currentStateName = m_anicontroller->m_curState->m_name;
				if (currentStateName == "Idle") {  //이미 모션을 실행 시켰으나 전조모션이 안나간 경우
					m_animator->SetParameter("StartRangedAttack", true);
				}
			}
			
			
			break;
		case EPatternPhase::Spawning:
		{
			RotateToTarget(); // Lock final direction
			//todo :: 에니메이션 재생
			//m_animator->SetParameter("RangeAtteckTrigger", true);
			
			// 방향과 위치만 정하자 
			Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

			Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();

			Mathf::Vector3 dir = targetPos - ownerPos;
			dir.y = 0;
			dir.Normalize();
			Mathf::Vector3 pos = ownerPos + (dir * 2.0f) + (Mathf::Vector3(0, 1, 0) * 0.5);
		
			projectilePos = pos;
			projectileDir = dir;
			//ShootProjectile(0, pos, dir);//--> 에니메이션에서 발사 프레임에 맞춰서 호출하는 방식으로 변경 가능	
			m_patternPhase = EPatternPhase::Action; // Hand off to animation
			break;
		}
	
	}
}

void TBoss1::Update_BP0013(float tick)
{
	switch (m_patternPhase)
	{
		case EPatternPhase::Warning:
			RotateToTarget(); //전조 모션중 타겟을 바라보게
			if (m_anicontroller && m_anicontroller->m_curState) {
				std::string currentStateName = m_anicontroller->m_curState->m_name;
				if (currentStateName == "Idle") {  //이미 모션을 실행 시켰으나 전조모션이 안나간 경우
					m_animator->SetParameter("StartRangedAttack", true);
				}
			}
			break;
		case EPatternPhase::Spawning:
		{
			RotateToTarget(); // Lock final direction
			//todo :: 에니메이션 재생
			//m_animator->SetParameter("RangeAtteckTrigger", true);
			
			// 방향과 위치만 정하자 
			Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

			Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();

			Mathf::Vector3 dir = targetPos - ownerPos;
			dir.y = 0;
			dir.Normalize();
			Mathf::Vector3 pos = ownerPos + (dir * 2.0f) + (Mathf::Vector3(0, 1, 0) * 0.5);

			projectilePos = pos;
			projectileDir = dir;
			//ShootProjectile(0, pos, dir);//--> 에니메이션에서 발사 프레임에 맞춰서 호출하는 방식으로 변경 가능	
			m_patternPhase = EPatternPhase::Action; // Hand off to animation
			break;
		}
	}
}

void TBoss1::Update_BP0021(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Warning:
		// Fixed attack, do not track
		RotateToTarget(); // Track target during warning
		if (m_anicontroller && m_anicontroller->m_curState) {
			std::string currentStateName = m_anicontroller->m_curState->m_name;
			if (currentStateName == "Idle") {  //이미 모션을 실행 시켰으나 전조모션이 안나간 경우
				m_animator->SetParameter("StartMeleeAttack", true);
			}
		}
		break;
	case EPatternPhase::Spawning:
		RotateToTarget(); // Lock final direction
		m_patternPhase = EPatternPhase::Action;
		break;
	}
}

void TBoss1::Update_BP0022(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Warning:
		RotateToTarget(); // Track target during warning
		if (m_anicontroller && m_anicontroller->m_curState) {
			std::string currentStateName = m_anicontroller->m_curState->m_name;
			if (currentStateName == "Idle") {  //이미 모션을 실행 시켰으나 전조모션이 안나간 경우
				m_animator->SetParameter("WarningTrigger", true);
			}
		}
		break;
	case EPatternPhase::Move:
		// This phase is for movement. Assume BurrowMove is animation-driven.
		// When movement animation is complete, OnMoveFinished() will be called.
		// For now, we just wait for OnMoveFinished() to set the next phase.
		BurrowMove(tick);
		if (isMoved) {
			if (m_anicontroller && m_anicontroller->m_curState) {
				std::string currentStateName = m_anicontroller->m_curState->m_name;
				if (currentStateName == "Idle") {
					m_patternPhase = EPatternPhase::Spawning;
					if (m_animator) m_animator->SetParameter("MeleeCombo", true);
				}
			}
		}
		break;
	case EPatternPhase::Spawning:
		RotateToTarget(); // Lock final direction
		m_patternPhase = EPatternPhase::Action;
		break;
	/*case EPatternPhase::Action:
		if (m_anicontroller && m_anicontroller->m_curState) {
			std::string currentStateName = m_anicontroller->m_curState->m_name;
			if (currentStateName != "MeleeComboAtteck") {
				if (m_animator) m_animator->SetParameter("MeleeCombo", true);
			}
		}
		break;*/
	}
}

void TBoss1::Update_BP0031(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Warning:
		// BP0034 might have a warning animation, e.g., showing where objects will spawn.
		// RotateToTarget(); // If it tracks during warning
		m_patternPhase = EPatternPhase::Spawning; // 장판은 전조 없음 만약에 걸리면 바로 넘기자 혹은 여기서 이동 처리 생각해 볼수도 
		break;
	case EPatternPhase::Spawning:
	{
		//비었으면 문제 인대 그럼 돌려보내
		if (BP003Objs.empty()) { return; }

		//todo: 일단 타겟으로 잡은 플레이어 위치만 생각 둘다 만들어 지는건 각 플레이어 방향 받아서 생각 좀 해보자
		//Mathf::Vector3 pos = m_target->GetComponent<Transform>()->GetWorldPosition();
		// --> 플레이어 모두
		//m_animator->SetParameter("ShiftTrigger", true); --> 시작 한번
	
		int index = 0;
		if (Player1) {
			Mathf::Vector3 pos = BB->GetValueAsGameObject("1P")->GetComponent<Transform>()->GetWorldPosition();
			//한개 
			BP003* script = BP003Objs[index]->GetComponent<BP003>();
			BP003Objs[index]->SetEnabled(true);
			//BP003Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
			bool drop = false;
			if (index < m_patternItemFlags.size()) {
				drop = m_patternItemFlags[index];
			}
			
			script->Initialize(this, pos, BP003Damage, BP003RadiusSize, BP003Delay, drop);
			script->isAttackStart = true;
			index++;
		}

		if (Player2)
		{
			Mathf::Vector3 pos = BB->GetValueAsGameObject("2P")->GetComponent<Transform>()->GetWorldPosition();
			BP003* script = BP003Objs[index]->GetComponent<BP003>();
			BP003Objs[index]->SetEnabled(true);
			//BP003Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
			bool drop = false;
			if (index < m_patternItemFlags.size()) {
				drop = m_patternItemFlags[index];
			}

			script->Initialize(this, pos, BP003Damage, BP003RadiusSize, BP003Delay, drop);
			script->isAttackStart = true;
			index++;
		}


		pattenIndex = index;
		m_patternPhase = EPatternPhase::WaitForObjects;
		break; // while 루프 탈출
	}
	case EPatternPhase::WaitForObjects:
	{
		bool allFinished = true;
		// 활성화했던 모든 오브젝트를 순회하며 비활성화되었는지 확인합니다.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // 아직 하나라도 활성 상태이면 대기를 계속합니다.
				break;
			}
		}

		if (allFinished)
		{
			// 모든 오브젝트가 비활성화되었으므로 패턴을 완전히 종료합니다.
			EndPattern();
		}
		break;
	}
	}

}

void TBoss1::Update_BP0032(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Warning:
		// BP0034 might have a warning animation, e.g., showing where objects will spawn.
		// RotateToTarget(); // If it tracks during warning
		m_patternPhase = EPatternPhase::Spawning; // 장판은 전조 없음 만약에 걸리면 바로 넘기자 혹은 여기서 이동 처리 생각해 볼수도 
		break;
	case EPatternPhase::Spawning:
	{
		//3개보다 적개 등록 됬으면 일단 에러인대 돌려보내
		if (BP003Objs.size() < 3) { return; }

		//일단 내위치랑 전방 방향
		Transform* tr = GetOwner()->GetComponent<Transform>();
		Mathf::Vector3 pos = tr->GetWorldPosition();
		Mathf::Vector3 forward = tr->GetForward();

		//회전축 y축 설정
		Mathf::Vector3 up_axis(0.0f, 1.0f, 0.0f);

		//전방에서 120도 240 해서 삼감 방면 방향
		Mathf::Quaternion rot120 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(120.0f));
		Mathf::Quaternion rot240 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(240.0f));

		//3방향 
		Mathf::Vector3 dir1 = forward; //전방
		Mathf::Vector3 dir2 = Mathf::Vector3::Transform(forward, rot120); //120
		Mathf::Vector3 dir3 = Mathf::Vector3::Transform(forward, rot240); //240

		//하나씩 컨트롤 하기 귀찮다 벡터로 포문돌려
		std::vector < Mathf::Vector3 > directions;
		directions.push_back(dir1);
		directions.push_back(dir2);
		directions.push_back(dir3);

		int index = 0;
		//m_animator->SetParameter("ShiftTrigger", true); --> 시작 한번
		for (auto& dir : directions) {
			

			Mathf::Vector3 objpos = pos + dir * 6.0f; // 6만큼 떨어진 위치 이 수치는 프로퍼티로 뺄까 일단 내가 계속 임의로 수정해주는걸로
			GameObject* floor = BP003Objs[index];
			BP003* script = floor->GetComponent<BP003>();
			floor->SetEnabled(true);
			bool drop = false;
			if (index < m_patternItemFlags.size()) {
				drop = m_patternItemFlags[index];
			}
			//floor->GetComponent<Transform>()->SetWorldPosition(objpos); = 초기화에서
			script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay, drop);
			script->isAttackStart = true;

			index++;
		}

		pattenIndex = index;
		m_patternPhase = EPatternPhase::WaitForObjects;
		break; // while 루프 탈출
	}
	case EPatternPhase::WaitForObjects:
	{
		bool allFinished = true;
		// 활성화했던 모든 오브젝트를 순회하며 비활성화되었는지 확인합니다.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // 아직 하나라도 활성 상태이면 대기를 계속합니다.
				break;
			}
		}

		if (allFinished)
		{
			// 모든 오브젝트가 비활성화되었으므로 패턴을 완전히 종료합니다.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0033(float tick)
{
	switch (m_patternPhase)
	{
	case EPatternPhase::Warning:
		// BP0034 might have a warning animation, e.g., showing where objects will spawn.
		// RotateToTarget(); // If it tracks during warning
		m_patternPhase = EPatternPhase::Move; // 장판은 전조 없음 만약에 걸리면 바로 넘기자 혹은 여기서 이동 처리 생각해 볼수도 
		break;
	case EPatternPhase::Move:
		MoveToChunsik(tick);
		if (isMoved) {
			m_patternPhase = EPatternPhase::Spawning;
		}
		break;
	case EPatternPhase::Spawning:
	{
		//MoveToChunsik(tick);
		
		Transform* tr = GetOwner()->GetComponent<Transform>();
		//if (m_chunsik) {
		//	Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
		//	tr->SetWorldPosition(chunsik);
		//	//이때 회전이나 같은 것도 알맞은 각도로 돌려 놓느것
		//}
		//일단 보스위치랑 전방 방향
		Mathf::Vector3 pos = tr->GetWorldPosition();
		Mathf::Vector3 forward = tr->GetForward();

		//m_animator->SetParameter("ShiftTrigger", true); --> 시작 한번

		//회전축 y축 설정
		Mathf::Vector3 up_axis(0.0f, 1.0f, 0.0f);

		//전방에서 120도 240 해서 삼감 방면 방향
		Mathf::Quaternion rot120 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(120.0f));
		Mathf::Quaternion rot240 = Mathf::Quaternion::CreateFromAxisAngle(up_axis, Mathf::ToRadians(240.0f));

		//3방향 
		Mathf::Vector3 dir1 = forward; //전방
		Mathf::Vector3 dir2 = Mathf::Vector3::Transform(forward, rot120); //120
		Mathf::Vector3 dir3 = Mathf::Vector3::Transform(forward, rot240); //240

		//하나씩 컨트롤 하기 귀찮다 벡터로 포문돌려
		std::vector < Mathf::Vector3 > directions;
		directions.push_back(dir1);
		directions.push_back(dir2);
		directions.push_back(dir3);

		int index = 0;
			

		for (auto& dir : directions) {
			//가까운거
			Mathf::Vector3 objpos = pos + dir * 6.0f; // 6만큼 떨어진 위치 이 수치는 프로퍼티로 뺄까 일단 내가 계속 임의로 수정해주는걸로
			GameObject* floor = BP003Objs[index];
			BP003* script = floor->GetComponent<BP003>();
			floor->SetEnabled(true);
			//floor->GetComponent<Transform>()->SetWorldPosition(objpos);
			bool drop = false;
			if (index < m_patternItemFlags.size()) {
				drop = m_patternItemFlags[index];
			}
			script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay, drop, true, false);
			script->isAttackStart = true;

			index++;

			//먼거
			Mathf::Vector3 objpos2 = pos + dir * 12.0f; // 12만큼 떨어진 위치 이 수치는 프로퍼티로 뺄까 일단 내가 계속 임의로 수정해주는걸로
			GameObject* floor2 = BP003Objs[index];
			BP003* script2 = floor2->GetComponent<BP003>();
			floor2->SetEnabled(true);
			//floor2->GetComponent<Transform>()->SetWorldPosition(objpos2);
			bool drop2 = false;
			if (index < m_patternItemFlags.size()) {
				drop2 = m_patternItemFlags[index];
			}
			script2->Initialize(this, objpos2, BP003Damage, BP003RadiusSize, BP003Delay, drop2, true);
			script2->isAttackStart = true;

			index++;
		}
		pattenIndex = index;
		m_patternPhase = EPatternPhase::WaitForObjects;
		break; // while 루프 탈출
		
	}
	case EPatternPhase::WaitForObjects:
	{
		bool allFinished = true;
		// 활성화했던 모든 오브젝트를 순회하며 비활성화되었는지 확인합니다.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // 아직 하나라도 활성 상태이면 대기를 계속합니다.
				break;
			}
		}

		if (allFinished)
		{
			// 모든 오브젝트가 비활성화되었으므로 패턴을 완전히 종료합니다.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Update_BP0034(float tick)
{
	// 현재 단계(Phase)에 따라 다른 로직을 수행합니다.
	// 순차 1개씩 -> 한줄 4개 씩 
	switch (m_patternPhase)
	{
	case EPatternPhase::Warning:
		// BP0034 might have a warning animation, e.g., showing where objects will spawn.
		// RotateToTarget(); // If it tracks during warning
		m_patternPhase = EPatternPhase::Move; // 장판은 전조 없음 만약에 걸리면 바로 넘기자 혹은 여기서 이동 처리 생각해 볼수도 
		break;
	case EPatternPhase::Move:
		MoveToChunsik(tick);
		if (isMoved) {
			m_patternPhase = EPatternPhase::Spawning;
		}
		break;
	case EPatternPhase::Spawning:
	{
		BPTimer += tick;
		while (BPTimer >= BP0034delay)
		{
			if (pattenIndex < BP0034Points.size())
			{
				// 오브젝트 하나를 활성화합니다. --> row 단위 4개씩
				//m_animator->SetParameter("ShiftTrigger", true); --> 시작 한번
				int i = 0;
				for (; i < 4;)
				{
					Mathf::Vector3 objpos = BP0034Points[pattenIndex + i].second;
					GameObject* floor = BP003Objs[pattenIndex + i];
					BP003* script = floor->GetComponent<BP003>();
					floor->SetEnabled(true);
					bool drop = false;
					if (pattenIndex + i < m_patternItemFlags.size()) {
						drop = m_patternItemFlags[pattenIndex + i];
					}
					script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay, drop,false);
					script->isAttackStart = true;
					i++;
					if (pattenIndex + i > BP0034Points.size()) {
						i--;
						break; // 범위를 벗어나지 않도록 검사
					}
				}
				pattenIndex += i;
				BPTimer -= BP0034delay;
			}
			else
			{
				// 모든 오브젝트를 생성했으면 '완료 대기' 단계로 전환합니다.
				m_patternPhase = EPatternPhase::WaitForObjects;
				break; // while 루프 탈출
			}
		}
		
		break;
	}
	case EPatternPhase::WaitForObjects:
	{
		bool allFinished = true;
		// 활성화했던 모든 오브젝트를 순회하며 비활성화되었는지 확인합니다.
		for (int i = 0; i < pattenIndex; ++i)
		{
			if (BP003Objs[i]->IsEnabled())
			{
				allFinished = false; // 아직 하나라도 활성 상태이면 대기를 계속합니다.
				break;
			}
		}

		if (allFinished)
		{
			// 모든 오브젝트가 비활성화되었으므로 패턴을 완전히 종료합니다.
			EndPattern();
		}
		break;
	}
	}
}

void TBoss1::Calculate_BP0034()
{
	//춘식이에서 격자점 계산
	Transform* tr = m_chunsik->GetComponent<Transform>();

	//todo: 일단 타겟으로 잡은 플레이어 위치만 생각 둘다 만들어 지는건 각 플레이어 방향 받아서 생각 좀 해보자
	Mathf::Vector3 targetPos;

	if (m_target) {
		targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
	}
	else {//문제가 생겨서 타겟을 잃어 버렸는대 패턴 발생시 
		//일단 플레이어1의 포지션을 가져오자 -> todo

	}


	Mathf::Vector3 direction; //우선 방향
	Mathf::Vector3 pos = tr->GetWorldPosition();//춘식이 위치 
	direction = targetPos - pos;
	direction.Normalize();

	// ★ 1. 가장 가까운 8방위 축을 찾아, 대각선인지 판별
	const std::vector<Mathf::Vector3> eightDirections = {
		Mathf::Vector3(0.f, 0.f, 1.f),   // 0: N
		Mathf::Vector3(1.f, 0.f, 1.f),   // 1: NE
		Mathf::Vector3(1.f, 0.f, 0.f),   // 2: E
		Mathf::Vector3(1.f, 0.f, -1.f),  // 3: SE
		Mathf::Vector3(0.f, 0.f, -1.f),  // 4: S
		Mathf::Vector3(-1.f, 0.f, -1.f), // 5: SW
		Mathf::Vector3(-1.f, 0.f, 0.f),  // 6: W
		Mathf::Vector3(-1.f, 0.f, 1.f)   // 7: NW
	};

	int closestDirIndex = -1;
	float maxDot = -2.0f;
	for (int i = 0; i < 8; ++i) {
		Mathf::Vector3 normalizedDir = eightDirections[i];
		normalizedDir.Normalize();
		float dot = direction.Dot(normalizedDir);
		if (dot > maxDot) {
			maxDot = dot;
			closestDirIndex = i;
		}
	}

	// 대각선 방향(인덱스가 홀수)일 경우에만 45도 회전
	bool shouldRotate = (closestDirIndex % 2 != 0);


	// ★ 2. 결정된 회전값 생성 (대각선이 아니면 Identity, 즉 0도 회전)
	Mathf::Quaternion gridRotation = Mathf::Quaternion::Identity;
	if (shouldRotate) {
		const float ANGLE_45_RAD = 3.1415926535f / 4.f;
		gridRotation = Mathf::Quaternion::CreateFromAxisAngle(Mathf::Vector3(0, 1, 0), ANGLE_45_RAD);
	}
	Mathf::Quaternion inverseGridRotation = gridRotation;
	if (shouldRotate) {
		inverseGridRotation.Conjugate();
	}


	//// 3. 격자 기본 값 계산 및 정렬 규칙 결정 (Tilted 버전과 동일한 로직)
	//const float PI = 3.1415926535f;
	//float circleArea = PI * m_chunsikRadius * m_chunsikRadius;
	//float singleCellArea = circleArea / 16;
	//float gridSpacing = sqrt(singleCellArea);
	//int gridDimension = static_cast<int>(ceil(m_chunsikRadius * 2 / gridSpacing));

	Mathf::Vector3 localPlayerDir = Mathf::Vector3::Transform(direction, inverseGridRotation);
	bool isMajorAxisX = std::abs(localPlayerDir.x) > std::abs(localPlayerDir.z);
	float majorSortSign = (isMajorAxisX ? localPlayerDir.x : localPlayerDir.z) < 0.f ? 1.f : -1.f;
	float minorSortSign = (isMajorAxisX ? localPlayerDir.z : localPlayerDir.x) < 0.f ? 1.f : -1.f;

	//// 3. 우선순위와 위치를 저장할 벡터
	////std::vector<std::pair<int, Mathf::Vector3>> placements;
	//BP0034Points.clear();

	//// 4. 격자 순회 (Tilted 버전과 동일한 로직)
	//int iz = 0;
	//for (float z = -m_chunsikRadius; z <= m_chunsikRadius; z += gridSpacing, ++iz) {
	//	int ix = 0;
	//	for (float x = -m_chunsikRadius; x <= m_chunsikRadius; x += gridSpacing, ++ix) {
	//		Mathf::Vector3 localOffset(x, 0, z);
	//		if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

	//		Mathf::Vector3 rotatedOffset = Mathf::Vector3::Transform(localOffset, gridRotation);
	//		Mathf::Vector3 spawnPos = pos + rotatedOffset;

	//		int majorIndex, minorIndex;
	//		if (isMajorAxisX) { majorIndex = ix; minorIndex = iz; }
	//		else { majorIndex = iz; minorIndex = ix; }

	//		int priority = static_cast<int>((majorIndex * majorSortSign) * gridDimension + (minorIndex * minorSortSign));
	//		BP0034Points.push_back({ priority, spawnPos });
	//	}
	//}
	int pointsPerRow = 4;
	float gridSpacing = (m_chunsikRadius * 2) / (pointsPerRow - 1);
	BP0034Points.clear();

	// 4. 격자 순회 (훨씬 간단해짐)
	for (int iz = 0; iz < pointsPerRow; ++iz) {
		for (int ix = 0; ix < pointsPerRow; ++ix) {
			// -halfSize ~ +halfSize 범위로 좌표 계산
			float x = -m_chunsikRadius + ix * gridSpacing;
			float z = -m_chunsikRadius + iz * gridSpacing;

			Mathf::Vector3 localOffset(x, 0, z);

			// ★★★ 원 밖에 있는지 검사하는 if문이 필요 없어짐! ★★★
			// if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

			Mathf::Vector3 rotatedOffset = Mathf::Vector3::Transform(localOffset, gridRotation);
			Mathf::Vector3 spawnPos = pos + rotatedOffset;

			int majorIndex, minorIndex;
			if (isMajorAxisX) { majorIndex = ix; minorIndex = iz; }
			else { majorIndex = iz; minorIndex = ix; }

			int priority = static_cast<int>((majorIndex * majorSortSign) * pointsPerRow + (minorIndex *
				minorSortSign));
			BP0034Points.push_back({ priority, spawnPos });
		}
	}


	// 5. 우선순위(pair.first)에 따라 기본 오름차순 정렬
	std::sort(BP0034Points.begin(), BP0034Points.end(), [](auto a, auto b) {
		return a.first < b.first;
		});

}

void TBoss1::EndPattern()
{
	m_lastCompletedPattern = m_activePattern;
	m_activePattern = EPatternType::None;
	m_patternPhase = EPatternPhase::Inactive;
	if (m_animator) {
		m_animator->SetParameter("StartRangedAttack", false);
		m_animator->SetParameter("StartMeleeAttack", false);
		m_animator->SetParameter("rangedCombo", false);
		m_animator->SetParameter("meleeCombo", false);
	}
}

void TBoss1::SelectTarget()
{
	//todo : 타겟 선택 (1P/2P) 함수로 빼는 이유는 패턴에 따라 타겟을 언제 선택할지 다를수 있기 때문
	//싱글플레이어 모드 가능성 있으므로 1P/2P가 모두 있는지 확인
	
	if (!Player1 && !Player2) {
		//에러 플레이어를 찾을 수 없음
		return;
	}
	if (Player1 && !Player2) {
		//1P만 존재
		m_target = Player1; //무조건 1P
		return;
	}
	//2P만 존재 하는 경우가 있을까? --> 이건 나중에 생각하자
	//아니라면 둘이 선택되었던 횟수 비교
	if (p1Count != p2Count)
	{
		if (p1Count < p2Count) {
			m_target = Player1;
			p1Count++;
		}
		else {
			m_target = Player2;
			p2Count++;
		}
		return;
	}
	//횟수가 같다면?
	//코인 토스 처럼 2/1 확률로 1P/2P 선택
	int randValue = rand() % 2; // 0, 1 중 하나를 랜덤으로 선택
	if (randValue == 0) {
		m_target = Player1;
		p1Count++;
	}
	else {
		m_target = Player2;
		p2Count++;
	}

	if (p1Count == p2Count)
	{
		//횟수가 같아지면 초기화
		p1Count = 0;
		p2Count = 0;
	}
	
}

void TBoss1::StartNextComboAttack()
{
	m_patternPhase = EPatternPhase::Spawning;
	BPTimer = 0.f;

	switch (m_activePattern)
	{
	case EPatternType::BP0013: // Ranged Combo
		if (m_animator) m_animator->SetParameter("RangedCombo", true);
		break;
	case EPatternType::BP0022: // Melee Combo example
		if(m_animator) m_animator->SetParameter("MeleeCombo", true);
		break;
	}
}

void TBoss1::SetupPatternItemData(int players)
{
	m_patternItemData[EPatternType::BP0031] = { players,BP0031_MIN_ITEM ,BP0031_MAX_ITEM };
	m_patternItemData[EPatternType::BP0032] = { 3,BP0032_MIN_ITEM ,BP0032_MAX_ITEM };
	m_patternItemData[EPatternType::BP0033] = { 6,BP0033_MIN_ITEM ,BP0033_MAX_ITEM };
	m_patternItemData[EPatternType::BP0034] = { 16,BP0034_MIN_ITEM ,BP0034_MAX_ITEM };
}

void TBoss1::PrepareItemDropsForPattern(EPatternType patternType)
{
	const auto& info = m_patternItemData[patternType];
	int totalCount = info.totalPatternCount;
	Random<int> randItemCount(info.minItemCount, info.maxItemCount);
	int itemsToDrop = randItemCount();

	if (itemsToDrop > totalCount) itemsToDrop = totalCount;

	std::vector<int> indices(totalCount);
	std::iota(indices.begin(), indices.end(), 0);
	
	std::random_device rd;
	std::mt19937 g(rd());

	std::shuffle(indices.begin(), indices.end(),g);
	m_patternItemFlags.assign(totalCount, false);
	for (int i = 0; i < itemsToDrop; ++i) {
		m_patternItemFlags[indices[i]] = true;
	}
}

void TBoss1::Burrow()
{
	//todo : 땅속으로 들어감
	//땅속으로 들어가는 에니메이션 재생
	//이후 안보이게 처리
	//콜라이더 비활성화
	if (m_moveState == EBossMoveState::Idle) {
		m_animator->SetParameter("BurrowTrigger", true);
		m_rigid->SetColliderEnabled(false);
	}
}

void TBoss1::SetBurrow()
{
	isBurrow = true;
	m_moveState = EBossMoveState::Burrowed;
}

void TBoss1::Protrude()
{
	//todo : 땅속에서 나옴
	
	//튀어나오기 전에 이동 가능영역 안에 있는지 확인
	Mathf::Vector3 chunsikPos = m_chunsik->GetComponent<Transform>()->GetWorldPosition(); //춘식이(중심) 위치
	//타겟 확인  ==> 타겟이 없거나 잃어버렸다면? 그렇다면 랜덤 위치로
	Mathf::Vector3 targetPos;
	if(m_target)
	{
		targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
		targetPos.y = chunsikPos.y; //높이 맞춤
	}
	else {
		//춘식이를 기준으로 랜덤 위치
		float angle = static_cast<float>(rand()) / RAND_MAX * 2.f * 3.1415926535f; // 춘식이 기준 랜덤 방향
		float radius = static_cast<float>(rand()) / RAND_MAX * m_chunsikRadius; // 춘식이 기준 랜덤 거리
		targetPos = chunsikPos + Mathf::Vector3(cos(angle) * radius, 0, sin(angle) * radius); // 위를 기준으로 랜덤 위치 계산
	}

	float dist = (targetPos - chunsikPos).Length();
	if (dist < m_chunsikRadius)
	{
		//춘식이 반경 안에 있으면 타겟 위치로
	}
	else {
		//춘식이 반경 밖에 있으면 반경 안쪽 가장자리로
		Mathf::Vector3 dir = targetPos - chunsikPos;
		dir.Normalize();
		targetPos = chunsikPos + dir * (m_chunsikRadius - 1.f); // 약간 안쪽으로
	}

	//보스 위치 변경
	m_pOwner->GetComponent<Transform>()->SetWorldPosition(targetPos); //해당 위치에서 모션만 보여주고 바로 콜라이더 활성화
	//인디케이터 필요 ==> 해당 위치에 인디케이터 생성 후 잠시 대기 후 튀어나오기
	
	//모델 보이게 처리 하며

	//땅속에서 나오는 에니메이션 재생

	//올라오면서 플레이어 데미지 판정 + 플레이어 넉백
	if (m_moveState == EBossMoveState::Burrowed) {
		m_animator->SetParameter("ProtrudeTrigger", true);
	}
}

void TBoss1::ProtrudeEnd()
{	
	m_rigid->SetColliderEnabled(true);
	isBurrow = false;
	isMoved = true;
	m_moveState = EBossMoveState::Idle;
}

void TBoss1::ProtrudeChunsik()
{
	Mathf::Vector3 chunsikPos = m_chunsik->GetComponent<Transform>()->GetWorldPosition(); //춘식이(중심) 위치
	m_pOwner->GetComponent<Transform>()->SetWorldPosition(chunsikPos); //해당 위치에서 모션만 보여주고 바로 콜라이더 활성화

	//패턴시 보스를 중심이동 시키는 내용 --> 이때도 인디케이터와 데미지 판정이 필요 한가?

	//모델 보이게 처리 하며

	//땅속에서 나오는 에니메이션 재생
	if(m_moveState == EBossMoveState::Burrowed) {
		m_animator->SetParameter("ProtrudeTrigger", true);
	}
}


void TBoss1::BP0011()
{
	// 타겟을 향해 화염 투사체를 1개 발사하여 폭발
	if (m_activePattern != EPatternType::None) return;
	m_activePattern = EPatternType::BP0011;
	m_patternPhase = EPatternPhase::Warning;
	BPTimer = 0.f;
	pattenIndex = 0;
	projectileIndex = 0;
	if (m_animator) m_animator->SetParameter("StartRangedAttack", true);

}

void TBoss1::BP0012()
{
	//todo : 60도 부채꼴 범위로 화염 투사체 3개 발사
	EndPattern();
}

void TBoss1::BP0013()
{
	// 타겟을 향해 화염 투사체를 3개 연속 발사하여 폭발
	if (m_activePattern != EPatternType::None) return;
	m_activePattern = EPatternType::BP0013;
	pattenIndex = 0;
	projectileIndex = 0;
	
	m_patternPhase = EPatternPhase::Warning;
	BPTimer = 0.f;
	if (m_animator) m_animator->SetParameter("StartRangedAttack", true);
}

void TBoss1::BP0014()
{
	//todo : 60도 부채꼴 범위로 화염 투사체 5개를 2회 연속 발사
	EndPattern();
}

void TBoss1::BP0021()
{
	if (m_activePattern != EPatternType::None) return;
	m_activePattern = EPatternType::BP0021;
	m_patternPhase = EPatternPhase::Warning;
	pattenIndex = 0;
	BPTimer = 0.f;
	//RotateToTarget(); // Lock rotation at the beginning for this fixed attack
	if (m_animator) m_animator->SetParameter("StartMeleeAttack", true);
}

void TBoss1::BP0022()
{
	if (m_activePattern != EPatternType::None) return;
	m_activePattern = EPatternType::BP0022;
	m_patternPhase = EPatternPhase::Warning;
	pattenIndex = 0;
	BPTimer = 0.f;
	isMoved = false;
	//RotateToTarget(); // Lock rotation at the beginning for this fixed attack
	if (m_animator) m_animator->SetParameter("WarningTrigger", true);

}

void TBoss1::BP0031()
{
	//std::cout << "BP0031" << std::endl;
	//target 대상 으로 하나? 내발 밑에 하나? 임의 위치 하나? 
	//플레이어 수 만큼 1개씩 각 위치에
	
	if (m_activePattern != EPatternType::None) return;
	m_activePattern = EPatternType::BP0031;
	m_patternPhase = EPatternPhase::Spawning; //
	BPTimer = 0.f;
	pattenIndex = 0;
	PrepareItemDropsForPattern(m_activePattern);
	m_animator->SetParameter("ShiftTrigger", true);
}

void TBoss1::BP0032()
{
	//std::cout << "BP0032" << std::endl;
	//보스 주위로 3개
	
	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // 다른 패턴이 실행 중이면 무시 //요 검사는 BT에서 하는게 좋지 않나?
	//
	m_activePattern = EPatternType::BP0032;
	/* start patten end*/

	// 1. 패턴의 단계를 '생성 중'으로 설정하고 상태 변수들을 초기화합니다.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	BPTimer = 0.f;
	PrepareItemDropsForPattern(m_activePattern);
	m_animator->SetParameter("ShiftTrigger", true);
}

void TBoss1::BP0033()
{
	//3방향 회전
	//std::cout << "BP0033" << std::endl;
	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // 다른 패턴이 실행 중이면 무시 //요 검사는 BT에서 하는게 좋지 않나?
	//
	m_activePattern = EPatternType::BP0033;
	/* start patten end*/

	// 1. 패턴의 단계를 '생성 중'으로 설정하고 상태 변수들을 초기화합니다.
	m_patternPhase = EPatternPhase::Move;
	pattenIndex = 0;
	isMoved = false;
	BPTimer = 0.f;
	PrepareItemDropsForPattern(m_activePattern);
	m_animator->SetParameter("ShiftTrigger", true);
}

void TBoss1::BP0034()
{
	//std::cout << "BP0034" << std::endl;
	//적 방향을 우선으로 대각선 방향으로 맵 전체에 격자 모양으로 순차적으로 생성 폭파
	// 변경 라인 우선순위로 한줄씩 폭파 / 추가적으로 생성딜레이 프로퍼티 뺄것

	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // 다른 패턴이 실행 중이면 무시 //요 검사는 BT에서 하는게 좋지 않나?
	//
	m_activePattern = EPatternType::BP0034;
	/* start patten end*/

	// 1. 패턴의 단계를 '생성 중'으로 설정하고 상태 변수들을 초기화합니다.
	m_patternPhase = EPatternPhase::Move; //중앙으로 이동
	pattenIndex = 0;
	isMoved = false;
	BPTimer = 0.f;
	// 2. 이 패턴에 사용할 스폰 위치를 미리 계산합니다.
	PrepareItemDropsForPattern(m_activePattern);
	Calculate_BP0034();
	m_animator->SetParameter("ShiftTrigger", true);
}

void TBoss1::SendDamage(Entity* sender, int damage, HitInfo hitInfo)
{
	if (isDead) return;
	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		if (player)
		{
			Mathf::Vector3 curPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 senderPos = sender->GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 dir = curPos - senderPos;

			dir.Normalize();
			if (m_criticalMark)
			{
				if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
				{
					damage *= player->m_curWeapon->coopCrit;
					hitInfo.isCritical = true;
					//데미지2배및 hitEffect 크리티컬 이펙트로 출력 몬스터,리소스 동일
				}
			}
			PlayHitEffect(this->GetOwner(), hitInfo);

			m_currentHP -= damage;

			//blackBoard->SetValueAsInt("CurrHP", m_currentHP);


			if (m_currentHP <= 0)
			{
				isDead = true;  
				Dead();         //Dead애니메이션으로 보내기 + 보스 콜라이더 끄기 등등
				//DeadEvent(); //Die 애니메이션등이있으면 거기로 옮길것  // 이 함수에서 사망이펙트 + 삭제처리 + 게임매니저에 보스클리어 이벤트보내기등
				Time->SetTimeScale(0.1f, 5.0f); //보스 연출용 예시
				
				m_currentHP = 0;
			}
			else {
				HitImpulse();
			}
		}
	}
}

void TBoss1::Dead()
{
	m_animator->SetParameter("Dead", true);
	GetOwner()->SetLayer("Water");
}

void TBoss1::DeadEvent()
{
	EndDeadAnimation = true;
	deadObj->SetEnabled(true);
	auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
	Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
	deadPos.y += 0.7f;
	deadObj->GetComponent<Transform>()->SetPosition(deadPos);
	deadEffect->Initialize();
}

void TBoss1::BossClear()
{
	GameObject* GMObj = GameObject::Find("GameManager");
	if (GMObj)
	{
		GameManager* GM = GMObj->GetComponent<GameManager>();
		if (GM)
		{
			GM->BossClear(); //예시
		}
	}
}

