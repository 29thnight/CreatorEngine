#include "TestBehavior.h"
#include "pch.h"
#include <cmath>

void TestBehavior::Start()
{
	// Initialize the behavior
	// This is where you can set up any initial state or properties for the behavior
	// For example, you might want to set the position, rotation, or scale of the object
	// that this behavior is attached to.
	// You can also use this method to register any event listeners or perform any other
	// setup tasks that are needed before the behavior starts running.
}

void TestBehavior::FixedUpdate(float fixedTick)
{
}

void TestBehavior::OnTriggerEnter(const Collision& collider)
{
}

void TestBehavior::OnTriggerStay(const Collision& collider)
{
}

void TestBehavior::OnTriggerExit(const Collision& collider)
{
}

void TestBehavior::OnCollisionEnter(const Collision& collider)
{
}

void TestBehavior::OnCollisionStay(const Collision& collider)
{
}

void TestBehavior::OnCollisionExit(const Collision& collider)
{
}

void TestBehavior::Update(float tick)
{
	std::cout << "TestBehavior::Updatedwdwdwd" << std::endl;

	GetComponent<Transform>().SetPosition(Mathf::Vector3(sinf(tick / 10.f), 0, 0));
	//static float time = 0.f;
	//time += tick * 0.1f * 3.141592f;
	//if (time > 3.141592f / 2.f)
	//	time -= 3.141592f;
	//// �Ʒ� ������ �������� ���� ȸ��
	//Mathf::Vector3 down = Mathf::Vector3(0, 1, 0);
	//
	//// coneAngle: �Ʒ� ���⿡�� �󸶳� ������ (����)
	//float coneAngle = -30.f / 180.f * 3.141592f;
	//
	//// ȸ����: ���� ���Ϳ� ������ �� �ƹ��ų� (ex. ������ ����)
	//Mathf::Vector3 right = Mathf::Vector3(1, 0, 0);
	//
	//// 1. �Ʒ� ���⿡�� coneAngle��ŭ ������ ���� ����
	//Mathf::Quaternion tilt = Mathf::Quaternion::CreateFromAxisAngle(right, coneAngle);
	//Mathf::Vector3 coneDir = Mathf::Vector3::Transform(down, tilt); // �Ʒ����� ������ ����
	//
	//// 2. ������ ���͸� �Ʒ� ���� ���� �������� ȸ��
	//Mathf::Quaternion spin = Mathf::Quaternion::CreateFromAxisAngle(down, time); // �ð��� ���� ȸ��
	//Mathf::Vector3 finalDir = Mathf::Vector3::Transform(coneDir, spin);
	//
	//GetOwner()->m_transform.SetRotation(Mathf::Quaternion::LookRotation(finalDir, { 0,1,0 }));
}

void TestBehavior::LateUpdate(float tick)
{
}
