#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"
#include "IAwakable.h"
#include "IOnDisable.h"
#include "IUpdatable.h"
#include "Camera.h"
#include "CameraComponent.generated.h"

class CameraComponent : public Component, public IRenderable, public IAwakable, public IUpdatable, public IOnDisable
{
public:
   ReflectCameraComponent
	[[Serializable(Inheritance:Component)]]
   CameraComponent() 
   {
	   m_name = "CameraComponent"; 
	   m_typeID = TypeTrait::GUIDCreator::GetTypeID<CameraComponent>();
   } 
   virtual ~CameraComponent() = default;

	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
		if (m_pCamera)
		{
			m_pCamera->m_isActive = m_IsEnabled;
		}
	}

	void Awake() override
	{
		//이 부분은 원하는 로직 정리해서 다시 작성
		if (m_cameraIndex != -1)
		{
			m_pCamera = CameraManagement->GetCamera(m_cameraIndex);
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
			m_pCamera->m_eyePosition = m_pOwner->m_transform.position;
			XMVECTOR rotationQuat = m_pOwner->m_transform.rotation;
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

	void OnDisable() override
	{
		//delete m_pCamera;
		//m_cameraIndex = -1;
		//m_pCamera = nullptr;
	}

	Camera* GetCamera() const
	{
		return m_pCamera;
	}

	DirectX::BoundingFrustum GetFrustum() const
	{
		DirectX::BoundingFrustum frustum = m_pCamera->GetFrustum();
		//frustum.Transform(frustum, m_pOwner->m_transform.GetWorldMatrix());
		frustum.Origin = Mathf::Vector3(m_pOwner->m_transform.position);
		frustum.Orientation = m_pOwner->m_transform.rotation;

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
