#include "AsisMove.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "InputManager.h"
#include "pch.h"
#include <cmath>

#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"

void AsisMove::Start()
{
}

void AsisMove::FixedUpdate(float fixedTick)
{

	/*
	int pathSize = points.size();
	int nextPointIndex = (currentPointIndex + 1) % pathSize;
	Vector3 currentPosition = GetOwner()->m_transform.GetWorldPosition();
	Quaternion currentRotation = GetOwner()->m_transform.GetWorldQuaternion();
	currentRotation.Normalize();
	Vector3 currentForward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), currentRotation);
	Vector3 predictNextPosition = currentPosition + currentForward * m_moveSpeed * m_predictNextTime;

	Vector3 dir = Mathf::Normalize(points[currentPointIndex] - points[nextPointIndex]); // B → A 방향 단위 벡터
	Vector3 endResult = points[nextPointIndex] + dir * m_pathRadius;
	Vector3 startResult = points[currentPointIndex] - dir * m_pathRadius;

	Vector3 closestPoint = GetBothPointAndLineClosestPoint(predictNextPosition, startResult, endResult);
	//Vector3 closestPoint = GetBothPointAndLineClosestPoint(predictNextPosition, points[currentPointIndex], points[nextPointIndex]);

	if (DebugPoint)
		DebugPoint->m_transform.SetPosition(closestPoint);
	Mathf::Vector3 direction = currentForward;

	float rotDownSpeed = 1.f;

	// 1. 미래위치의 투영점이 경로의 반지름을 벗어난 경우, 경로를 재설정
	if (Mathf::Distance(predictNextPosition, closestPoint) > m_pathRadius) {
		nextMovePoint = closestPoint; // 새로운 목적지 설정
		direction = Mathf::Normalize(nextMovePoint - currentPosition);
		Vector3 right = Vector3::Up.Cross(direction);
		if (right.LengthSquared() < 0.0001f)
			right = Vector3::Right; // fallback for colinear
		right.Normalize();
		Vector3 up = direction.Cross(right);

		Matrix rotMatrix = Matrix(
			right.x, right.y, right.z, 0,
			up.x, up.y, up.z, 0,
			direction.x, direction.y, direction.z, 0,
			0, 0, 0, 1
		);

		Quaternion rot = Quaternion::CreateFromRotationMatrix(rotMatrix);
		Quaternion newRot = Quaternion::Slerp(currentRotation, rot, m_rotateSpeed * fixedTick);

		rotDownSpeed = rot.Dot(GetComponent<Transform>().GetWorldQuaternion());
		rotDownSpeed = std::clamp(rotDownSpeed, 0.f, 1.f);

		GetOwner()->m_transform.SetRotation(newRot);
	}

	Vector3 newPosition = currentPosition + direction * m_moveSpeed * fixedTick * rotDownSpeed;
	GetOwner()->m_transform.SetPosition(newPosition);

	float newDistance = Mathf::Distance(newPosition, points[nextPointIndex]);
	if (newDistance <= m_pathRadius) {
		currentPointIndex = nextPointIndex; // Loop through the points
	}
	*/
}

void AsisMove::OnTriggerEnter(const Collision& collider)
{
}

void AsisMove::OnTriggerStay(const Collision& collider)
{
}

void AsisMove::OnTriggerExit(const Collision& collider)
{
}

void AsisMove::OnCollisionEnter(const Collision& collider)
{
}

void AsisMove::OnCollisionStay(const Collision& collider)
{
}

void AsisMove::OnCollisionExit(const Collision& collider)
{
}

void AsisMove::Update(float tick)
{
}

void AsisMove::LateUpdate(float tick)
{
}
