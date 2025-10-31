#include "BP003.h"
#include "Entity.h"
#include "pch.h"
#include "PrefabUtility.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Core.Random.h"
#include "EffectComponent.h"
#include "TweenManager.h"

void BP003::Start()
{
	meshRenderers = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
	for (auto& m : meshRenderers) {
		m->m_Material = m->m_Material->Instantiate(m->m_Material, "clonebomb");
	}
}

void BP003::Update(float tick)
{
	if (!isInitialize || !isAttackStart) {
		return;
	}

	m_timer += tick;
	//장판 같은거 에니메이션 효과 줘야 한다고 하면 여기다가

	//여기는 궤도 공전 효과

	if (m_useOrbiting) {
		if (m_orbitingStartDelay < m_timer) {
			isOrbiting = true;
		}

		if ((m_delay - m_orbitingEndDelay) < m_timer) {
			isOrbiting = false;
		}
	}
	
	if (isOrbiting) 
	{

		Mathf::Vector3 ownerPos = m_ownerEntity->GetOwner()->GetComponent<Transform>()->GetWorldPosition();

		float orbitingSpeed = 1.0f; //회전 속도 

		// 1. 매 프레임 궤도 각도를 증가시킴
		float clockwise = m_clockWise ? 1.0f : -1.0f; //시계방향 반시계반향
		m_orbitAngle += orbitingSpeed * clockwise * tick;

		// 2. 원 위의 x, z 좌표 계산
		float offsetX = cos(m_orbitAngle) * m_orbitDistance;
		float offsetZ = sin(m_orbitAngle) * m_orbitDistance;

		// 3. 목표물 위치에 offset을 더해 나의 새로운 위치를 계산
		
		Mathf::Vector3 newPosition = ownerPos + Mathf::Vector3(offsetX, 0, offsetZ);

		// 4. 내 위치를 직접 설정
		m_pOwner->GetComponent<Transform>()->SetWorldPosition(newPosition);

	}

	


	if (m_timer > m_delay) {
		Explosion(); 
	}
	else {
		ShaderUpdate();
	}

	if (ownerDestory) {
		GetOwner()->SetEnabled(false); //보스 죽으면 기능정지
		GetOwner()->Destroy(); //삭제 마크 찍음
	}
}

void BP003::Initialize(Entity* owner, Mathf::Vector3 pos, int damage, float radius, float delay,bool itemDrop, bool useOrbiting, bool clockwise)
{
	m_ownerEntity = owner;
	m_damage = damage;
	m_radius = radius;
	m_itemDrop = itemDrop;
	m_delay = delay;
	m_timer = 0.0f;

	m_pOwner->GetComponent<Transform>()->SetScale({ m_radius ,1 ,m_radius });

	//일단 초기화때 넣자
	m_useOrbiting = useOrbiting;
	m_clockWise = clockwise;
	//자체 회전을 위한 내용
	//우선 변수 보스랑 거리 유지를 해야 하니 소환된 이후 보스와의 거리를 측정하여 들고있자
	Mathf::Vector3 ownerPos = m_ownerEntity->GetOwner()->GetComponent<Transform>()->GetWorldPosition();
	GetOwner()->GetComponent<Transform>()->SetWorldPosition(pos);

	Mathf::Vector3 toMe = pos - ownerPos;
	toMe.y = 0;
	m_orbitAngle = atan2(toMe.z, toMe.x); //소환됬을때의 각도-->회전시 일정한 값을 
	m_orbitDistance = toMe.Length(); //소환됬을때 거리 -->회전식 일정한 거리가 떨어져 있을수 있도록
	
	auto effcomp = GetOwner()->GetComponent<EffectComponent>();
	effcomp->Apply();

	isInitialize = true;
}

