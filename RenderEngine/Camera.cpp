#include "Camera.h"

Mathf::xMatrix Camera::CalculateView() const
{
	return XMMatrixLookAtRH(m_eyePosition, m_lookAt, m_up);
}

void Camera::HandleMovement(float deltaTime)
{
	float x = 0.f, y = 0.f, z = 0.f;

	//Input 작성하기 전까지는 대기
}

Mathf::xMatrix PerspacetiveCamera::CalculateProjection()
{
	return XMMatrixPerspectiveFovRH(XMConvertToRadians(m_fov), m_aspectRatio, 0.1f, 200.f);
}

Mathf::xMatrix OrthographicCamera::CalculateProjection()
{
	return XMMatrixOrthographicRH(m_viewWidth, m_viewHeight, m_nearPlane, m_farPlane);
}
