#pragma once
#include "ProjectionType.h"
#include "DeviceResources.h"
#include "LightProperty.h"
#include "Texture.h"
#include "Camera.generated.h"
#include "BitFlag.h"

//#ifdef DYNAMICCPP_EXPORTS
//struct ID3D11Buffer
//{
//};
//
//struct ID3D11DeviceContext
//{
//};
//#endif // !DYNAMICCPP_EXPORTS

struct ShadowInfo
{
	Mathf::xVector m_eyePosition{};
	Mathf::xVector m_lookAt{};
	float m_nearPlane{};
	float m_farPlane{};
	float m_viewWidth{};
	float m_viewHeight{};
	Mathf::xMatrix m_lightViewProjection{};
};

class MeshRenderer;
class PrimitiveRenderProxy;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
class Camera : public std::enable_shared_from_this<Camera>
{
public:
   ReflectCamera
	[[Serializable]]
	Camera();
	~Camera();

	Camera(bool isTemperary);

	Mathf::xMatrix CalculateProjection(bool shadow = false);
	Mathf::Vector4 ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth);
	Mathf::Vector4 RayCast(Mathf::Vector2 screenPosition);
	Mathf::xMatrix CalculateView() const;
	Mathf::xMatrix CalculateInverseView() const;
	Mathf::xMatrix CalculateInverseProjection();
	DirectX11::Sizef GetScreenSize() const;
	DirectX::BoundingFrustum GetFrustum();

	void SetShadowInfo(const std::vector<ShadowInfo>& shadowInfo)
	{
		m_cascadeinfo = shadowInfo;
	}

	void ApplyShadowInfo(uint32 index)
	{
		if (index < m_cascadeinfo.size())
		{
			m_eyePosition										= m_cascadeinfo[index].m_eyePosition;
			m_lookAt											= m_cascadeinfo[index].m_lookAt;
			m_nearPlane											= m_cascadeinfo[index].m_nearPlane;
			m_farPlane											= m_cascadeinfo[index].m_farPlane;
			m_viewWidth											= m_cascadeinfo[index].m_viewWidth;
			m_viewHeight										= m_cascadeinfo[index].m_viewHeight;
			m_shadowMapConstant.m_lightViewProjection[index]	= m_cascadeinfo[index].m_lightViewProjection;
		}
	}

	void ResizeRelease();
	void ResizeResources();

	void RegisterContainer();
	void HandleMovement(float deltaTime);
	void MoveToTarget(Mathf::Vector3 targetPosition);
	void UpdateBuffer(bool shadow = false);
	void UpdateBufferCascade(ID3D11DeviceContext* deferredContext, bool shadow = false);
	// 남은 두 UpdateBuffer는 렌더가 소유한 카메라에만 쓴다:
	//   UpdateBuffer(deferredContext, ...)  RenderPassData::m_shadowCamera
	//   UpdateBuffer(bool)                  SkyBoxPass의 지역 ortho 카메라
	// 렌더링 대상 카메라(게임 소유)의 상수 버퍼는 RenderPassData가 들고
	// BindFrameCameraBuffers로 묶는다. DeferredUpdateBuffer는 그래서 사라졌다
	// — 게임 소유 카메라의 GPU 버퍼를 렌더 스레드가 갱신하던 유일한 통로였다.
	void UpdateBuffer(ID3D11DeviceContext* deferredContext, bool shadow = false);

	float CalculateLODDistance(const Mathf::Vector3& position) const;

	[[Property]]
	Mathf::Quaternion rotate{ XMQuaternionIdentity() };

	static constexpr Mathf::xVector FORWARD = { 0.f, 0.f, 1.f };
	static constexpr Mathf::xVector RIGHT = { 1.f, 0.f, 0.f };
	static constexpr Mathf::xVector UP = { 0.f, 1.f, 0.f };
	static constexpr int cascadeCount = 3;

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
	float m_speedMul{ 1.f };
	float m_aspectRatio{};
	[[Property]]
	float m_fov{ 60.f };

	float m_viewWidth{ 1.f };
	float m_viewHeight{ 1.f };

	int m_monitorIndex{ 0 };
	int m_cameraIndex{ -1 };
	float deltaPitch = 0.f;
	float deltaYaw = 0.f;
	bool m_isLinkRenderData;

	Mathf::Vector4 m_rayDirection{ 0.f, 0.f, 0.f, 0.f };

	// 캐스케이드 분할 비율. 저작 설정이라 게임 스레드가 소유하고,
	// 렌더 스레드는 읽기만 한다.
	std::vector<float>			m_cascadeDevideRatios = { 0.05f, 0.15f };

	// 계산 결과(m_cascadeEnd)는 여기 있지 않다. RenderPassData가 카메라별로
	// 들고 있다 — 렌더 스레드가 게임 소유 객체의 vector를 재할당하던 것을
	// 없애기 위해서다(PHASE 3-2).
	//
	// m_cascadeinfo는 남는다: RenderPassData::m_shadowCamera(렌더 소유 카메라)가
	// SetShadowInfo로 받아 UpdateBufferCascade에 쓴다. 게임 소유 카메라의
	// 이 필드는 쓰이지 않는다.
	std::vector<ShadowInfo>		m_cascadeinfo;
	ShadowMapConstant           m_shadowMapConstant;

	bool m_isActive{ true };
	bool m_isOrthographic{ false };
	BitFlag m_avoidRenderPass{};

	ComPtr<ID3D11Buffer>	m_ViewBuffer;
	ComPtr<ID3D11Buffer>	m_ProjBuffer;
	ComPtr<ID3D11Buffer>	m_CascadeViewBuffer;
	ComPtr<ID3D11Buffer>	m_CascadeProjBuffer;
};

