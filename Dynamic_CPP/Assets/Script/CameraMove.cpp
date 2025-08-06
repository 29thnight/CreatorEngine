#include "CameraMove.h"
#include "pch.h"
void CameraMove::Start()
{
	target = GameObject::Find("Asis");
}

void CameraMove::Update(float tick)
{
	if (target == nullptr)
	{
		return;
	}
	Transform* transform = GetOwner()->GetComponent<Transform>();
	Mathf::Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();
	Mathf::Vector3 currentPos = transform->GetWorldPosition();
	
	transform->SetPosition(Mathf::Lerp(targetPos, currentPos, tick * followSpeed) + offset);
}

