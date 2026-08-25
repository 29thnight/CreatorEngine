#include "EditorCameraRig.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "MathematicsInterop.h"

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

		const DirectX::XMVECTOR qYaw = DirectX::XMQuaternionRotationAxis(Camera::UP, m_deltaYaw);
		const DirectX::XMVECTOR right = DirectX::XMVector3Rotate(Camera::RIGHT, qYaw);
		const DirectX::XMVECTOR qPitch = DirectX::XMQuaternionRotationAxis(right, m_deltaPitch);
		const DirectX::XMVECTOR cameraRotation = DirectX::XMQuaternionNormalize(
			DirectX::XMQuaternionMultiply(qYaw, qPitch));

		m_camera.m_forward = DirectX::XMVector3Normalize(
			DirectX::XMVector3Rotate(Camera::FORWARD, cameraRotation));
		m_camera.m_up = DirectX::XMVector3Normalize(
			DirectX::XMVector3Rotate(Camera::UP, cameraRotation));
		m_camera.m_right = DirectX::XMVector3Normalize(
			DirectX::XMVector3Cross(m_camera.m_up, m_camera.m_forward));
		m_camera.rotate = cameraRotation;
		m_camera.m_rotation = cameraRotation;
	}

	const DirectX::XMVECTOR movement = DirectX::XMVectorScale(
		DirectX::XMVectorAdd(
			DirectX::XMVectorAdd(
				DirectX::XMVectorScale(m_camera.m_forward, z),
				DirectX::XMVectorScale(m_camera.m_up, y)),
			DirectX::XMVectorScale(m_camera.m_right, x)),
		m_speed * deltaTime);
	m_camera.m_eyePosition = DirectX::XMVectorAdd(m_camera.m_eyePosition, movement);
	m_camera.m_lookAt = DirectX::XMVectorAdd(m_camera.m_eyePosition, m_camera.m_forward);
}

void EditorCameraRig::ApplySnapshot(const FrameCameraSnapshot& snapshot) noexcept
{
	m_camera.m_eyePosition = MathematicsInterop::ToDirectXPoint(snapshot.eyePosition);
	m_camera.m_forward = DirectX::XMVector3Normalize(
		MathematicsInterop::ToDirectXDirection(snapshot.forward));
	m_camera.m_up = DirectX::XMVector3Normalize(
		MathematicsInterop::ToDirectXDirection(snapshot.up));
	m_camera.m_right = DirectX::XMVector3Normalize(
		MathematicsInterop::ToDirectXDirection(snapshot.right));
	m_camera.m_lookAt = DirectX::XMVectorAdd(m_camera.m_eyePosition, m_camera.m_forward);
	m_camera.m_fov = snapshot.fov;
	m_camera.m_nearPlane = snapshot.nearPlane;
	m_camera.m_farPlane = snapshot.farPlane;
	m_camera.m_isOrthographic = snapshot.isOrthographic;

	const DirectX::XMMATRIX rotationMatrix(
		m_camera.m_right, m_camera.m_up, m_camera.m_forward,
		DirectX::XMVectorSet(0.f, 0.f, 0.f, 1.f));
	const DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(
		DirectX::XMQuaternionRotationMatrix(rotationMatrix));
	m_camera.rotate = rotation;
	m_camera.m_rotation = rotation;

	Mathf::Vector3 forward{};
	DirectX::XMStoreFloat3(&forward, m_camera.m_forward);
	SetOrientation(std::atan2(forward.x, forward.z),
		-std::asin(std::clamp(forward.y, -1.f, 1.f)));
}
