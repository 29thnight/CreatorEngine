#include "Camera.h"
#include "../InputManager.h"

Mathf::xMatrix Camera::CalculateView() const
{
	return XMMatrixLookAtLH(m_eyePosition, m_lookAt, m_up);
}

void Camera::HandleMovement(float deltaTime)
{
	float x = 0.f, y = 0.f, z = 0.f;

	if (InputManagement->IsKeyPressed('W'))
	{
		z += 1.f;
	}
	if (InputManagement->IsKeyPressed('S'))
	{
		z -= 1.f;
	}
	if (InputManagement->IsKeyPressed('A'))
	{
		x -= 1.f;
	}
	if (InputManagement->IsKeyPressed('D'))
	{
		x += 1.f;
	}
	if (InputManagement->IsKeyPressed('Q'))
	{
		y -= 1.f;
	}
	if (InputManagement->IsKeyPressed('E'))
	{
		y += 1.f;
	}

	if (InputManagement->IsMouseButtonDown(MouseKey::MIDDLE))
	{
		m_pitch += InputManagement->GetMouseDelta().y * 0.01f;
		m_yaw += InputManagement->GetMouseDelta().x * 0.01f;

		Mathf::xVector viewRotation = XMQuaternionRotationRollPitchYaw(m_pitch, m_yaw, 0.f);
		m_forward = XMVector3Normalize(XMVector3Rotate(FORWARD, viewRotation));
		m_right = XMVector3Normalize(XMVector3Cross(UP, m_forward));
	}

	m_eyePosition += ((z * m_forward) + (y * UP) + (x * m_right)) * m_speed * deltaTime;
	m_lookAt = m_eyePosition + m_forward;
}

Mathf::xMatrix PerspacetiveCamera::CalculateProjection()
{
	return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), m_aspectRatio, 0.1f, 200.f);
}

Mathf::xMatrix OrthographicCamera::CalculateProjection()
{
	return XMMatrixOrthographicLH(m_viewWidth, m_viewHeight, m_nearPlane, m_farPlane);
}