void BP003::ShaderUpdate()
{
	float t = m_timer / m_delay;
	for (auto& m : meshRenderers) {
		m->m_Material->TrySetValue("Param", "lerpValue", &t, sizeof(float));
		m->m_Material->TrySetValue("Param", "maxScale", &maxScale, sizeof(float));
		m->m_Material->TrySetValue("Param", "scaleFrequency", &scaleFrequency, sizeof(float));
		m->m_Material->TrySetValue("Param", "rotFrequency", &rotFrequency, sizeof(float));
		m->m_Material->TrySetValue("FlashBuffer", "flashStrength", &t, sizeof(float));
		m->m_Material->TrySetValue("FlashBuffer", "flashFrequency", &flashFrequency, sizeof(float));
	}
}

void BP003::Explosion()
{
	//폭발 
	//overlap 쿼리
	Mathf::Vector3 pos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

	OverlapInput input;
	input.position = pos;
	input.rotation = Mathf::Quaternion::Identity;
	input.layerMask = 1 << 5; // 플레이어 레이어만 검사

	std::vector<HitResult> res;

	//이때 추가 이펙트나 인디케이터 주던가 말던가
	Prefab* ExplosionEff = nullptr;
	ExplosionEff = PrefabUtilitys->LoadPrefab("BossExplosion");
	GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(ExplosionEff, "entityItem");
	itemObj->GetComponent<Transform>()->SetWorldPosition(pos);
	itemObj->GetComponent<EffectComponent>()->Apply();


	PhysicsManagers->SphereOverlap(input, m_radius, res);

	//쿼리 감지시 데미지 처리
	for (auto& hit : res) {
		//tag 판단? 레이어 판단? 이름으로 찾을까? 원한다면 바꿔도 좋고 아님 말고 필요하다면 init때 레이어 마스크까지 넘기던지
		//플레이어만 찾자 아시스 안들어온다며
		
		Entity* objEntity = hit.gameObject->GetComponentDynamicCast<Entity>();
		objEntity->SendDamage(m_ownerEntity, m_damage);
	}

	// 아이템 드랍 불값으로 판정 겟수 한게 고정
	if (m_itemDrop) {  
		ItemDrop();
	}

	m_timer = 0.0f;
	GetOwner()->SetEnabled(false);
}

void BP003::ItemDrop()
{
	Random<int> randtype(0, 3); //타입은 4개 종류 랜덤

	int type = randtype();
	Prefab* itemPrefab = nullptr;
	switch (type)
	{
	case 0:
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxMushroom");
		break;
	case 1:
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxMineral");
		break;
	case 2:
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxFruit");
		break;
	case 3:
		itemPrefab = PrefabUtilitys->LoadPrefab("BoxFlower");
		break;
	default:
		break;
	}

	if (itemPrefab)
	{
		GameObject* itemObj = PrefabUtilitys->InstantiatePrefab(itemPrefab, "entityItem");
		Mathf::Vector3 spawnPos = GetOwner()->m_transform.GetWorldPosition();
		spawnPos.y += 0.1f;
		itemObj->m_transform.SetPosition(spawnPos);

		Random<float> randX(-3.0f, 3.0f);
		Random<float> randY(0.2f, 1.f);
		Random<float> randZ(-3.0f, 3.0f);

		float randx = randX();
		float randy = randY();
		float randz = randZ();

		Mathf::Vector3 temp = { randx, randy,randz };
		float f = Random<float>(2.f, 3.f).Generate();
		auto tween = std::make_shared<Tweener<float>>(
			[=]() { return 0.f; },
			[=](float val) {
				Mathf::Vector3 pos = spawnPos;
				float force = f; // 중력 비슷하게 y축 곡선
				pos.x = Mathf::Lerp(spawnPos.x, spawnPos.x + temp.x, val);
				pos.z = Mathf::Lerp(spawnPos.z, spawnPos.z + temp.z, val);
				pos.y = Mathf::Lerp(spawnPos.y, spawnPos.y + temp.y, val)
					+ force * (1 - (2 * val - 1) * (2 * val - 1));
				itemObj->m_transform.SetPosition(pos);
			},
			1.f,
			.5f,
			[](float t) { return Easing::Linear(t); }
		);

		auto GM = GameObject::Find("GameManager");
		if (GM)
		{
			auto tweenManager = GM->GetComponent<TweenManager>();
			if (tweenManager)
			{
				tweenManager->AddTween(tween);
			}
		}
	}
}

