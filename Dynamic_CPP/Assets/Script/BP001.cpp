#include "BP001.h"
#include "pch.h"
#include "Entity.h"
void BP001::Start()
{
}

void BP001::OnTriggerEnter(const Collision& collision)
{
	//todo : 벽 충돌 감지도 해서 터트려야 해요
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Explosion();
	}
}

void BP001::OnCollisionEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Explosion();
	}
}

void BP001::Update(float tick)
{
	if (!isInitialize || !isAttackStart) {
		return;
	}

	m_timer += tick;

	//이동
	Transform* tr = GetOwner()->GetComponent<Transform>();
	Mathf::Vector3 pos = direction * m_speed * tick;
	tr->AddPosition(pos);

	//시간 되면 폭발
	if (m_timer > m_delay) { 
		Explosion();
	}
}

void BP001::Initialize(Entity* owner, Mathf::Vector3 pos, Mathf::Vector3 dir, int damage, float radius, float delay, float speed)
{
	m_ownerEntity = owner;
	GetOwner()->GetComponent<Transform>()->SetWorldPosition(pos);
	direction = dir;
	m_damage = damage;
	m_radius = radius;
	m_delay = delay;
	m_speed = speed;
	isExplode = false; //폭파 여부 초기화

	isInitialize = true;
}

void BP001::Explosion()
{ 
	if (isExplode) return; //폭파 일회성

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

	isExplode = true;
	m_timer = 0.0f;
	GetOwner()->SetEnabled(false);
}

