#include "CameraMove.h"
#include "pch.h"
#include "GameManager.h"
#include "Entity.h"
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
		if (GM->TestCameraControll == false && OnCaculCamera ==false)
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


			float followTimer = 0.f;

			if (distance > detectRange)
			{
				targetPosition = targetPos;
				followTimer = tick / followSpeed / 3.f;
			}
			else {
				followTimer = 0.f;
			}

			Mathf::Vector3 dir = targetPosition - currentPos;
			dir.y = 0.f;
			dir.Normalize();


			//transform->SetPosition(currentPos + dir * tick * followSpeed + offset);
			transform->SetPosition(Mathf::Vector3::Lerp(currentPos, targetPosition, followTimer) + offset);


			//Mathf::Vector3 lerpPos = Mathf::Lerp(targetPos, currentPos, tick * followSpeed);
			//lerpPos.y = 0.f;
			//transform->SetPosition(lerpPos + offset);
		}
		else if(GM->TestCameraControll == false && OnCaculCamera == true)
		{
			//플레이어 2명 아시스 위치에따라 카메라 위치계산

			if (!GM->GetAsis().empty() && GM->GetPlayers().size() >= 2)
			{

				GameObject* asis = GM->GetAsis()[0]->GetOwner();
				GameObject* player1 = GM->GetPlayers()[0]->GetOwner();
				GameObject* player2 = GM->GetPlayers()[1]->GetOwner();

				Mathf::Vector3 asisPos = asis->m_transform.GetWorldPosition();
				Mathf::Vector3 P1Pos = player1->m_transform.GetWorldPosition();
				Mathf::Vector3 P2Pos = player2->m_transform.GetWorldPosition();


				Mathf::Vector3 centerPos = (asisPos + P1Pos + P2Pos) / 3;


				Transform* transform = GetOwner()->GetComponent<Transform>();
				Mathf::Vector3 targetPos = centerPos;
				targetPos.y = 0.f;
				Mathf::Vector3 currentPos = transform->GetWorldPosition();
				if (preOffset.x != 0.0f || preOffset.y != 0.0f || preOffset.z != 0.0f)
					currentPos -= preOffset;
				else
					currentPos -= offset;
				currentPos.y = 0.f;
				float distance = Mathf::Vector3::Distance(targetPos, currentPos);


				float followTimer = 0.f;

				if (distance > detectRange)
				{
					targetPosition = targetPos;
					followTimer = tick / followSpeed / 3.f;
				}
				else {
					followTimer = 0.f;
				}

				Mathf::Vector3 dir = targetPosition - currentPos;
				dir.y = 0.f;
				dir.Normalize();


				Mathf::Vector3 realOffset = offset;
				float farDistance = 0; 
				//currentPos  에 3개다 거리계산해서 젤먼애 구해서 걔기준 일정비례 offset lerp

				float asisDistance = Mathf::Vector3::Distance(asisPos, centerPos);
				float P1Distance = Mathf::Vector3::Distance(P1Pos, centerPos);
				float P2Distance = Mathf::Vector3::Distance(P2Pos, centerPos);
	
				farDistance = std::max(asisDistance, std::max(P1Distance, P2Distance));
				float t = farDistance / maxDistance;
				t = std::clamp(t, 0.0f, 1.0f);
		
				realOffset = Mathf::Vector3::Lerp(minOffset, maxOffset, t);
				preOffset = realOffset;
				//transform->SetPosition(currentPos + dir * tick * followSpeed + offset);
				Mathf::Vector3 realCurPos = Mathf::Vector3::Lerp(currentPos, targetPosition, followTimer) + realOffset;
				transform->SetPosition(Mathf::Vector3::Lerp(currentPos, targetPosition, followTimer) + realOffset);




			}

		}
		else if(GM->TestCameraControll == true)
		{

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