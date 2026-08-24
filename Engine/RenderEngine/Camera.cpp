#include "Camera.h"
#include "RHI/ScreenSizedResource.h"

namespace
{
	float ResolveAspectRatio(float requested)
	{
		if (requested > 0.f) return requested;
		const float current = ScreenResizeBus::Get().GetAspectRatio();
		return current > 0.f ? current : 1.f;
	}
}

Mathf::xMatrix Camera::CalculateProjection() const
{
	return CalculateProjectionForAspect(ResolveAspectRatio(0.f));
}

Mathf::xMatrix Camera::CalculateProjectionForAspect(float aspectRatio) const
{
	const float nearPlane = m_nearPlane > 0.f ? m_nearPlane : 0.1f;
	const float farPlane = m_farPlane > nearPlane ? m_farPlane : nearPlane + 0.4f;
	if (m_isOrthographic)
	{
		return XMMatrixOrthographicLH(m_viewWidth, m_viewHeight, nearPlane, farPlane);
	}
	return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov),
		ResolveAspectRatio(aspectRatio), nearPlane, farPlane);
}

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
	Mathf::Vector4 _far = ConvertScreenToWorld(screenPosition, 1.f);
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

Mathf::xMatrix Camera::CalculateInverseProjection() const
{
	return XMMatrixInverse(nullptr, CalculateProjection());
}

Core::Sizef Camera::GetScreenSize() const
{
	Core::Sizef size;
	size = { static_cast<float>(ScreenResizeBus::Get().GetWidth()), static_cast<float>(ScreenResizeBus::Get().GetHeight()) };
	return size;
}

FrameCameraSnapshot Camera::CaptureFrameSnapshot(float aspectRatio) const
{
	FrameCameraSnapshot snapshot{};
	snapshot.view = CalculateView();
	snapshot.projection = CalculateProjectionForAspect(
		ResolveAspectRatio(aspectRatio));
	snapshot.inverseView = XMMatrixInverse(nullptr, snapshot.view);
	snapshot.inverseProjection = XMMatrixInverse(nullptr, snapshot.projection);
	snapshot.eyePosition = m_eyePosition;
	snapshot.forward = m_forward;
	snapshot.right = m_right;
	snapshot.up = m_up;
	snapshot.fov = m_fov;
	snapshot.nearPlane = m_nearPlane > 0.f ? m_nearPlane : 0.1f;
	snapshot.farPlane = m_farPlane > snapshot.nearPlane
		? m_farPlane : snapshot.nearPlane + 0.4f;
	snapshot.isOrthographic = m_isOrthographic;
	return snapshot;
}

DirectX::BoundingFrustum Camera::GetFrustum(float aspectRatio) const
{
	const FrameCameraSnapshot snapshot = CaptureFrameSnapshot(aspectRatio);
	DirectX::BoundingFrustum frustum;
	BoundingFrustum::CreateFromMatrix(frustum, snapshot.projection);
	frustum.Transform(frustum, snapshot.inverseView);

	return frustum;
}

// ★ HandleMovement 74줄이 여기 있었다 (PHASE 4-3 슬라이스 4).
//
//   InputManager를 11번, ImGui를 7번 부르던 함수이고 — Camera.cpp가
//   게임플레이 헤더를 여는 유일한 이유였다. 그런데 부르는 쪽은 에디터
//   씬 뷰 하나뿐이고, 대상은 언제나 EnhancedSceneRenderer의 editorCamera다.
//   씬에 저장되는 게임 카메라는 이 경로를 한 번도 타지 않았다.
//
//   조작 상태 넷(m_speed·m_speedMul·deltaPitch·deltaYaw)도 함께 갔다.
//   EngineGUIWindow/EditorCameraRig가 지금 그것을 든다.

void Camera::MoveToTarget(Mathf::Vector3 targetPosition)
{
	m_eyePosition = targetPosition;
	m_lookAt = m_eyePosition + m_forward;
}
