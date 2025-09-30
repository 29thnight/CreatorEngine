#include "TBoss1.h"
#include "pch.h"
#include "CharacterControllerComponent.h"
#include "BehaviorTreeComponent.h"
#include "PrefabUtility.h"
#include <utility>
#include "BP003.h"

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
	//Prefab* BP001Prefab = PrefabUtilitys->LoadPrefab("Boss1BP001Obj");
	Prefab* BP003Prefab = PrefabUtilitys->LoadPrefab("Boss1BP003Obj");

	////1번 패턴 투사체 최대 10개
	for (size_t i = 0; i < 10; i++) {
		//	std::string Projectilename = "BP001Projectile" +std::to_string(i);
		//	GameObject* Prefab1 = PrefabUtilitys->InstantiatePrefab(BP001Prefab, Projectilename);
		//	Prefab1->SetEnabled(false);
		//	BP001Objs.push_back(Prefab1);
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
	//test code
	bool hasTarget = BB->HasKey("1P");
	if (hasTarget) {
		m_target = BB->GetValueAsGameObject("1P");
	}

	RotateToTarget();

	if (usePatten) {
		for (auto pos : BP0034Points) {

		}
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

void TBoss1::PattenActionIdle()
{
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
	//일단은 타깃 방향 

	//일단 보스를 춘식이 위치로 옮겨 0033과 동일
	Transform* tr = GetOwner()->GetComponent<Transform>();
	if (m_chunsik) {
		Mathf::Vector3 chunsik = m_chunsik->GetComponent<Transform>()->GetWorldPosition();
		tr->SetWorldPosition(chunsik);
		//이때 회전이나 같은 것도 알맞은 각도로 돌려 놓느것
	}
	//춘식이 없으면 제자리

	
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

	// 1. 격자 관련 기본 값 계산
	const float PI = 3.1415926535f;
	float circleArea = PI * m_chunsikRadius * m_chunsikRadius;
	float singleCellArea = circleArea / 16;
	float gridSpacing = sqrt(singleCellArea);
	int gridDimension = static_cast<int>(ceil(m_chunsikRadius * 2 / gridSpacing));

	// 2. 정렬 규칙 결정
	Mathf::Vector3 relativePlayerPos = direction;
	bool isMajorAxisX = std::abs(relativePlayerPos.x) > std::abs(relativePlayerPos.z);
	float majorSortSign = (isMajorAxisX ? relativePlayerPos.x : relativePlayerPos.z) < 0.f ? 1.f : -1.f;
	float minorSortSign = (isMajorAxisX ? relativePlayerPos.z : relativePlayerPos.x) < 0.f ? 1.f : -1.f;

	// 3. 우선순위와 위치를 저장할 벡터
	std::vector<std::pair<int, Mathf::Vector3>> placements;

	// 4. 격자를 순회하며 우선순위 계산 및 저장
	int iz = 0;
	for (float z = -m_chunsikRadius; z <= m_chunsikRadius; z += gridSpacing, ++iz)
	{
		int ix = 0;
		for (float x = -m_chunsikRadius; x <= m_chunsikRadius; x += gridSpacing, ++ix)
		{
			Mathf::Vector3 localOffset(x, 0, z);
			if (localOffset.LengthSquared() > m_chunsikRadius * m_chunsikRadius) continue;

			// ★★★ 회전(기울이기) 없이 월드 좌표를 계산 ★★★
			Mathf::Vector3 spawnPos = pos + localOffset;

			int majorIndex, minorIndex;
			if (isMajorAxisX) {
				majorIndex = ix; minorIndex = iz;
			}
			else {
				majorIndex = iz; minorIndex = ix;
			}

			int priority = static_cast<int>(
				(majorIndex * majorSortSign) * gridDimension + (minorIndex * minorSortSign)
				);

			placements.push_back({ priority, spawnPos });
		}
	}

	// 5. 우선순위(pair.first)에 따라 기본 오름차순 정렬
	std::sort(placements.begin(), placements.end(),[](auto a,auto b){
		return a.first < b.first;
		});

	// 6. 정렬된 순서대로 오브젝트 생성
	for (size_t i = 0; i < placements.size(); ++i) 
	{
		const auto& placement = placements[i];
		int sequenceNumber = static_cast<int>(i);
		Mathf::Vector3 objpos = placements[i].second;
		GameObject* floor = BP003Objs[i];
		BP003* script = floor->GetComponent<BP003>();
		floor->SetEnabled(true);
		//floor->GetComponent<Transform>()->SetWorldPosition(objpos);
		script->Initialize(this, objpos, BP003Damage, BP003RadiusSize, BP003Delay);
		script->isAttackStart = true;
	}
}

