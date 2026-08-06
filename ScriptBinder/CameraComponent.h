#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IRegistableEvent.h"
#include "Camera.h"
#include "CameraComponent.generated.h"
#include "SceneManager.h"

class CameraComponent : public Component, public RegistableEvent<CameraComponent>
{
public:
   ReflectCameraComponent
	[[Serializable(Inheritance:Component)]]
   CameraComponent() 
   {
	   m_name = "CameraComponent"; 
	   m_typeID = type_guid(CameraComponent);
   } 
   virtual ~CameraComponent() = default;

	void Awake() override
	{
		//�� �κ��� ���ϴ� ���� �����ؼ� �ٽ� �ۼ�
/*		if (m_cameraIndex != -1)
		{
			m_pCamera = CameraManagement->GetCamera(1);
			if (m_pCamera == nullptr)
			{
				m_pCamera = std::make_shared<Camera>();
				m_pCamera->RegisterContainer();
				m_cameraIndex = m_pCamera->m_cameraIndex;
			}
		}
		else
		{
			if (m_pCamera == nullptr)
			{
				m_pCamera = std::make_shared<Camera>();
				m_pCamera->RegisterContainer();
			}
			m_cameraIndex = m_pCamera->m_cameraIndex;
		}*/		//���� ������ ���� �ּ�
		auto camera = CameraManagement->GetCamera(1);

		if(m_pCamera != camera)
		{
			m_pCamera.swap(camera); //�ӽ÷� 1�� ī�޶� ����
			m_Camera = m_pCamera.get();
		}

		if (m_pCamera == nullptr)
		{
			m_pCamera = std::make_shared<Camera>();
			m_Camera = m_pCamera.get();
			m_pCamera->RegisterContainer();
		}
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
	[[Property]]
	Camera* m_Camera{ nullptr };
	BoundingBox m_editorBoundingBox{ { 0, 0, 0 }, { 1, 1, 1 } };
	[[Property]]
	int m_cameraIndex{ -1 };
	bool m_IsEnabled{ false };
};
