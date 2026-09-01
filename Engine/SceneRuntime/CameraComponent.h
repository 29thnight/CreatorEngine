#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "Camera.h"
#include <mathematics/bounds.hpp>

class CameraComponent : public meta::identity<CameraComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_Camera>,
           meta::field<&Self::m_isPrimary>);
   }
public:
    // CT6-d: 팩토리 분기의 강제 활성(저장된 비활성도 켬 — 기존 특이 동작 보존)
    void OnDeserialized() { SetEnabled(true); }

   CameraComponent() 
   {
   } 
   virtual ~CameraComponent() = default;

	// 등록/해지는 씬 편입/이탈 훅으로 한다. CameraSystem은 소유하지 않고
	// 현재 씬의 후보만 추적한다. DDOL 이송도 같은 두 훅을 통과한다.
	void OnAddedToScene() override;
	void OnRemovingFromScene() override;

	Camera* GetCamera() noexcept { return &m_Camera; }
	const Camera* GetCamera() const noexcept { return &m_Camera; }

	bool IsPrimary() const noexcept { return m_isPrimary; }
	void SetPrimary(bool primary) noexcept { m_isPrimary = primary; }

	FrameCameraSnapshot CaptureFrameSnapshot(float aspectRatio = 0.f) const
	{
		return ResolveCamera().CaptureFrameSnapshot(aspectRatio);
	}

	std::optional<math::bounding_frustum> TryGetFrustum(
		float aspectRatio = 0.f) const
	{
		return ResolveCamera().TryGetFrustum(aspectRatio);
	}

	math::aabb GetEditorBoundingBox() const
	{
		const auto& position = m_pOwner->Transform_().GetPositionValue();
		return math::aabb{
			math::vector3{ position.x, position.y, position.z },
			m_editorBoundingBox.extents };
	}

private:
	Camera ResolveCamera() const
	{
		Camera resolved = m_Camera;
		if (nullptr == m_pOwner) return resolved;

		resolved.m_eyePosition = m_pOwner->Transform_().GetWorldPosition();
		const math::quaternion rotation = math::normalize(
			m_pOwner->Transform_().GetWorldQuaternion());
		resolved.m_forward = math::normalize(math::rotate(Camera::FORWARD, rotation));
		resolved.m_up = math::normalize(math::rotate(Camera::UP, rotation));
		resolved.m_right = math::normalize(math::rotate(Camera::RIGHT, rotation));
		resolved.rotate = rotation;
		return resolved;
	}

	Camera m_Camera{};
	// Editor picking용 2x2x2 unit box. 중심은 매 호출마다 owner position이다.
	math::aabb m_editorBoundingBox{
		math::vector3{}, math::vector3{ 1.0f, 1.0f, 1.0f } };
	bool m_isPrimary{ false };
};
