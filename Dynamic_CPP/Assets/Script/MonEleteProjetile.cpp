#include "MonEleteProjetile.h"
#include "pch.h"
#include "Entity.h"
#include "Camera.h"

void MonEleteProjetile::Start()
{
}


void MonEleteProjetile::OnTriggerEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Action(collision.otherObj);
	}
	//TODO : 
	//광물이나 벽등의 충돌되었을때 사라지는 내용이 필요한가?
}

void MonEleteProjetile::OnCollisionEnter(const Collision& collision)
{
	//다른 오브젝트 충돌을 막기위해 trigger 객체로 할꺼 같지만 일단 이것도 만들자 
	if (collision.otherObj->m_tag.ToString() == "Player" || collision.otherObj->m_tag.ToString() == "Asis") {
		Action(collision.otherObj);
	}
	//TODO : 
	//광물이나 벽등의 충돌되었을때 사라지는 내용이 필요한가?
}

void MonEleteProjetile::Update(float tick)
{
	auto camera = CameraManagement->GetLastCamera().get();

	//초기화 시키고 이동 명령 했으면 초기화된 방향과 속력으로 이동
	if (isInitialize && m_isMoving) {
		Mathf::Vector3 v = m_Dir * m_Speed * tick;
		m_pTransform->AddPosition(v);
	}

	auto camViewProj = camera->CalculateView() * camera->CalculateProjection();
	auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	float w = XMVectorGetW(clipSpacePos);
	if (w < 0.001f) {
		//카메라 뒤편
		//여기로 오면 뭔가 잘못 된거임
		RevertPool();
	}

	XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	float x = XMVectorGetX(ndcPos);
	float y = XMVectorGetY(ndcPos);
	x = abs(x);
	y = abs(y);

	
	if (x > 1 || y>1)
	{
		//카메라 x축 혹은 y축 밖
		//여기는 일절 충돌 없이 카메라 밖으로 나간경우
		RevertPool();
	}
}

void MonEleteProjetile::Initallize(Entity* owner, int damage, float speed, Mathf::Vector3 dir)
{
	ownerEntity = owner;
	m_Damege = damage;
	m_Speed = speed;
	m_Dir = dir;
	isInitialize = true;
}

void MonEleteProjetile::Action(GameObject* target)
{
	if (!isInitialize) return;

	//다중충돌 안한다고 가정했을때 1회성으로 끝내려고
	if (isTrigger) return;

	Entity* targetEntity = target->GetComponentDynamicCast<Entity>();
	targetEntity->SendDamage(ownerEntity, m_Damege);

	//todo : 여기서 이제 뚫고 갈건지 비화성화 할건지? 일단은 일회성으로 사라지게 하자
	RevertPool();
}

void MonEleteProjetile::RevertPool()
{
	//나아가던 객체가 행동을 멈추고 비활성화 되어 풀로 돌아가는 내용
	if (OwnerDestroy)
	{
		GetOwner()->Destroy();
	}
	m_isMoving = false;
	GetOwner()->SetEnabled(false);
}

