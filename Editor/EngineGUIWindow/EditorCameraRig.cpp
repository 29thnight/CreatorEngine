#include "EditorCameraRig.h"
#include "InputManager.h"
#include "ImGuiRegister.h"

#include <algorithm>
#include <cmath>
#include <mathematics/transform.hpp>

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

		const math::quaternion qYaw =
			math::quaternion_from_axis_angle(Camera::UP, m_deltaYaw);
		const math::vector3 right = math::rotate(Camera::RIGHT, qYaw);
		const math::quaternion qPitch =
			math::quaternion_from_axis_angle(right, m_deltaPitch);
		const math::quaternion cameraRotation = math::normalize(qYaw * qPitch);

		m_camera.m_forward = math::normalize(
			math::rotate(Camera::FORWARD, cameraRotation));
		m_camera.m_up = math::normalize(
			math::rotate(Camera::UP, cameraRotation));
		m_camera.m_right = math::normalize(
			math::cross(m_camera.m_up, m_camera.m_forward));
		m_camera.rotate = cameraRotation;
	}

	const math::vector3 movement =
		(m_camera.m_forward * z + m_camera.m_up * y + m_camera.m_right * x) *
		(m_speed * deltaTime);
	m_camera.m_eyePosition += movement;
}

void EditorCameraRig::ApplySnapshot(const FrameCameraSnapshot& snapshot) noexcept
{
	m_camera.m_eyePosition = snapshot.eyePosition;
	m_camera.m_forward = math::normalize(snapshot.forward);
	m_camera.m_up = math::normalize(snapshot.up);
	m_camera.m_right = math::normalize(snapshot.right);
	m_camera.m_fov = snapshot.fov;
	m_camera.m_nearPlane = snapshot.nearPlane;
	m_camera.m_farPlane = snapshot.farPlane;
	m_camera.m_isOrthographic = snapshot.isOrthographic;

	const math::matrix4x4 rotationMatrix{
		m_camera.m_right.x, m_camera.m_right.y, m_camera.m_right.z, 0.f,
		m_camera.m_up.x, m_camera.m_up.y, m_camera.m_up.z, 0.f,
		m_camera.m_forward.x, m_camera.m_forward.y, m_camera.m_forward.z, 0.f,
		0.f, 0.f, 0.f, 1.f };
	m_camera.rotate = math::normalize(
		math::quaternion_from_rotation_matrix(rotationMatrix));

	SetOrientation(std::atan2(m_camera.m_forward.x, m_camera.m_forward.z),
		-std::asin(std::clamp(m_camera.m_forward.y, -1.f, 1.f)));
}
