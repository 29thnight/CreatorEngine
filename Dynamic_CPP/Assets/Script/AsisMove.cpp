#include "AsisMove.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "InputManager.h"
#include "pch.h"
#include <cmath>

void AsisMove::Start()
{
	auto point1 = GameObject::Find("TestPoint1");
	auto point2 = GameObject::Find("TestPoint2");
	auto point3 = GameObject::Find("TestPoint3");

	if (point1 != nullptr) {
		points[0] = point1->GetComponent<Transform>()->GetWorldPosition();
	}
	if(point2 != nullptr)
		points[1] = point2->GetComponent<Transform>()->GetWorldPosition();
	if(point3 != nullptr)
		points[2] = point3->GetComponent<Transform>()->GetWorldPosition();

	std::cout << points[0].x << ", " << points[0].y << ", " << points[0].z << std::endl;
	std::cout << points[1].x << ", " << points[1].y << ", " << points[1].z << std::endl;
	std::cout << points[2].x << ", " << points[2].y << ", " << points[2].z << std::endl;
}

void AsisMove::FixedUpdate(float fixedTick)
{
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
	//auto point1 = GameObject::Find("TestPoint1");
	//auto point2 = GameObject::Find("TestPoint2");
	//auto point3 = GameObject::Find("TestPoint3");
	//
	//if (point1 != nullptr) {
	//	points[0] = point1->GetComponent<Transform>()->GetWorldPosition();
	//}
	//if (point2 != nullptr)
	//	points[1] = point2->GetComponent<Transform>()->GetWorldPosition();
	//if (point3 != nullptr)
	//	points[2] = point3->GetComponent<Transform>()->GetWorldPosition();
	//
	//std::cout << points[0].x << ", " << points[0].y << ", " << points[0].z << std::endl;
	//std::cout << points[1].x << ", " << points[1].y << ", " << points[1].z << std::endl;
	//std::cout << points[2].x << ", " << points[2].y << ", " << points[2].z << std::endl;

	Transform& playerTransform = GetOwner()->m_transform;

	Mathf::Vector3 playerPosition = playerTransform.GetWorldPosition();
	nextMovePoint = points[currentPointIndex];
	float distanceToNextPoint = Mathf::Distance(playerPosition, nextMovePoint);
	if (distanceToNextPoint < 0.1f)
	{
		currentPointIndex = (currentPointIndex + 1) % 3; // Loop through the points
		nextMovePoint = points[currentPointIndex];
	}
	Mathf::Vector3 direction = Mathf::Normalize(nextMovePoint - playerPosition);
	playerTransform.SetPosition(playerPosition + direction * moveSpeed * tick);

}

void AsisMove::LateUpdate(float tick)
{
}
