#include "EditorCameraRig.h"
#include "InputManager.h"
#include "ImGuiRegister.h"

#include <algorithm>
#include <cmath>

void EditorCameraRig::HandleMovement(float deltaTime)
{
	float x = 0.f, y = 0.f, z = 0.f;
	constexpr float minSpeed = 10.f;
	constexpr float maxSpeed = 100.f;

	if (InputManagement->IsKeyPressed('W') || ImGui::IsKeyDown(ImGuiKey_W)) z += 1.f;
	if (InputManagement->IsKeyPressed('S') || ImGui::IsKeyDown(ImGuiKey_S)) z -= 1.f;
	if (InputManagement->IsKeyPressed('A') || ImGui::IsKeyDown(ImGuiKey_A)) x -= 1.f;
	if (InputManagement->IsKeyPressed('D') || ImGui::IsKeyDown(ImGuiKey_D)) x += 1.f;
	if (InputManagement->IsKeyPressed('Q') || ImGui::IsKeyDown(ImGuiKey_Q)) y -= 1.f;
	if (InputManagement->IsKeyPressed('E') || ImGui::IsKeyDown(ImGuiKey_E)) y += 1.f;

	if (InputManagement->IsWheelUp())
	{
		m_speedMul = std::clamp(m_speedMul + 0.01f, 0.01f, 2.f);
		m_speed = std::clamp(m_speed * m_speedMul, minSpeed, maxSpeed);
	}
	if (InputManagement->IsWheelDown())
	{
		m_speedMul = std::clamp(m_speedMul - 0.01f, 0.01f, 2.f);
		m_speed = std::clamp(m_speed * m_speedMul, minSpeed, maxSpeed);
	}

	if (InputManagement->IsMouseButtonPressed(MouseKey::RIGHT) ||
		ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		m_deltaPitch += InputManagement->GetMouseDelta().y * 0.01f;
		m_deltaYaw += InputManagement->GetMouseDelta().x * 0.01f;

		const XMVECTOR qYaw = XMQuaternionRotationAxis(Camera::UP, m_deltaYaw);
		const XMVECTOR right = XMVector3Rotate(Camera::RIGHT, qYaw);
		const XMVECTOR qPitch = XMQuaternionRotationAxis(right, m_deltaPitch);
		const XMVECTOR cameraRotation = XMQuaternionNormalize(
			XMQuaternionMultiply(qYaw, qPitch));

		m_camera.m_forward = XMVector3Normalize(
			XMVector3Rotate(Camera::FORWARD, cameraRotation));
		m_camera.m_up = XMVector3Normalize(
			XMVector3Rotate(Camera::UP, cameraRotation));
		m_camera.m_right = XMVector3Normalize(
			XMVector3Cross(m_camera.m_up, m_camera.m_forward));
		m_camera.rotate = cameraRotation;
		m_camera.m_rotation = cameraRotation;
	}

	m_camera.m_eyePosition +=
		((z * m_camera.m_forward) + (y * m_camera.m_up) +
			(x * m_camera.m_right)) * m_speed * deltaTime;
	m_camera.m_lookAt = m_camera.m_eyePosition + m_camera.m_forward;
}

void EditorCameraRig::ApplySnapshot(const FrameCameraSnapshot& snapshot) noexcept
{
	m_camera.m_eyePosition = snapshot.eyePosition;
	m_camera.m_forward = XMVector3Normalize(snapshot.forward);
	m_camera.m_up = XMVector3Normalize(snapshot.up);
	m_camera.m_right = XMVector3Normalize(snapshot.right);
	m_camera.m_lookAt = snapshot.eyePosition + m_camera.m_forward;
	m_camera.m_fov = snapshot.fov;
	m_camera.m_nearPlane = snapshot.nearPlane;
	m_camera.m_farPlane = snapshot.farPlane;
	m_camera.m_isOrthographic = snapshot.isOrthographic;

	const XMMATRIX rotationMatrix(
		m_camera.m_right, m_camera.m_up, m_camera.m_forward,
		XMVectorSet(0.f, 0.f, 0.f, 1.f));
	const XMVECTOR rotation = XMQuaternionNormalize(
		XMQuaternionRotationMatrix(rotationMatrix));
	m_camera.rotate = rotation;
	m_camera.m_rotation = rotation;

	Mathf::Vector3 forward{};
	XMStoreFloat3(&forward, m_camera.m_forward);
	SetOrientation(std::atan2(forward.x, forward.z),
		-std::asin(std::clamp(forward.y, -1.f, 1.f)));
}
