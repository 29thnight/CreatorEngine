#include "Camera.h"
#include "../InputManager.h"
#include "DeviceState.h"
#include "ImGuiRegister.h"

const static float pi = XM_PIDIV2 - 0.01f;
const static float pi2 = XM_PI * 2.f;

Mathf::Vector4 Camera::ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth)
{
	// 1. 스크린 좌표를 NDC 좌표로 변환
	float x_ndc = (2.0f * screenPosition.x / GetScreenSize().width) - 1.0f;
	float y_ndc = 1.0f - (2.0f * screenPosition.y / GetScreenSize().height);
	Mathf::Vector4 screenPositionNDC = { x_ndc, y_ndc, depth, 1.0f };

	// 2. 역행렬 투영 변환 (Projection^-1)
	Mathf::xMatrix invProj = CalculateInverseProjection();
	Mathf::Vector4 viewPosition = XMVector3TransformCoord(screenPositionNDC, invProj);

	// 3. 뷰 역행렬 변환 (View^-1)
	Mathf::xMatrix invView = CalculateInverseView();
	Mathf::Vector4 worldPosition = XMVector3TransformCoord(viewPosition, invView);

	return worldPosition;
}

Mathf::Vector4 Camera::RayCast(Mathf::Vector2 screenPosition)
{
	Mathf::Vector4 _near = ConvertScreenToWorld(screenPosition, 0.f);
	std::cout << "Near" << _near.x << _near.y << _near.z << _near.w << std::endl;
	Mathf::Vector4 _far = ConvertScreenToWorld(screenPosition, 1.f);
	std::cout << "Far" << _far.x << _far.y << _far.z << _far.w << std::endl;
	Mathf::Vector4 direction = _far - _near;
	direction = XMVector3Normalize(direction);
	return direction;
}

Mathf::xMatrix Camera::CalculateView() const
{
	return XMMatrixLookAtLH(m_eyePosition, m_lookAt, m_up);
}

Mathf::xMatrix Camera::CalculateInverseView() const
{
	return XMMatrixInverse(nullptr, CalculateView());
}

Mathf::xMatrix Camera::CalculateInverseProjection()
{
	return XMMatrixInverse(nullptr, CalculateProjection());
}

DirectX11::Sizef Camera::GetScreenSize() const
{
	return DeviceState::g_ClientRect;
}

DirectX::BoundingFrustum Camera::GetFrustum()
{
	DirectX::BoundingFrustum frustum;
	frustum.CreateFromMatrix(frustum, CalculateProjection());
	frustum.Transform(frustum, CalculateView());
	return frustum;
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

	XMVECTOR m_rotationQuat = XMQuaternionIdentity();
	static XMVECTOR rotate = XMQuaternionIdentity();
	//Change the Camera Rotaition Quaternion Not Use XMQuaternionRotationRollPitchYaw
	if (InputManagement->IsMouseButtonDown(MouseKey::MIDDLE))
	{


		// 마우스 이동량 가져오기
		float deltaPitch = InputManagement->GetMouseDelta().y * 0.01f;
		float deltaYaw = InputManagement->GetMouseDelta().x * 0.01f;

		// Pitch 제한 적용
		//m_pitch += deltaPitch;
		
		// 현재 회전 기준 축을 얻음
		XMVECTOR rightAxis = XMVector3Normalize(XMVector3Cross(m_up, m_forward));

		// 프레임당 변화량만 적용
		XMVECTOR pitchQuat = XMQuaternionRotationAxis(rightAxis, deltaPitch);
		XMVECTOR yawQuat = XMQuaternionRotationAxis(m_up, deltaYaw);

		// Yaw를 먼저 적용 -> Pitch를 적용
		XMVECTOR deltaRotation = XMQuaternionMultiply(yawQuat, pitchQuat);
		m_rotationQuat = XMQuaternionMultiply(deltaRotation, m_rotationQuat);
		m_rotationQuat = XMQuaternionMultiply(rotate, m_rotationQuat);
		m_rotationQuat = XMQuaternionNormalize(m_rotationQuat);


		// 새로운 방향 벡터 계산
		m_forward = XMVector3Normalize(XMVector3Rotate(FORWARD, m_rotationQuat));

		// Right 벡터 업데이트 (UP을 기준으로 다시 계산)
		m_right = XMVector3Normalize(XMVector3Cross(m_up, m_forward));

		m_up = XMVector3Cross(m_forward, m_right);

			float sign = XMVectorGetY(m_up) > 0 ? 1.0f : -1.0f;
			m_up = XMVectorSet(XMVectorGetX(m_up), sign * 20.f, XMVectorGetZ(m_up), 0);
			m_up = XMQuaternionNormalize(m_up);


		std::cout << XMVectorGetX(rightAxis) << ", " << XMVectorGetY(rightAxis) << ", " << XMVectorGetZ(rightAxis) << std::endl;

		rotate = m_rotationQuat;
	}

	if (InputManagement->IsMouseButtonDown(MouseKey::LEFT))
	{
		m_rayDirection = RayCast(InputManagement->GetMousePos());
		//std::cout << "MousePos" << InputManagement->GetMousePos().x << InputManagement->GetMousePos().y << std::endl;
		//std::cout << "RayCast" << m_rayDirection.x << m_rayDirection.y << m_rayDirection.z << m_rayDirection.w << std::endl;
	}

	m_eyePosition += ((z * m_forward) + (y * m_up) + (x * m_right)) * m_speed * deltaTime;
	m_lookAt = m_eyePosition + m_forward;
}

PerspacetiveCamera::PerspacetiveCamera()
{
	ImGui::ContextRegister("Perspacetive Camera", [&]()
	{
		ImGui::DragFloat("FOV", &m_fov, 1.f, 1.f, 179.f);
		ImGui::DragFloat("Aspect Ratio", &m_aspectRatio, 0.1f, 0.1f, 10.f);
		ImGui::DragFloat("Near Plane", &m_nearPlane, 0.1f, 0.1f, 100.f);
		ImGui::DragFloat("Far Plane", &m_farPlane, 1.f, 1.f, 100000.f);
		ImGui::DragFloat("Speed", &m_speed, 1.f, 1.f, 100.f);
		ImGui::DragFloat("Pitch", &m_pitch, 0.01f, -pi, pi);
		ImGui::DragFloat("Yaw", &m_yaw, 0.01f, -pi, pi);
		ImGui::DragFloat("Roll", &m_roll, 0.01f, -pi, pi);

		ImGui::Text("Eye Position");
		ImGui::DragFloat3("##Eye Position", &m_eyePosition.m128_f32[0], -1000, 1000);
		ImGui::Text("Forward");
		ImGui::DragFloat3("##Forward", &m_forward.m128_f32[0], -1000, 1000);
		ImGui::Text("Right");
		ImGui::DragFloat3("##Right", &m_right.m128_f32[0], -1000, 1000);
	});
}

Mathf::xMatrix PerspacetiveCamera::CalculateProjection()
{
	return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
}

OrthographicCamera::OrthographicCamera()
{
}

Mathf::xMatrix OrthographicCamera::CalculateProjection()
{
	return XMMatrixOrthographicLH(m_viewWidth, m_viewHeight, m_nearPlane, m_farPlane);
}
