#pragma once
#include "../Utility_Framework/Foundation/Core.Minimal.h"
#include "Component.h"
#include "Camera.h"
#include <mathematics/bounds.hpp>

class [[reflgen::reflect, reflgen::public_methods]] CameraComponent : public meta::identity<CameraComponent, Component>
{
   friend struct reflgen::access;
   public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override
    {
        Component::gc_trace(tracer);
    }

public:
    // CT6-d: 팩토리 분기의 강제 활성(저장된 비활성도 켬 — 기존 특이 동작 보존)
    [[reflgen::script_internal]]
    void OnDeserialized() { SetEnabled(true); }

   CameraComponent() 
   {
   } 
   virtual ~CameraComponent() = default;

	// 등록/해지는 씬 편입/이탈 훅으로 한다. CameraSystem은 소유하지 않고
	// 현재 씬의 후보만 추적한다. DDOL 이송도 같은 두 훅을 통과한다.
	[[reflgen::script_internal]]
	void OnAddedToScene() override;
	[[reflgen::script_internal]]
	void OnRemovingFromScene() override;

	Camera* GetCamera() noexcept { return &m_Camera; }
	const Camera* GetCamera() const noexcept { return &m_Camera; }

	bool IsPrimary() const noexcept { return m_isPrimary; }
	void SetPrimary(bool primary) noexcept { m_isPrimary = primary; }

    // Pair a discontinuous owner Transform change with this call before the
    // next frame is captured. Identity replacement remains a separate reset.
    void NotifyCameraCut() noexcept { m_Camera.NotifyCameraCut(); }

	FrameCameraSnapshot CaptureFrameSnapshot(float aspectRatio = 0.f) const
	{
		return ResolveCamera(true).CaptureFrameSnapshot(aspectRatio);
	}

	std::optional<math::bounding_frustum> TryGetFrustum(
		float aspectRatio = 0.f) const
	{
		return ResolveCamera().TryGetFrustum(aspectRatio);
	}

    // Render consumers use the presentation frustum; gameplay/AI retains the
    // existing TryGetFrustum simulation pose and cannot read interpolation back.
    std::optional<math::bounding_frustum> TryGetRenderFrustum(float aspectRatio = 0.f) const
    {
        return ResolveCamera(true).TryGetFrustum(aspectRatio);
    }

	float CalculateLODDistance(math::vector3 worldPosition) const
	{
		return ResolveCamera(true).CalculateLODDistance(worldPosition);
	}

	math::aabb GetEditorBoundingBox() const
	{
		const auto& position = m_pOwner->Transform_().GetPositionValue();
		return math::aabb{
			math::vector3{ position.x, position.y, position.z },
			m_editorBoundingBox.extents };
	}

private:
	Camera ResolveCamera(bool presentation = false) const
	{
		Camera resolved = m_Camera;
		if (nullptr == m_pOwner) return resolved;

		// Select pose at the consumer boundary; never feed render alpha into AI.
		resolved.m_eyePosition = presentation ? m_pOwner->Transform_().GetRenderWorldPosition()
            : m_pOwner->Transform_().GetWorldPosition();
		const math::quaternion rotation = math::normalize(
			presentation ? m_pOwner->Transform_().GetRenderWorldQuaternion()
            : m_pOwner->Transform_().GetWorldQuaternion());
		resolved.m_forward = math::normalize(math::rotate(Camera::kForward, rotation));
		resolved.m_up = math::normalize(math::rotate(Camera::kUp, rotation));
		resolved.m_right = math::normalize(math::rotate(Camera::kRight, rotation));
		resolved.rotate = rotation;
		return resolved;
	}

	Camera m_Camera{};

	// Editor picking용 2x2x2 unit box. 중심은 매 호출마다 owner position이다.
	[[reflgen::ignore]]
	math::aabb m_editorBoundingBox{
		math::vector3{}, math::vector3{ 1.0f, 1.0f, 1.0f } };

	bool m_isPrimary{ false };
};
