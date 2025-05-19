#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IAwakable.h"
#include "IOnDistroy.h"
#include "IUpdatable.h"
#include "Camera.h"
#include "CameraComponent.generated.h"

class CameraComponent : public Component, public IAwakable, public IUpdatable, public IOnDistroy
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
		//이 부분은 원하는 로직 정리해서 다시 작성
		if (m_cameraIndex != -1)
		{
			m_pCamera = CameraManagement->GetCamera(m_cameraIndex);
			if (m_pCamera == nullptr)
			{
				m_pCamera = new Camera();
				m_pCamera->RegisterContainer();
				m_cameraIndex = m_pCamera->m_cameraIndex;
			}
		}
		else
		{
			m_pCamera = new Camera();
			m_pCamera->RegisterContainer();
			m_cameraIndex = m_pCamera->m_cameraIndex;
		}		
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

	void OnDistroy() override
	{
		delete m_pCamera;
		m_cameraIndex = -1;
		m_pCamera = nullptr;
	}

	Camera* GetCamera() const
	{
		return m_pCamera;
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
	[[Property]]
	Camera* m_pCamera{ nullptr };
	[[Property]]
	int m_cameraIndex{ -1 };
	BoundingBox m_editorBoundingBox{ { 0, 0, 0 }, { 1, 1, 1 } };
	bool m_IsEnabled{ false };
};
