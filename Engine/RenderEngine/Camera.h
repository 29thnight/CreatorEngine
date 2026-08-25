#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "ProjectionType.h"
#include "ClassProperty.h"
// Sizef를 쓴다. 예전에는 DeviceResources.h가 전이로 공급했는데 그 파일이
// 사라졌다(2026-08-10) — 정의가 있는 곳에서 직접 든다.
#include "TypeDefinition.h"
#include "FrameCameraSnapshot.h"
// ID3D11DeviceContext·ID3D11Buffer 전방 선언이 여기 있었다 (E, 2026-08-09).
// 뷰/투영/캐스케이드 상수 버퍼와 UpdateBuffer 삼형제가 D4에서 사라졌다.
// 카메라 한 대의 값 상태. 소유권·활성 뷰 선택·렌더 슬롯 배정은 이 타입의
// 책임이 아니다. SceneRuntime의 CameraComponent와 Editor의 camera rig가 각각
// 값을 소유하고 RenderCore에는 FrameCameraSnapshot만 전달한다.
class Camera
{
   public:
   static consteval auto reflect()
   {
       using Self = Camera;
       return meta::schema<Self>(
           meta::field<&Self::rotate>,
           meta::field<&Self::m_nearPlane>,
           meta::field<&Self::m_farPlane>,
           meta::field<&Self::m_fov>);
   }
public:
	Camera() = default;
	~Camera() = default;

	Mathf::xMatrix CalculateProjection() const;
	Mathf::xMatrix CalculateProjectionForAspect(float aspectRatio) const;
	Mathf::Vector4 ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth);
	Mathf::Vector4 RayCast(Mathf::Vector2 screenPosition);
	Mathf::xMatrix CalculateView() const;
	Mathf::xMatrix CalculateInverseView() const;
	Mathf::xMatrix CalculateInverseProjection() const;
	Core::Sizef GetScreenSize() const;
	DirectX::BoundingFrustum GetFrustum(float aspectRatio = 0.f) const;
	FrameCameraSnapshot CaptureFrameSnapshot(float aspectRatio = 0.f) const;

	void MoveToTarget(Mathf::Vector3 targetPosition);

	Mathf::Quaternion rotate{ DirectX::XMQuaternionIdentity() };

	static constexpr Mathf::xVector FORWARD = { 0.f, 0.f, 1.f };
	static constexpr Mathf::xVector RIGHT = { 1.f, 0.f, 0.f };
	static constexpr Mathf::xVector UP = { 0.f, 1.f, 0.f };

	Mathf::xVector m_eyePosition{ DirectX::XMVectorSet(0, 1, -10, 1) };
	Mathf::xVector m_forward{ FORWARD };
	Mathf::xVector m_right{ RIGHT };
	Mathf::xVector m_up{ UP };
	Mathf::xVector m_lookAt{ DirectX::XMVectorAdd(m_eyePosition, m_forward) };
	Mathf::xVector m_rotation{ 0.f, 0.f, 0.f, 1.f };

	float m_nearPlane{ 0.1f };
	float m_farPlane{ 500.f };
	float m_fov{ 60.f };

	float m_viewWidth{ 1.f };
	float m_viewHeight{ 1.f };

	bool m_isOrthographic{ false };

private:
	math::matrix4x4 CalculateProjectionMathForAspect(float aspectRatio) const;
	math::matrix4x4 CalculateViewMath() const;
};
