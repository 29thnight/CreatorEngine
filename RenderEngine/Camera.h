#pragma once
#include "ProjectionType.h"
#include "DeviceResources.h"
#include "Texture.h"
#include "Camera.generated.h"

class Camera
{
public:
   ReflectCamera
	[[Serializable]]
	Camera();
	~Camera();

	Mathf::xMatrix CalculateProjection(bool shadow = false);
	Mathf::Vector4 ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth);
	Mathf::Vector4 RayCast(Mathf::Vector2 screenPosition);
	Mathf::xMatrix CalculateView() const;
	Mathf::xMatrix CalculateInverseView() const;
	Mathf::xMatrix CalculateInverseProjection();
	DirectX11::Sizef GetScreenSize() const;
	DirectX::BoundingFrustum GetFrustum();

	void RegisterContainer();
	void HandleMovement(float deltaTime);
	void UpdateBuffer(bool shadow = false);
	void UpdateBuffer(ID3D11DeviceContext* deferredContext);
	void ClearRenderTarget();

	[[Property]]
	Mathf::Vector4 rotate{ XMQuaternionIdentity() };

	static constexpr Mathf::xVector FORWARD = { 0.f, 0.f, 1.f };
	static constexpr Mathf::xVector RIGHT = { 1.f, 0.f, 0.f };
	static constexpr Mathf::xVector UP = { 0.f, 1.f, 0.f };

	Mathf::xVector m_eyePosition{ XMVectorSet(0, 1, -10, 1) };
	Mathf::xVector m_forward{ FORWARD };
	Mathf::xVector m_right{ RIGHT };
	Mathf::xVector m_up{ UP };
	Mathf::xVector m_lookAt{ m_eyePosition + m_forward };
	Mathf::xVector m_rotation{ 0.f, 0.f, 0.f, 1.f };

	[[Property]]
	float m_nearPlane{ 0.1f };
	[[Property]]
	float m_farPlane{ 500.f };
	[[Property]]
	float m_speed{ 10.f };

	float m_aspectRatio{};
	[[Property]]
	float m_fov{ 60.f };

	float m_viewWidth{ 1.f };
	float m_viewHeight{ 1.f };

	int m_monitorIndex{ 0 };
	int m_cameraIndex{ -1 };

	Mathf::Vector4 m_rayDirection{ 0.f, 0.f, 0.f, 0.f };

	bool m_isActive{ true };
	bool m_isOrthographic{ false };
	ApplyRenderPipelinePass m_applyRenderPipelinePass{};

	ComPtr<ID3D11Buffer> m_ViewBuffer;
	ComPtr<ID3D11Buffer> m_ProjBuffer;
    UniqueTexturePtr m_renderTarget{ nullptr, TextureHelper::deleter };
    UniqueTexturePtr m_depthStencil{ nullptr, TextureHelper::deleter };
};

class SceneRenderer;
class CameraContainer : public Singleton<CameraContainer>
{
private:
	CameraContainer()
	{
		m_cameras.resize(10);
	}
	~CameraContainer() = default;
	friend class Singleton<CameraContainer>;

public:
	int AddCamera(Camera* camera)
	{
		for (int i = 0; i < m_cameras.size(); ++i)
		{
			if (nullptr == m_cameras[i])
			{
				m_cameras[i] = camera;
				return i;
			}
		}
	}

	void DeleteCamera(uint32 index)
	{
		if (index < m_cameras.size())
		{
			m_cameras[index] = nullptr;
		}
	}

	void ReplaceCamera(uint32 index, Camera* camera)
	{
		if (index < m_cameras.size())
		{
			if (nullptr != m_cameras[index])
			{
				delete m_cameras[index];
			}

			m_cameras[index] = camera;
			m_cameras[index]->m_cameraIndex = index;
		}
	}

	Camera* GetCamera(uint32 index)
	{
		if (index < m_cameras.size())
		{
			return m_cameras[index];
		}

		return nullptr;
	}

	Camera* GetLastCamera()
	{
		for (int i = m_cameras.size() - 1; i >= 0; --i)
		{
			if (nullptr != m_cameras[i] && m_cameras[i]->m_isActive)
			{
				return m_cameras[i];
			}
		}
		return nullptr;
	}

private:
	friend class SceneRenderer;
	std::vector<Camera*> m_cameras;
};

inline auto& CameraManagement = CameraContainer::GetInstance();
