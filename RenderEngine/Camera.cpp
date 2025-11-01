#include "Camera.h"
#include "InputManager.h"
#include "DeviceState.h"
#include "ImGuiRegister.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "MeshRendererProxy.h"
#include "RenderScene.h"
#include "SceneManager.h"

Camera::Camera() : m_isLinkRenderData(true)
{
	m_aspectRatio = DirectX11::DeviceStates->g_aspectRatio;

	m_cameraIndex = CameraManagement->GetCameraCount();
	auto renderScene = SceneManagers->GetRenderScene();

	if(m_isLinkRenderData)
	{
		renderScene->AddRenderPassData(m_cameraIndex);
	}

	XMMATRIX identity = XMMatrixIdentity();

	std::string viewBufferName = "Camera(" + std::to_string(m_cameraIndex) + ")ViewBuffer";
	std::string projBufferName = "Camera(" + std::to_string(m_cameraIndex) + ")ProjBuffer";

	m_ViewBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ViewBuffer.Get(), viewBufferName.c_str());
	m_ProjBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ProjBuffer.Get(), projBufferName.c_str());

	m_cascadeinfo.resize(cascadeCount);
}

Camera::~Camera()
{
	if (m_cameraIndex != -1 && m_isLinkRenderData)
	{
		CameraManagement->DeleteCamera(m_cameraIndex);
		auto renderScene = SceneManagers->GetRenderScene();
		renderScene->RemoveRenderPassData(m_cameraIndex);
	}
}

Camera::Camera(bool isTemperary) : m_isLinkRenderData(false)
{
	if(!isTemperary)
	{
		m_aspectRatio = DirectX11::DeviceStates->g_aspectRatio;

		m_cameraIndex = CameraManagement->GetCameraCount();

		XMMATRIX identity = XMMatrixIdentity();

		std::string viewBufferName = "Camera_ViewBuffer";
		std::string projBufferName = "Camera_ProjBuffer";

		m_ViewBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
		DirectX::SetName(m_ViewBuffer.Get(), viewBufferName.c_str());
		m_ProjBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
		DirectX::SetName(m_ProjBuffer.Get(), projBufferName.c_str());
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

DirectX11::Sizef Camera::GetScreenSize() const
{
	DirectX11::Sizef size;
	size = { DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height };
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

void Camera::UpdateBuffer(bool shadow)
{
	Mathf::xMatrix view = CalculateView();
	Mathf::xMatrix proj = CalculateProjection(shadow);
	DirectX11::UpdateBuffer(m_ViewBuffer.Get(), &view);
	DirectX11::UpdateBuffer(m_ProjBuffer.Get(), &proj);

	DirectX11::VSSetConstantBuffer(1, 1, m_ViewBuffer.GetAddressOf());
	DirectX11::VSSetConstantBuffer(2, 1, m_ProjBuffer.GetAddressOf());
}

void Camera::UpdateBufferCascade(ID3D11DeviceContext* deferredContext, bool shadow)
{
	if (nullptr == m_CascadeViewBuffer)
	{
		m_CascadeViewBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * cascadeCount, D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER);
		DirectX::SetName(m_CascadeViewBuffer.Get(), "CameraCascadeViewBuffer");
	}

	if (nullptr == m_CascadeProjBuffer)
	{
		m_CascadeProjBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * cascadeCount, D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER);
		DirectX::SetName(m_CascadeProjBuffer.Get(), "CameraCascadeProjBuffer");
	}

	Mathf::xMatrix view[cascadeCount]{};
	Mathf::xMatrix proj[cascadeCount]{};
	for(auto i : std::views::iota(0, cascadeCount))
	{
		ApplyShadowInfo(i);
		view[i] = CalculateView();
		proj[i] = CalculateProjection(shadow);
	}

	deferredContext->UpdateSubresource(m_CascadeViewBuffer.Get(), 0, nullptr, view, 0, 0);
	deferredContext->UpdateSubresource(m_CascadeProjBuffer.Get(), 0, nullptr, proj, 0, 0);

	deferredContext->VSSetConstantBuffers(1, 1, m_CascadeViewBuffer.GetAddressOf());
	deferredContext->VSSetConstantBuffers(2, 1, m_CascadeProjBuffer.GetAddressOf());
}

void Camera::UpdateBuffer(ID3D11DeviceContext* deferredContext, bool shadow)
{
	Mathf::xMatrix view = CalculateView();
	Mathf::xMatrix proj = CalculateProjection(shadow);

	deferredContext->UpdateSubresource(m_ViewBuffer.Get(), 0, nullptr, &view, 0, 0);
	deferredContext->UpdateSubresource(m_ProjBuffer.Get(), 0, nullptr, &proj, 0, 0);

	deferredContext->VSSetConstantBuffers(1, 1, m_ViewBuffer.GetAddressOf());
	deferredContext->VSSetConstantBuffers(2, 1, m_ProjBuffer.GetAddressOf());
}

float Camera::CalculateLODDistance(const Mathf::Vector3& position) const
{
	return Mathf::Vector3::Distance(m_eyePosition, position);
}
