#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "Camera.h"
#include "MathematicsInterop.h"

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

	DirectX::BoundingFrustum GetFrustum(float aspectRatio = 0.f) const
	{
		return ResolveCamera().GetFrustum(aspectRatio);
	}

	DirectX::BoundingBox GetEditorBoundingBox() const
	{
		DirectX::BoundingBox box;
		const auto& position = m_pOwner->Transform_().position;
		box.Center = { position.x, position.y, position.z };
		box.Extents = m_editorBoundingBox.Extents;
		return box;
	}

private:
	Camera ResolveCamera() const
	{
		Camera resolved = m_Camera;
		if (nullptr == m_pOwner) return resolved;

		resolved.m_eyePosition = MathematicsInterop::ToDirectXPoint(
			m_pOwner->Transform_().GetWorldPosition());
		DirectX::XMVECTOR rotation = DirectX::XMQuaternionNormalize(
			MathematicsInterop::ToDirectX(
				m_pOwner->Transform_().GetWorldQuaternion()));
		resolved.m_forward = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(Camera::FORWARD, rotation));
		resolved.m_up = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(Camera::UP, rotation));
		resolved.m_right = DirectX::XMVector3Normalize(DirectX::XMVector3Rotate(Camera::RIGHT, rotation));
		resolved.m_lookAt = DirectX::XMVectorAdd(resolved.m_eyePosition, resolved.m_forward);
		return resolved;
	}

	Camera m_Camera{};
	DirectX::BoundingBox m_editorBoundingBox{ { 0, 0, 0 }, { 1, 1, 1 } };
	bool m_isPrimary{ false };
};
