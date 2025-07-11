#include "AsisMove.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "InputManager.h"
#include "pch.h"
#include <cmath>

#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"
using namespace Mathf;
inline static Mathf::Vector3 GetBothPointAndLineClosestPoint(const Mathf::Vector3& point, const Mathf::Vector3& lineStart, const Mathf::Vector3& lineEnd)
{
	Mathf::Vector3 lineDirection = lineEnd - lineStart;
	Mathf::Vector3 pointToLineStart = point - lineStart;
	
	float t = pointToLineStart.Dot(lineDirection) / lineDirection.Dot(lineDirection);
	Mathf::Clamp(t, 0.f, 1.f);
	Mathf::Vector3 closestPoint = lineStart + t * lineDirection;
	return closestPoint;
}
inline static float GetBothPointAndLineDistance(const Mathf::Vector3& point, const Mathf::Vector3& lineStart, const Mathf::Vector3& lineEnd)
{
	Mathf::Vector3 closestPoint = GetBothPointAndLineClosestPoint(point, lineStart, lineEnd);
	return Mathf::Distance(point, closestPoint);
}
inline static Mathf::Vector3 VectorProjection(const Mathf::Vector3& vector, const Mathf::Vector3& lineStart, const Mathf::Vector3& lineEnd)
{
	Mathf::Vector3 lineDirection = lineEnd - lineStart;
	float t = vector.Dot(lineDirection) / lineDirection.Dot(lineDirection);
	return lineStart + t * lineDirection;
}

void AsisMove::Start()
{
	auto paths = GameObject::Find("Paths");
	if (paths) {
		for (auto& index : paths->m_childrenIndices) {
			auto object = GameObject::FindIndex(index);
			if (object) {
				points.push_back(object->m_transform.GetWorldPosition());
			}
		}
	}
	else {
		auto point1 = GameObject::Find("TestPoint1");
		auto point2 = GameObject::Find("TestPoint2");
		auto point3 = GameObject::Find("TestPoint3");

		if (point1 != nullptr)
			points.push_back(point1->GetComponent<Transform>()->GetWorldPosition());
		if (point2 != nullptr)
			points.push_back(point2->GetComponent<Transform>()->GetWorldPosition());
		if (point3 != nullptr)
			points.push_back(point3->GetComponent<Transform>()->GetWorldPosition());
	}
#ifdef _DEBUG
	DebugPoint = GameObject::Find("DebugPoint");
#endif // _DEBUG
}

void AsisMove::FixedUpdate(float fixedTick)
{
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
}

void AsisMove::OnTriggerEnter(const Collision& collider)
{
	std::cout << "OnTriggerEnter ASIS MOVE" << std::endl;
}

void AsisMove::OnTriggerStay(const Collision& collider)
{
}

void AsisMove::OnTriggerExit(const Collision& collider)
{
}

void AsisMove::OnCollisionEnter(const Collision& collider)
{
	std::cout << "OnCollisionEnter ASIS MOVE" << std::endl;
}

void AsisMove::OnCollisionStay(const Collision& collider)
{
}

void AsisMove::OnCollisionExit(const Collision& collider)
{
}

