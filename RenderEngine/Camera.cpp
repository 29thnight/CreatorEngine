#include "Camera.h"
#include "RHI/ScreenSizedResource.h"
#include "InputManager.h"
#include "ImGuiRegister.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "PrimitiveRenderProxy.h"
#include "RenderScene.h"
// 활성 RenderScene은 렌더 소유자(EnhancedSceneRenderer)에게 직접 묻는다 —
// SceneManagers 경유는 렌더→게임플레이 역방향 간선이었다(RenderPassData.cpp와 동일).
#include "RHI/DX12/EnhancedSceneRenderer.h"

Camera::Camera() : m_isLinkRenderData(true)
{
	m_aspectRatio = ScreenResizeBus::Get().GetAspectRatio();

	// m_cameraIndex는 RegisterContainer()에서 실제 슬롯이 확정될 때 정해진다.
	// 예전에는 여기서 GetCameraCount()로 인덱스를 미리 추측해 RenderPassData까지
	// 등록했는데, 삭제로 생긴 빈 슬롯이 있으면 AddCamera가 반환하는 실제 인덱스와
	// 어긋나 먼저 등록해 둔 RenderPassData가 어떤 소멸 경로로도 회수되지 않는
	// 고아(렌더 타겟/뎁스 텍스처 누수)가 됐다.



	m_cascadeinfo.resize(cascadeCount);
}

Camera::~Camera()
{
	if (m_cameraIndex != -1 && m_isLinkRenderData)
	{
		CameraManagement->DeleteCamera(m_cameraIndex);
		auto renderScene = EnhancedSceneRenderer::GetRenderScene();
		renderScene->RemoveRenderPassData(m_cameraIndex);
	}
}

Camera::Camera(bool isTemperary) : m_isLinkRenderData(false)
{
	if(!isTemperary)
	{
		m_aspectRatio = ScreenResizeBus::Get().GetAspectRatio();

		m_cameraIndex = CameraManagement->GetCameraCount();


		std::string viewBufferName = "Camera_ViewBuffer";
		std::string projBufferName = "Camera_ProjBuffer";

	}
}

Mathf::xMatrix Camera::CalculateProjection(bool shadow)
{
	if (0 == m_nearPlane)
	{
		m_nearPlane = 0.1f;
	}

	if (shadow)
	{
		return XMMatrixOrthographicOffCenterLH(-m_viewWidth, m_viewWidth, -m_viewHeight, m_viewHeight, m_nearPlane, m_farPlane);
	}

	if (m_isOrthographic)
	{
		return XMMatrixOrthographicLH(m_viewWidth, m_viewHeight, m_nearPlane, m_farPlane);
	}
	else
	{
		return XMMatrixPerspectiveFovLH(XMConvertToRadians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
	}
}

Mathf::Vector4 Camera::ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth)
{
	// 1. 스크린 좌표를 NDC 좌표로 변환
	float x_ndc = (2.0f * screenPosition.x / GetScreenSize().width) - 1.0f;
	float y_ndc = 1.0f - (2.0f * screenPosition.y / GetScreenSize().height);
	Mathf::Vector4 screenPositionNDC = { x_ndc, y_ndc, depth, 1.0f };

	// 2. 역행렬 투영 변환 (Projection^-1)
	Mathf::xMatrix invProj = CalculateInverseProjection();
	Mathf::Vector4 viewPosition = XMVector3TransformCoord(screenPositionNDC, invProj);

	// 3. 뷰 역행렬 변환 (View^-1)
	Mathf::xMatrix invView = CalculateInverseView();
	Mathf::Vector4 worldPosition = XMVector3TransformCoord(viewPosition, invView);

	return worldPosition;
}

Mathf::Vector4 Camera::RayCast(Mathf::Vector2 screenPosition)
{
	Mathf::Vector4 _near = ConvertScreenToWorld(screenPosition, 0.f);
	Mathf::Vector4 _far = ConvertScreenToWorld(screenPosition, 1.f);
	Mathf::Vector4 direction = _far - _near;
	direction = XMVector3Normalize(direction);
	return direction;
}

Mathf::xMatrix Camera::CalculateView() const
{
	return XMMatrixLookAtLH(m_eyePosition, m_lookAt, m_up);
}

Mathf::xMatrix Camera::CalculateInverseView() const
{
	return XMMatrixInverse(nullptr, CalculateView());
}

Mathf::xMatrix Camera::CalculateInverseProjection()
{
	return XMMatrixInverse(nullptr, CalculateProjection());
}

Core::Sizef Camera::GetScreenSize() const
{
	Core::Sizef size;
	size = { static_cast<float>(ScreenResizeBus::Get().GetWidth()), static_cast<float>(ScreenResizeBus::Get().GetHeight()) };
	return size;
}

