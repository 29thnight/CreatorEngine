#include "Camera.h"
#include "MathematicsInterop.h"
#include "RHI/ScreenSizedResource.h"

#include <mathematics/scalar.hpp>
#include <mathematics/transform.hpp>

#include <cmath>

namespace
{
	float ResolveAspectRatio(float requested)
	{
		if (requested > 0.f) return requested;
		const float current = ScreenResizeBus::Get().GetAspectRatio();
		return current > 0.f ? current : 1.f;
	}
}

math::matrix4x4 Camera::CalculateProjection() const
{
	return CalculateProjectionForAspect(ResolveAspectRatio(0.f));
}

math::matrix4x4 Camera::CalculateProjectionForAspect(float aspectRatio) const
{
	const float nearPlane = m_nearPlane > 0.f ? m_nearPlane : 0.1f;
	const float farPlane = m_farPlane > nearPlane ? m_farPlane : nearPlane + 0.4f;
	if (m_isOrthographic)
	{
		return math::orthographic_lh(m_viewWidth, m_viewHeight, nearPlane, farPlane);
	}
	return math::perspective_fov_lh(math::radians(m_fov),
		ResolveAspectRatio(aspectRatio), nearPlane, farPlane);
}

math::vector3 Camera::ConvertScreenToWorld(math::vector2 screenPosition, float depth) const
{
	const float xNdc = (2.0f * screenPosition.x / GetScreenSize().width) - 1.0f;
	const float yNdc = 1.0f - (2.0f * screenPosition.y / GetScreenSize().height);
	const math::vector4 clipPosition{ xNdc, yNdc, depth, 1.0f };
	const math::vector4 worldPosition =
		clipPosition * math::inverse(CalculateView() * CalculateProjection());
	if (std::fabs(worldPosition.w) <= 1.0e-6f)
	{
		return math::vector3{ worldPosition.x, worldPosition.y, worldPosition.z };
	}
	const float inverseW = 1.0f / worldPosition.w;
	return math::vector3{
		worldPosition.x * inverseW,
		worldPosition.y * inverseW,
		worldPosition.z * inverseW };
}

math::vector3 Camera::RayCast(math::vector2 screenPosition) const
{
	const math::vector3 nearPoint = ConvertScreenToWorld(screenPosition, 0.f);
	const math::vector3 farPoint = ConvertScreenToWorld(screenPosition, 1.f);
	return math::normalize(farPoint - nearPoint);
}

math::matrix4x4 Camera::CalculateView() const
{
	return math::look_to_lh(m_eyePosition, m_forward, m_up);
}

math::matrix4x4 Camera::CalculateInverseView() const
{
	return math::inverse(CalculateView());
}

math::matrix4x4 Camera::CalculateInverseProjection() const
{
	return math::inverse(CalculateProjection());
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
	snapshot.inverseView = math::inverse(snapshot.view);
	snapshot.inverseProjection = math::inverse(snapshot.projection);
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
	DirectX::BoundingFrustum::CreateFromMatrix(
		frustum, MathematicsInterop::ToDirectX(snapshot.projection));
	frustum.Transform(frustum, MathematicsInterop::ToDirectX(snapshot.inverseView));

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

void Camera::MoveToTarget(math::vector3 targetPosition)
{
	m_eyePosition = targetPosition;
}
