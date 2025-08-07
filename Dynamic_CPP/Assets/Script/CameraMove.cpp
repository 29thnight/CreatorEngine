#include "CameraMove.h"
#include "pch.h"
void CameraMove::Start()
{
	target = GameObject::Find("Asis");
}

void CameraMove::Update(float tick)
{
}

void CameraMove::LateUpdate(float tick)
{
	if (target == nullptr)
	{
		return;
	}

	Transform* transform = GetOwner()->GetComponent<Transform>();
	Mathf::Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();
	targetPos.y = 0.f;
	Mathf::Vector3 currentPos = transform->GetWorldPosition();
	currentPos = currentPos - offset;
	currentPos.y = 0.f;
	float distance = Mathf::Vector3::Distance(targetPos, currentPos);

	if (distance > detectRange)
	{
		targetPosition = targetPos;
		followTimer = 0.f;
	}

	followTimer += tick / followSpeed / 3.f;
	if (followTimer > followSpeed)
	{
		
	}

	Mathf::Vector3 dir = targetPosition - currentPos;
	dir.y = 0.f;
	dir.Normalize();

	//transform->SetPosition(currentPos + dir * tick * followSpeed + offset);
	transform->SetPosition(Mathf::Vector3::Lerp(currentPos, targetPosition, followTimer / followSpeed) + offset);


	//Mathf::Vector3 lerpPos = Mathf::Lerp(targetPos, currentPos, tick * followSpeed);
	//lerpPos.y = 0.f;
	//transform->SetPosition(lerpPos + offset);
}

