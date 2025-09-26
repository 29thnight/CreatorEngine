#include "BP003.h"
#include "Entity.h"
#include "pch.h"
void BP003::Start()
{
}

void BP003::Update(float tick)
{
	if (!isInitialize || !isAttackStart) {
		return;
	}

	m_timer += tick;
	//장판 같은거 에니메이션 효과 줘야 한다고 하면 여기다가


	if (m_timer > m_delay) {
		Explosion(); 
	}

	if (ownerDestory) {
		GetOwner()->SetEnabled(false); //보스 죽으면 기능정지
		GetOwner()->Destroy(); //삭제 마크 찍음
	}
}

void BP003::Initialize(Entity* owner, int damage, float radius, float delay)
{
	m_ownerEntity = owner;
	m_damage = damage;
	m_radius = radius;
	m_delay = delay;
	isInitialize = true;
}

void BP003::Explosion()
{
	//폭발 
	//overlap 쿼리
	Mathf::Vector3 pos = GetOwner()->GetComponent<Transform>()->GetWorldPosition();

	OverlapInput input;
	input.position = pos;
	input.rotation = Mathf::Quaternion::Identity;
	//input.layerMask = ~0;

	std::vector<HitResult> res;

	//이때 추가 이펙트나 인디케이터 주던가 말던가

	PhysicsManagers->SphereOverlap(input, m_radius, res);

	//쿼리 감지시 데미지 처리
	for (auto& hit : res) {
		//tag 판단? 레이어 판단? 이름으로 찾을까? 원한다면 바꿔도 좋고 아님 말고 필요하다면 init때 레이어 마스크까지 넘기던지
		//플레이어만 찾자 아시스 안들어온다며
		if (hit.gameObject->m_tag == "Player") {
			Entity* objEntity = hit.gameObject->GetComponentDynamicCast<Entity>();
			objEntity->SendDamage(m_ownerEntity, m_damage);
		}
	}

	//
	GetOwner()->SetEnabled(false);

}

