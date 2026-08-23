#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "Camera.h"
#include "SceneManager.h"
#include "CameraSystem.h"

class CameraComponent : public meta::identity<CameraComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_Camera>,
           meta::field<&Self::m_cameraIndex>);
   }
public:
    // CT6-d: 팩토리 분기의 강제 활성(저장된 비활성도 켬 — 기존 특이 동작 보존)
    void OnDeserialized() { SetEnabled(true); }

   CameraComponent() 
   {
   } 
   virtual ~CameraComponent() = default;

	void OnInitialized() override
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

	// 트랙 렌더: 가상 Update 오버라이드를 걷어내고 CameraSystem(조밀 벡터,
	// 전용 틱)으로 옮겼다 — 등록/해지는 씬 편입/이탈 훅으로 한다(DDOL 안전,
	// 근거는 AnimatorSystem.h 상단 주석 참고). 옛 Update 본문(카메라 eye/
	// forward/up/right 갱신)은 CameraSystem::Update로 그대로 옮겨 갔다 —
	// 전용 진입점을 새로 만들지 않고 아래 GetCamera()를 그 시스템이 직접
	// 쓴다(왜 그런지, HasTransform() 방어를 안 넣은 이유는 CameraSystem.h
	// 상단 주석 참고).
	void OnAddedToScene() override
	{
		CameraSystems->Register(this);
	}

	void OnRemovingFromScene() override
	{
		CameraSystems->Unregister(this);
	}

	void OnUninitializing() override
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
		box.Center = Mathf::Vector3(m_pOwner->Transform_().position);
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