void AsisMove::Update(float tick)
{

	return;





//	int pathSize = points.size();
//	int nextPointIndex = (currentPointIndex + 1) % 3;
//	Vector3 currentposition = GetOwner()->m_transform.GetWorldPosition();
//	float powPathRadius = pathRadius * pathRadius;
//
//	float powNextPointDistance =
//		(currentposition.x - points[nextPointIndex].x) * (currentposition.x - points[nextPointIndex].x) +
//		(currentposition.y - points[nextPointIndex].y) * (currentposition.y - points[nextPointIndex].y) +
//		(currentposition.z - points[nextPointIndex].z) * (currentposition.z - points[nextPointIndex].z);
//
//	Vector3 forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), GetOwner()->m_transform.GetWorldQuaternion());
//	Mathf::Vector3 direction = forward;
//
//	if (powNextPointDistance < powPathRadius) {
//		currentPointIndex = nextPointIndex; // Loop through the points
//		nextPointIndex = (currentPointIndex + 1) % 3; // Update next point index
//		nextMovePoint = points[nextPointIndex];
//		direction = Mathf::Normalize(points[nextPointIndex] - points[currentPointIndex]);//Mathf::Normalize(nextMovePoint - currentposition);
//	}
//
//	Vector3 closestPoint = GetBothPointAndLineClosestPoint(currentposition, points[currentPointIndex], points[nextPointIndex]); 
//	float powBothPosAndClosest = 
//		(currentposition.x - closestPoint.x) * (currentposition.x - closestPoint.x) +
//		(currentposition.y - closestPoint.y) * (currentposition.y - closestPoint.y) +
//		(currentposition.z - closestPoint.z) * (currentposition.z - closestPoint.z);
//
//
//	// 경로의 반지름을 이탈한 경우 다시 경로를 설정
//	if (powBothPosAndClosest > powPathRadius || 
//		(currentposition.x < std::min(points[currentPointIndex].x, points[nextPointIndex].x) ||
//		 currentposition.x > std::max(points[currentPointIndex].x, points[nextPointIndex].x) ||
//		 currentposition.z < std::min(points[currentPointIndex].z, points[nextPointIndex].z) ||
//		 currentposition.z > std::max(points[currentPointIndex].z, points[nextPointIndex].z)))
//	{
//		Vector3 predictPosition = (moveSpeed * predictNextTime) * forward + currentposition;
//		Vector3 OA = predictPosition - points[currentPointIndex];
//		Vector3 projectionPoint = VectorProjection(OA, points[currentPointIndex], points[nextPointIndex]);
//
//		Mathf::Clamp(projectionPoint.x, std::min(points[currentPointIndex].x, points[nextPointIndex].x) - pathRadius, std::max(points[currentPointIndex].x, points[nextPointIndex].x) + pathRadius);
//		Mathf::Clamp(projectionPoint.z, std::min(points[currentPointIndex].z, points[nextPointIndex].z) - pathRadius, std::max(points[currentPointIndex].z, points[nextPointIndex].z) + pathRadius);
//
//		// new destination point
//		nextMovePoint = projectionPoint;
//		direction = Mathf::Normalize(nextMovePoint - currentposition);
//	}
//
//#ifdef _DEBUG
//	DebugPoint->m_transform.SetPosition(nextMovePoint);
//#endif // DEBUG
//
//	direction.Normalize();
//	Vector3 right = Vector3::Up.Cross(direction);
//	if (right.LengthSquared() < 0.0001f)
//		right = Vector3::Right; // fallback for colinear
//	right.Normalize();
//	Vector3 up = direction.Cross(right);
//
//	Matrix rotMatrix = Matrix(
//		right.x, right.y, right.z, 0,
//		up.x, up.y, up.z, 0,
//		direction.x, direction.y, direction.z, 0,
//		0, 0, 0, 1
//	);
//
//	Quaternion rot = Quaternion::CreateFromRotationMatrix(rotMatrix);
//
//	Quaternion currentRot = GetOwner()->m_transform.GetWorldQuaternion();
//	Quaternion newRot = Quaternion::Slerp(currentRot, rot, rotateSpeed * tick);
//
//	GetOwner()->m_transform.SetRotation(newRot);
//
//	Vector3 newPosition = currentposition + direction * moveSpeed * tick;
//	GetOwner()->m_transform.SetPosition(newPosition);


	//// 처리 이후에도 해당 패스를 벗어난 경우, 새로운 패스 지정.
	//if ((newPosition.x < std::min(points[currentPointIndex].x, points[nextPointIndex].x) ||
	//	 newPosition.x > std::max(points[currentPointIndex].x, points[nextPointIndex].x) ||
	//	 newPosition.z < std::min(points[currentPointIndex].z, points[nextPointIndex].z) ||
	//	 newPosition.z > std::max(points[currentPointIndex].z, points[nextPointIndex].z))) {

	//	currentPointIndex = nextPointIndex; // Loop through the points
	//	nextPointIndex = (currentPointIndex + 1) % 3; // Update next point index
	//	nextMovePoint = points[nextPointIndex];
	//	direction = Mathf::Normalize(nextMovePoint - currentposition);
	//}


	//float powNextPointDistance = 
	//	(newPosition.x - points[(currentPointIndex + 1) % 3].x) * (newPosition.x - points[(currentPointIndex + 1) % 3].x) +
	//	(newPosition.y - points[(currentPointIndex + 1) % 3].y) * (newPosition.y - points[(currentPointIndex + 1) % 3].y) +
	//	(newPosition.z - points[(currentPointIndex + 1) % 3].z) * (newPosition.z - points[(currentPointIndex + 1) % 3].z);
	//if (powNextPointDistance < powPathRadius) {
	//	currentPointIndex = (currentPointIndex + 1) % 3; // Loop through the points
	//	nextMovePoint = points[currentPointIndex + 1];
	//}
}

void AsisMove::LateUpdate(float tick)
{
}