class CameraContainer : public DLLCore::Singleton<CameraContainer>
{
private:
	CameraContainer()
	{
		m_cameras.resize(10);
	}
	~CameraContainer() = default;
	friend class DLLCore::Singleton<CameraContainer>;

public:
	void Finalize()
	{
		m_cameras.clear();
	}

public:
	// 슬롯이 가득 찬 경우를 나타내는 인덱스.
	static constexpr int INVALID_CAMERA_INDEX = -1;

	int AddCamera(std::shared_ptr<Camera> camera)
	{
		for (int i = 0; i < m_cameras.size(); ++i)
		{
			if (nullptr == m_cameras[i])
			{
				m_cameras[i] = camera;
				return i;
			}
		}

		// 빈 슬롯이 없을 때 반환값 없이 함수가 끝나면 정의되지 않은 동작이 되고,
		// 릴리스 빌드에서는 쓰레기 값이 인덱스로 쓰여 배열 범위를 벗어난다.
		Debug->LogError("카메라 슬롯이 가득 찼습니다. 새 카메라를 등록할 수 없습니다.");
		return INVALID_CAMERA_INDEX;
	}

	void DeleteCamera(uint32 index)
	{
		if (index < m_cameras.size())
		{
			m_cameras[index].reset();
		}
	}

	void ReplaceCamera(uint32 index, std::shared_ptr<Camera> camera)
	{
		if (index < m_cameras.size())
		{
			m_cameras[index] = camera;
			camera->m_cameraIndex = index;
		}
	}

	std::shared_ptr<Camera> GetCamera(uint32 index)
	{
		if (index < m_cameras.size())
		{
			return m_cameras[index];
		}

		return nullptr;
	}

	std::vector<std::shared_ptr<Camera>>& GetCameras()
	{
		return m_cameras;
	}

	std::shared_ptr<Camera> GetLastCamera()
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

	size_t GetCameraCount()
	{
		size_t count = 0;
		for (auto& camera : m_cameras)
		{
			if (nullptr != camera)
			{
				count++;
			}
		}
		return count;
	}

	Core::Delegate<void> m_releaseResourcesEvent{};
	Core::Delegate<void> m_recreateResourcesEvent{};

private:
	std::vector<std::shared_ptr<Camera>> m_cameras;
};

inline auto CameraManagement = CameraContainer::GetInstance();
