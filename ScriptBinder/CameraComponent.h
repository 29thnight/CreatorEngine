#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Camera.h"
#include "SceneManager.h"

class CameraComponent : public meta::identity<CameraComponent, Component>
{
public:
    // CT6-d: 팩토리 분기의 강제 활성(저장된 비활성도 켬 — 기존 특이 동작 보존)
    void OnDeserialized() { SetEnabled(true); }

   static consteval auto describe()
   {
       return meta::describe<CameraComponent>(
           meta::base<Component>(),
           meta::member<&CameraComponent::m_Camera>(),
           meta::member<&CameraComponent::m_cameraIndex>());
   }
   CameraComponent() 
   {
   } 
   virtual ~CameraComponent() = default;

	void Awake() override
	{
		// 컴포넌트마다 자기 카메라를 만들어 등록한다 (RenderSceneViewPlan ④).
		//
		// ★ 예전에는 모든 CameraComponent가 GetCamera(1)을 잡았다 — 슬롯 1이
		//   비어 있으면 만들고, 차 있으면 "그 카메라를 공유"했다. 게임 카메라가
		//   둘이면 한 Camera 객체를 두 컴포넌트가 번갈아 덮어썼고(다중 카메라
		//   원천 불가), 슬롯 1에 무엇이 있는지에 따라 동작이 갈렸다.
		//   재생 재진입(재-Awake)에서는 이미 가진 카메라를 그대로 쓴다.
		if (nullptr == m_pCamera)
		{
			m_pCamera = std::make_shared<Camera>();
			m_pCamera->RegisterContainer();
		}
		m_Camera = m_pCamera.get();
		m_cameraIndex = m_pCamera->m_cameraIndex;
	}

	void Update(float deltaSeconds) override
	{
		if (m_pCamera)
		{
			m_pCamera->m_eyePosition = m_pOwner->m_transform.GetWorldPosition();
			XMVECTOR rotationQuat = m_pOwner->m_transform.GetWorldQuaternion();
			rotationQuat = XMQuaternionNormalize(rotationQuat);

			static const XMVECTOR FORWARD = XMVectorSet(0, 0, 1, 0);
			static const XMVECTOR UP = XMVectorSet(0, 1, 0, 0);
			static const XMVECTOR RIGHT = XMVectorSet(1, 0, 0, 0);

			m_pCamera->m_forward = XMVector3Normalize(XMVector3Rotate(FORWARD, rotationQuat));
			m_pCamera->m_up = XMVector3Normalize(XMVector3Rotate(UP, rotationQuat));
			m_pCamera->m_right = XMVector3Normalize(XMVector3Rotate(RIGHT, rotationQuat));
			m_pCamera->m_lookAt = m_pCamera->m_eyePosition + m_pCamera->m_forward;
		}
	}

	void OnDestroy() override
	{
		// 재생 중에는 카메라를 놓지 않는다.
		//
		// 예전에는 활성 씬 이름이 "PlayScene"인지로 판정했다. 재생 시작이 그
		// 이름의 사본 씬을 만들던 시절의 조건인데, 지금은 씬을 복제하지 않고
		// 같은 씬으로 플레이하므로 이름이 바뀌지 않는다 — 그대로 두면 재생 중
		// 파괴에서도 카메라를 놓아 화면이 끊긴다.
		if (!SceneManagers->IsGameStart())
		{
			m_pCamera = nullptr;
			m_cameraIndex = -1;
		}
	}

	Camera* GetCamera() const
	{
		return m_pCamera.get();
	}

	DirectX::BoundingFrustum GetFrustum() const
	{
		DirectX::BoundingFrustum frustum = m_pCamera->GetFrustum();

		return frustum;
	}

	DirectX::BoundingBox GetEditorBoundingBox() const
	{
		DirectX::BoundingBox box;
		box.Center = Mathf::Vector3(m_pOwner->m_transform.position);
		box.Extents = m_editorBoundingBox.Extents;
		return box;
	}

private:
	std::shared_ptr<Camera> m_pCamera{ nullptr };
	Camera* m_Camera{ nullptr };
	BoundingBox m_editorBoundingBox{ { 0, 0, 0 }, { 1, 1, 1 } };
	int m_cameraIndex{ -1 };
	bool m_IsEnabled{ false };
};