DirectX::BoundingFrustum Camera::GetFrustum()
{
	if (m_farPlane == 0.f)
	{
		m_farPlane = 0.5f;
	}

	DirectX::BoundingFrustum frustum;
	BoundingFrustum::CreateFromMatrix(frustum, CalculateProjection());

	DirectX::XMMATRIX viewMatrix = CalculateView();
	DirectX::XMMATRIX invView = XMMatrixInverse(nullptr, viewMatrix);
	frustum.Transform(frustum, invView);

	return frustum;
}

void Camera::RegisterContainer()
{
	m_cameraIndex = CameraManagement->AddCamera(shared_from_this());
	if (CameraContainer::INVALID_CAMERA_INDEX == m_cameraIndex)
	{
		return;
	}

	// 실제 슬롯이 확정된 뒤에 렌더 데이터를 등록한다.
	// 이렇게 해야 소멸자의 RemoveRenderPassData(m_cameraIndex)와 짝이 맞는다.
	if (m_isLinkRenderData)
	{
		if (auto renderScene = EnhancedSceneRenderer::GetRenderScene())
		{
			renderScene->AddRenderPassData(m_cameraIndex);
		}
	}

}

void Camera::HandleMovement(float deltaTime)
{
	float x = 0.f, y = 0.f, z = 0.f;
	constexpr float minSpeed = 10.f;
	constexpr float maxSpeed = 100.f;

	if (InputManagement->IsKeyPressed('W') || ImGui::IsKeyDown(ImGuiKey_W))
	{
		z += 1.f;
	}
	if (InputManagement->IsKeyPressed('S') || ImGui::IsKeyDown(ImGuiKey_S))
	{
		z -= 1.f;
	}
	if (InputManagement->IsKeyPressed('A') || ImGui::IsKeyDown(ImGuiKey_A))
	{
		x -= 1.f;
	}
	if (InputManagement->IsKeyPressed('D') || ImGui::IsKeyDown(ImGuiKey_D))
	{
		x += 1.f;
	}
	if (InputManagement->IsKeyPressed('Q') || ImGui::IsKeyDown(ImGuiKey_Q))
	{
		y -= 1.f;
	}
	if (InputManagement->IsKeyPressed('E') || ImGui::IsKeyDown(ImGuiKey_E))
	{
		y += 1.f;
	}

	if (InputManagement->IsWheelUp())
	{
		m_speedMul += 0.01f;
		m_speedMul = std::clamp(m_speedMul, 0.01f, 2.f);
		m_speed = m_speed * m_speedMul;
		m_speed = std::clamp(m_speed, minSpeed, maxSpeed);
	}

	if (InputManagement->IsWheelDown())
	{
		m_speedMul -= 0.01f;
		m_speedMul = std::clamp(m_speedMul, 0.01f, 2.f);
		m_speed = m_speed * m_speedMul;
		m_speed = std::clamp(m_speed, minSpeed, maxSpeed);
	}

	XMVECTOR m_rotationQuat = XMQuaternionIdentity();
	//Change the Camera Rotaition Quaternion Not Use XMQuaternionRotationRollPitchYaw
	if (InputManagement->IsMouseButtonPressed(MouseKey::RIGHT) || ImGui::IsMouseDown(ImGuiMouseButton_Right))
	{
		// 마우스 이동량 가져오기
		deltaPitch += InputManagement->GetMouseDelta().y * 0.01f;
		deltaYaw += InputManagement->GetMouseDelta().x * 0.01f;

		XMVECTOR qYaw = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0), deltaYaw);

		// qYaw 기준으로 회전된 로컬 X축
		XMVECTOR right = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), qYaw);

		XMVECTOR qPitch = XMQuaternionRotationAxis(right, deltaPitch);

		// 최종 쿼터니언 = Yaw → Pitch
		XMVECTOR cameraRot = XMQuaternionMultiply(qYaw, qPitch);

		// Forward 벡터 구하기
		m_forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), cameraRot);
		m_up = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), cameraRot);
		m_right = XMVector3Cross(m_up, m_forward);
		rotate = cameraRot;
	}

	m_eyePosition += ((z * m_forward) + (y * m_up) + (x * m_right)) * m_speed * deltaTime;
	m_lookAt = m_eyePosition + m_forward;
}

void Camera::MoveToTarget(Mathf::Vector3 targetPosition)
{
	m_eyePosition = targetPosition;
	m_lookAt = m_eyePosition + m_forward;
}

float Camera::CalculateLODDistance(const Mathf::Vector3& position) const
{
	return Mathf::Vector3::Distance(m_eyePosition, position);
}
