#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "ProjectionType.h"
#include "ClassProperty.h"
// Sizef를 쓴다. 예전에는 DeviceResources.h가 전이로 공급했는데 그 파일이
// 사라졌다(2026-08-10) — 정의가 있는 곳에서 직접 든다.
#include "TypeDefinition.h"
#include "FrameCameraSnapshot.h"
#include <mathematics/frustum.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/vector2.hpp>
#include <optional>
#include <type_traits>
// ID3D11DeviceContext·ID3D11Buffer 전방 선언이 여기 있었다 (E, 2026-08-09).
// 뷰/투영/캐스케이드 상수 버퍼와 UpdateBuffer 삼형제가 D4에서 사라졌다.
// 카메라 한 대의 값 상태. 소유권·활성 뷰 선택·렌더 슬롯 배정은 이 타입의
// 책임이 아니다. SceneRuntime의 CameraComponent와 Editor의 camera rig가 각각
// 값을 소유하고 RenderCore에는 FrameCameraSnapshot만 전달한다.
class [[reflgen::reflect]] Camera
{
   public:
public:
	Camera() = default;
	~Camera() = default;

	math::matrix4x4 CalculateProjection() const;
	math::matrix4x4 CalculateProjectionForAspect(float aspectRatio) const;
	math::vector3 ConvertScreenToWorld(math::vector2 screenPosition, float depth) const;
	math::vector3 RayCast(math::vector2 screenPosition) const;
	math::matrix4x4 CalculateView() const;
	math::matrix4x4 CalculateInverseView() const;
	math::matrix4x4 CalculateInverseProjection() const;
	float CalculateLODDistance(math::vector3 worldPosition) const noexcept;
	Core::Sizef GetScreenSize() const;
	std::optional<math::bounding_frustum> TryGetFrustum(
		float aspectRatio = 0.f) const;
	FrameCameraSnapshot CaptureFrameSnapshot(float aspectRatio = 0.f) const;

	void MoveToTarget(math::vector3 targetPosition);

    // Call after a teleport/cut, before sealing the next render frame. Normal
    // Transform movement does not imply a cut; lens changes are detected from
    // the submitted unjittered projection by the temporal history consumer.
    void NotifyCameraCut() noexcept { ++m_cameraCutRevision; }

	math::quaternion rotate{};

	static constexpr math::vector3 kForward = math::vector3::unit_z();
	static constexpr math::vector3 kRight = math::vector3::unit_x();
	static constexpr math::vector3 kUp = math::vector3::unit_y();

	[[reflgen::ignore]]
	math::vector3 m_eyePosition{ 0.f, 1.f, -10.f };

	[[reflgen::ignore]]
	math::vector3 m_forward{ kForward };

	[[reflgen::ignore]]
	math::vector3 m_right{ kRight };

	[[reflgen::ignore]]
	math::vector3 m_up{ kUp };

	float m_nearPlane{ 0.1f };
	float m_farPlane{ 500.f };
	float m_fov{ 60.f };

	[[reflgen::ignore]]
	float m_viewWidth{ 1.f };

	[[reflgen::ignore]]
	float m_viewHeight{ 1.f };

	[[reflgen::ignore]]
	bool m_isOrthographic{ false };

private:
    [[reflgen::ignore]]
    std::uint64_t m_cameraCutRevision{ 0 };
};

static_assert(std::is_same_v<decltype(Camera::rotate), math::quaternion>);
static_assert(std::is_same_v<decltype(Camera::m_eyePosition), math::vector3>);
