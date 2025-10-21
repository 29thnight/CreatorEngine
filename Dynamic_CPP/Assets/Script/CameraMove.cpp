#include "CameraMove.h"
#include "pch.h"
#include "GameManager.h"
void CameraMove::Start()
{
	target = GameObject::Find("Asis");

	GM = GameObject::Find("GameManager")->GetComponent<GameManager>();
	if (GM)
	{
		GM->testCamera = GetOwner();
	}
}

void CameraMove::Update(float tick)
{
}

void CameraMove::LateUpdate(float tick)
{
	
	if (GM)
	{
		if (GM->TestCameraControll == false)
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
		else
		{
			//게임패드입력으로 카메라 움직이기

		}
	}
}


void CameraMove::OnCameraControll()
{
	if (GM)
	{
		GM->TestCameraControll = false;
	}
}

void CameraMove::OffCameraCOntroll()
{
	if (GM)
	{
		GM->TestCameraControll = true;
	}
}

void CameraMove::CameraMoveFun(Mathf::Vector2 dir)
{
	if (!m_isCallStart) return;
	if (GM)
	{
		if (GM->TestCameraControll)
		{
			//게임패드입력으로 카메라 움직이기
			if (GM->testCamera)
			{
				float offsetX = cameraMoveSpeed * dir.x;
				float offsetZ = cameraMoveSpeed * dir.y;
				Mathf::Vector3 addpos = { offsetX , 0,offsetZ };

				GM->testCamera->GetComponent<Transform>()->AddPosition(addpos);

			}

		}
	}
}