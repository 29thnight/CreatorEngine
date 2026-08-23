#include "EditorCameraController.h"
#include "Camera.h"
#include "InputManager.h"
#include "ImGuiRegister.h"

#include <algorithm>

EditorCameraController& EditorCameraController::Get()
{
	static EditorCameraController instance;
	return instance;
}

void EditorCameraController::HandleMovement(Camera& camera, float deltaTime)
{
	float x = 0.f, y = 0.f, z = 0.f;
	constexpr float minSpeed = 10.f;
	constexpr float maxSpeed = 100.f;

	if (InputManagement->IsKeyPressed('W') || ImGui::IsKeyDown(ImGuiKey_W))
	{
		z += 1.f;
	}
	if (InputManagement->IsKeyPressed('S') || ImGui::IsKeyDown(ImGuiKey_S))
	{
		z -= 1.f;
	}
	if (InputManagement->IsKeyPressed('A') || ImGui::IsKeyDown(ImGuiKey_A))
	{
		x -= 1.f;
	}
	if (InputManagement->IsKeyPressed('D') || ImGui::IsKeyDown(ImGuiKey_D))
	{
		x += 1.f;
	}
	if (InputManagement->IsKeyPressed('Q') || ImGui::IsKeyDown(ImGuiKey_Q))
	{
		y -= 1.f;
	}
	if (InputManagement->IsKeyPressed('E') || ImGui::IsKeyDown(ImGuiKey_E))
	{
		y += 1.f;
	}

	if (InputManagement->IsWheelUp())
	{
		m_speedMul += 0.01f;
		m_speedMul = std::clamp(m_speedMul, 0.01f, 2.f);
		m_speed = m_speed * m_speedMul;
		m_speed = std::clamp(m_speed, minSpeed, maxSpeed);
	}

	if (InputManagement->IsWheelDown())
	{
		m_speedMul -= 0.01f;
		m_speedMul = std::clamp(m_speedMul, 0.01f, 2.f);
		m_speed = m_speed * m_speedMul;
		m_speed = std::clamp(m_speed, minSpeed, maxSpeed);
	}

	XMVECTOR m_rotationQuat = XMQuaternionIdentity();
	//Change the Camera Rotaition Quaternion Not Use XMQuaternionRotationRollPitchYaw
	if (InputManagement->IsMouseButtonPressed(MouseKey::RIGHT) || ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		// 마우스 이동량 가져오기
		m_deltaPitch += InputManagement->GetMouseDelta().y * 0.01f;
		m_deltaYaw += InputManagement->GetMouseDelta().x * 0.01f;

		XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), m_deltaYaw);

		// qYaw 기준으로 회전된 로컬 X축
		XMVECTOR right = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), qYaw);

		XMVECTOR qPitch = XMQuaternionRotationAxis(right, m_deltaPitch);

		// 최종 쿼터니언 = Yaw → Pitch
		XMVECTOR cameraRot = XMQuaternionMultiply(qYaw, qPitch);

		// Forward 벡터 구하기
		camera.m_forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), cameraRot);
		camera.m_up = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), cameraRot);
		camera.m_right = XMVector3Cross(camera.m_up, camera.m_forward);
		camera.rotate = cameraRot;
	}

	camera.m_eyePosition +=
		((z * camera.m_forward) + (y * camera.m_up) + (x * camera.m_right)) * m_speed * deltaTime;
	camera.m_lookAt = camera.m_eyePosition + camera.m_forward;
}

