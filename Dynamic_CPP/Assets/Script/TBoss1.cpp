#include "TBoss1.h"
#include "pch.h"
#include "CharacterControllerComponent.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include <utility>
#include "BP003.h"
#include "BP001.h"

void TBoss1::Start()
{
	BT = m_pOwner->GetComponent<BehaviorTreeComponent>();
	BB = BT->GetBlackBoard();


	//춘식이는 블렉보드에서 포인트 잡아놓을꺼다. 나중에 바꾸던가 말던가
	bool hasChunsik = BB->HasKey("Chunsik");
	if (hasChunsik) {
		m_chunsik = BB->GetValueAsGameObject("Chunsik");
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
	

}

void TBoss1::Update(float tick)
{

	//test code  ==> todo : 필요에 따라 최초 타겟과 타겟을 변경하는 내용 ==> 변경기준 카운트? 거리? 랜덤?
	//1. 보스 기준으로 가장 가까운 플레이어
	//2. 이전 타겟과의 선택 횟수 차이가 1 이하인지 확인 (eB_TargetNumLimit)

	bool hasTarget = BB->HasKey("1P");
	if (hasTarget) {
		m_target = BB->GetValueAsGameObject("1P");
	}

	RotateToTarget();

	UpdatePattern(tick);
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



void TBoss1::UpdatePattern(float tick)
{
	// 활성화된 패턴에 따라 적절한 업데이트 함수로 분기합니다.
	switch (m_activePattern)
	{
	case EPatternType::BP0034:
		Update_BP0034(tick);
		break;

		// 다른 패턴들의 case를 여기에 추가...

	case EPatternType::None:
	default:
		// 실행 중인 패턴이 없으면 아무것도 하지 않습니다.
		break;
	}
}

void TBoss1::Update_BP0034(float tick)
{
	// 현재 단계(Phase)에 따라 다른 로직을 수행합니다.
	switch (m_patternPhase)
	{
	case EPatternPhase::Spawning:
	{
		bp0034Timer += tick;
		while (bp0034Timer >= delay)
		{
			if (pattenIndex < BP0034Points.size())
			{
				// 오브젝트 하나를 활성화합니다.
				Mathf::Vector3 objpos = BP0034Points[pattenIndex].second;
				GameObject* floor = BP003Objs[pattenIndex];
				BP003* script = floor->GetComponent<BP003>();
				floor->SetEnabled(true);
				script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay);
				script->isAttackStart = true;

				pattenIndex++;
				bp0034Timer -= delay;
			}
			else
			{
				// 모든 오브젝트를 생성했으면 '완료 대기' 단계로 전환합니다.
				m_patternPhase = EPatternPhase::Waiting;
				break; // while 루프 탈출
			}
		}
		break;
	}
	case EPatternPhase::Waiting:
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
	Transform* tr = GetOwner()->GetComponent<Transform>();
	//todo: 일단 타겟으로 잡은 플레이어 위치만 생각 둘다 만들어 지는건 각 플레이어 방향 받아서 생각 좀 해보자
	Mathf::Vector3 targetPos;

	if (m_target) {
		targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
	}
	else {//문제가 생겨서 타겟을 잃어 버렸는대 패턴 발생시 
		//일단 플레이어1의 포지션을 가져오자 -> todo

	}


	Mathf::Vector3 direction; //우선 방향
	Mathf::Vector3 pos = tr->GetWorldPosition();//보스 위치 
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


	// 3. 격자 기본 값 계산 및 정렬 규칙 결정 (Tilted 버전과 동일한 로직)
	const float PI = 3.1415926535f;
	float circleArea = PI * m_chunsikRadius * m_chunsikRadius;
	float singleCellArea = circleArea / 16;
	float gridSpacing = sqrt(singleCellArea);
	int gridDimension = static_cast<int>(ceil(m_chunsikRadius * 2 / gridSpacing));

	Mathf::Vector3 localPlayerDir = Mathf::Vector3::Transform(direction, inverseGridRotation);
	bool isMajorAxisX = std::abs(localPlayerDir.x) > std::abs(localPlayerDir.z);
	float majorSortSign = (isMajorAxisX ? localPlayerDir.x : localPlayerDir.z) < 0.f ? 1.f : -1.f;
	float minorSortSign = (isMajorAxisX ? localPlayerDir.z : localPlayerDir.x) < 0.f ? 1.f : -1.f;

	// 3. 우선순위와 위치를 저장할 벡터
	//std::vector<std::pair<int, Mathf::Vector3>> placements;
	BP0034Points.clear();

	// 4. 격자 순회 (Tilted 버전과 동일한 로직)
	int iz = 0;
	for (float z = -m_chunsikRadius; z <= m_chunsikRadius; z += gridSpacing, ++iz) {
		int ix = 0;
		for (float x = -m_chunsikRadius; x <= m_chunsikRadius; x += gridSpacing, ++ix) {
			Mathf::Vector3 localOffset(x, 0, z);
			if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

			Mathf::Vector3 rotatedOffset = Mathf::Vector3::Transform(localOffset, gridRotation);
			Mathf::Vector3 spawnPos = pos + rotatedOffset;

			int majorIndex, minorIndex;
			if (isMajorAxisX) { majorIndex = ix; minorIndex = iz; }
			else { majorIndex = iz; minorIndex = ix; }

			int priority = static_cast<int>((majorIndex * majorSortSign) * gridDimension + (minorIndex * minorSortSign));
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
	m_activePattern = EPatternType::None;
	m_patternPhase = EPatternPhase::Inactive;
}

void TBoss1::BP0011()
{
	//todo : 타겟을 향해 화염 투사체를 1개 발사하여 폭발
	//좀 더 생각 해봐야 하는 내용 보스 회전에 관하여 보스는 계속 플레이어를 따라 돌거다
	//투사체에 대하여 어느 타이밍에 날려야 깔끔해 보이는가
	//일단 타겟을봐

	if (!m_target) {
		return;
	}

	//비었으면 문제 인대 그럼 돌려보내
	if (BP001Objs.empty()) { return; }

	Mathf::Vector3 ownerPos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	
	Mathf::Vector3 targetPos = m_target->GetComponent<Transform>()->GetWorldPosition();
	
	Mathf::Vector3 dir = targetPos - ownerPos;
	dir.y = 0;
	dir.Normalize();
	Mathf::Vector3 pos = ownerPos + (dir * 2.0f) + (Mathf::Vector3(0, 1, 0) * 0.5);
	

	BP001* script = BP001Objs[0]->GetComponent<BP001>();
	BP001Objs[0]->SetEnabled(true);
	script->Initialize(this, pos, dir, BP001Damage, BP001RadiusSize, BP001Delay, BP001Speed);
	script->isAttackStart = true;

}

void TBoss1::BP0012()
{
	//todo : 60도 부채꼴 범위로 화염 투사체 3개 발사
}

void TBoss1::BP0013()
{
	//todo : 타겟을 향해 화염 투사체를 3개 연속 발사하여 폭발
}

void TBoss1::BP0014()
{
	//todo : 60도 부채꼴 범위로 화염 투사체 5개를 2회 연속 발사
}

void TBoss1::BP0031()
{
	std::cout << "BP0031" << std::endl;
	//target 대상 으로 하나? 내발 밑에 하나? 임의 위치 하나? 
	//todo: --> 플레이어 수 만큼 1개씩 각 위치에
	if (!m_target) {
		return;
	}

	//비었으면 문제 인대 그럼 돌려보내
	if (BP003Objs.empty()) { return; }

	//todo: 일단 타겟으로 잡은 플레이어 위치만 생각 둘다 만들어 지는건 각 플레이어 방향 받아서 생각 좀 해보자
	Mathf::Vector3 pos = m_target->GetComponent<Transform>()->GetWorldPosition();
	
	//한개 
	BP003* script = BP003Objs[0]->GetComponent<BP003>();
	BP003Objs[0]->SetEnabled(true);
	//BP003Objs[0]->GetComponent<Transform>()->SetWorldPosition(pos);
	script->Initialize(this, pos, BP003Damage, BP003RadiusSize, BP003Delay);
	script->isAttackStart = true;
}

void TBoss1::BP0032()
{
	std::cout << "BP0032" << std::endl;
	//내 주위로 3개
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

	for (auto& dir : directions) {
		Mathf::Vector3 objpos = pos + dir * 6.0f; // 6만큼 떨어진 위치 이 수치는 프로퍼티로 뺄까 일단 내가 계속 임의로 수정해주는걸로
		GameObject* floor = BP003Objs[index];
		BP003* script = floor->GetComponent<BP003>();
		floor->SetEnabled(true);
		//floor->GetComponent<Transform>()->SetWorldPosition(objpos); = 초기화에서
		script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay);
		script->isAttackStart = true;

		index++;
	}

	////다 생성 했으니 변수 다 필요없고 꺼지시게
	//index = 0; //로컬 변수라 함수 부를때마다 초기화 되겠지만 일단 해줘
	//directions.clear(); //예도

}

void TBoss1::BP0033()
{
	std::cout << "BP0033" << std::endl;
	//중요 보스맵 중앙 좌표를 어떻게 구할까?
	//->센터에 chunsik이 세워 놓고 그거 가지고 찾자.
	//춘식이 기준으로 거리구해서 이동 가능도 구하고
	//전체 맵도 춘식이 기준으로 원형 내부에 격자 4*4격자를 가지고 전체 맵 페턴 
	//이제 추가 적인 문제는 각 BP003Obj가 춘식이 혹은 춘식이 위치는 알고 있어야 한다.
	//초기화 함수에 넘겨주냐? 아니면 내부 변수로 들고 있을꺼냐?
	
	//일단 보스를 춘식이 위치로 옮겨  --> 잠만 보스도 cct로 움직이냐?? 그럼 좀 사고인대.. 이동 부터 만들어야 하나? -->보스는 CCT 쓰지 말자
	//CharacterControllerComponent* cct = GetOwner()->GetComponent< CharacterControllerComponent>();
	
	Transform* tr = GetOwner()->GetComponent<Transform>();
	if (m_chunsik) {
		Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
		tr->SetWorldPosition(chunsik);
		//이때 회전이나 같은 것도 알맞은 각도로 돌려 놓느것
	}
	//춘식이 없으면 제자리

	//일단 보스위치랑 전방 방향
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

	for (auto& dir : directions) {
		//가까운거
		Mathf::Vector3 objpos = pos + dir * 6.0f; // 6만큼 떨어진 위치 이 수치는 프로퍼티로 뺄까 일단 내가 계속 임의로 수정해주는걸로
		GameObject* floor = BP003Objs[index];
		BP003* script = floor->GetComponent<BP003>();
		floor->SetEnabled(true);
		//floor->GetComponent<Transform>()->SetWorldPosition(objpos);
		script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay,true,false);
		script->isAttackStart = true;

		index++;

		//먼거
		Mathf::Vector3 objpos2 = pos + dir * 12.0f; // 12만큼 떨어진 위치 이 수치는 프로퍼티로 뺄까 일단 내가 계속 임의로 수정해주는걸로
		GameObject* floor2 = BP003Objs[index];
		BP003* script2 = floor2->GetComponent<BP003>();
		floor2->SetEnabled(true);
		//floor2->GetComponent<Transform>()->SetWorldPosition(objpos2);
		script2->Initialize(this, objpos2, BP003Damage, BP003RadiusSize, BP003Delay,true);
		script2->isAttackStart = true;

		index++;
	}

}

void TBoss1::BP0034()
{
	std::cout << "BP0034" << std::endl;
	//적 방향을 우선으로 대각선 방향으로 맵 전체에 격자 모양으로 순차적으로 생성 폭파
	//=> todo : 변경 라인 우선순위로 한줄씩 폭파 / 추가적으로 생성딜레이 프로퍼티 뺄것

	//일단 보스를 춘식이 위치로 옮겨 0033과 동일
	Transform* tr = GetOwner()->GetComponent<Transform>();
	if (m_chunsik) {
		Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
		tr->SetWorldPosition(chunsik);
		//이때 회전이나 같은 것도 알맞은 각도로 돌려 놓느것
	}
	//춘식이 없으면 제자리

	/* start patten*/
	if (m_activePattern != EPatternType::None) return; // 다른 패턴이 실행 중이면 무시
	//
	m_activePattern = EPatternType::BP0034;
	/* start patten end*/

	// 1. 패턴의 단계를 '생성 중'으로 설정하고 상태 변수들을 초기화합니다.
	m_patternPhase = EPatternPhase::Spawning;
	pattenIndex = 0;
	bp0034Timer = 0.f;

	// 2. 이 패턴에 사용할 스폰 위치를 미리 계산합니다.
	Calculate_BP0034();
}

