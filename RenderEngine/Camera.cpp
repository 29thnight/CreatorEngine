#include "Camera.h"
#include "InputManager.h"
#include "DeviceState.h"
#include "ImGuiRegister.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "RenderCommand.h"

const static float pi = XM_PIDIV2 - 0.01f;
const static float pi2 = XM_PI * 2.f;

Camera::Camera()
{
	m_aspectRatio = DeviceState::g_aspectRatio;

	m_cameraIndex = CameraManagement->GetCameraCount();

	std::string cameraRTVName = "Camera_" + std::to_string(m_cameraIndex) + "_RTV";

    auto renderTexture = TextureHelper::CreateRenderTexture(  
       DeviceState::g_ClientRect.width,  
       DeviceState::g_ClientRect.height,  
       cameraRTVName,  
       DXGI_FORMAT_R16G16B16A16_FLOAT  
    );  
    m_renderTarget.swap(renderTexture);


	auto depthStencil = TextureHelper::CreateDepthTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"Camera_" + std::to_string(m_cameraIndex) + "_DSV"
	);
    m_depthStencil.swap(depthStencil);

	XMMATRIX identity = XMMatrixIdentity();

	std::string viewBufferName = "Camera(" + std::to_string(m_cameraIndex) + ")ViewBuffer";
	std::string projBufferName = "Camera(" + std::to_string(m_cameraIndex) + ")ProjBuffer";

	m_ViewBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ViewBuffer.Get(), viewBufferName.c_str());
	m_ProjBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ProjBuffer.Get(), projBufferName.c_str());

	m_defferdQueue.reserve(300);
	m_forwardQueue.reserve(300);
}

Camera::~Camera()
{
	if (m_cameraIndex != -1)
	{
		CameraManagement->DeleteCamera(m_cameraIndex);
	}
}

Camera::Camera(bool isShadow)
{
	if(isShadow)
	{
		m_aspectRatio = DeviceState::g_aspectRatio;

		m_cameraIndex = CameraManagement->GetCameraCount();

		std::string cameraRTVName = "ShadowCamera_RTV";

		auto renderTexture = TextureHelper::CreateRenderTexture(
			DeviceState::g_ClientRect.width,
			DeviceState::g_ClientRect.height,
			cameraRTVName,
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_renderTarget.swap(renderTexture);

		auto depthStencil = TextureHelper::CreateDepthTexture(
			DeviceState::g_ClientRect.width,
			DeviceState::g_ClientRect.height,
			"ShadowCamera_DSV"
		);
		m_depthStencil.swap(depthStencil);

		XMMATRIX identity = XMMatrixIdentity();

		std::string viewBufferName = "ShadowCamera_ViewBuffer";
		std::string projBufferName = "ShadowCamera_ProjBuffer";

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
	return DeviceState::g_ClientRect;
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

void Camera::ResizeRelease()
{
	if (m_renderTarget)
	{
		m_renderTarget.reset();
	}

	if (m_depthStencil)
	{
		m_depthStencil.reset();
	}
}

void Camera::ResizeResources()
{
	m_aspectRatio = DeviceState::g_aspectRatio;

	std::string cameraRTVName = "Camera_" + std::to_string(m_cameraIndex) + "_RTV";
	auto renderTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		cameraRTVName,
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
	m_renderTarget.swap(renderTexture);

	std::string depthName = "Camera_" + std::to_string(m_cameraIndex) + "_DSV";
	auto depthStencil = TextureHelper::CreateDepthTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		depthName
	);
	m_depthStencil.swap(depthStencil);
}

void Camera::RegisterContainer()
{
	m_cameraIndex = CameraManagement->AddCamera(this);
}

void Camera::HandleMovement(float deltaTime)
{
	float x = 0.f, y = 0.f, z = 0.f;

	if (InputManagement->IsKeyPressed('W'))
	{
		z += 1.f;
	}
	if (InputManagement->IsKeyPressed('S'))
	{
		z -= 1.f;
	}
	if (InputManagement->IsKeyPressed('A'))
	{
		x -= 1.f;
	}
	if (InputManagement->IsKeyPressed('D'))
	{
		x += 1.f;
	}
	if (InputManagement->IsKeyPressed('Q'))
	{
		y -= 1.f;
	}
	if (InputManagement->IsKeyPressed('E'))
	{
		y += 1.f;
	}

	XMVECTOR m_rotationQuat = XMQuaternionIdentity();
	//Change the Camera Rotaition Quaternion Not Use XMQuaternionRotationRollPitchYaw
	if (InputManagement->IsMouseButtonPressed(MouseKey::RIGHT))
	{
		// 마우스 이동량 가져오기
		float deltaPitch = InputManagement->GetMouseDelta().y * 0.01f;
		float deltaYaw = InputManagement->GetMouseDelta().x * 0.01f;

		// 현재 회전 기준 축을 얻음
		XMVECTOR rightAxis = XMVector3Normalize(XMVector3Cross(m_up, m_forward));

		// 프레임당 변화량만 적용
		XMVECTOR pitchQuat = XMQuaternionRotationAxis(rightAxis, deltaPitch);
		XMVECTOR yawQuat = XMQuaternionRotationAxis(m_up, deltaYaw);

		// Yaw를 먼저 적용 -> Pitch를 적용
		XMVECTOR deltaRotation = XMQuaternionMultiply(yawQuat, pitchQuat);
		m_rotationQuat = XMQuaternionMultiply(deltaRotation, m_rotationQuat);
		m_rotationQuat = XMQuaternionMultiply(rotate, m_rotationQuat);
		m_rotationQuat = XMQuaternionNormalize(m_rotationQuat);

		// 새로운 방향 벡터 계산
		m_forward = XMVector3Normalize(XMVector3Rotate(FORWARD, m_rotationQuat));

		// Right 벡터 업데이트 (UP을 기준으로 다시 계산)
		m_right = XMVector3Normalize(XMVector3Cross(m_up, m_forward));

		m_up = XMVector3Cross(m_forward, m_right);

			float sign = XMVectorGetY(m_up) > 0 ? 1.0f : -1.0f;
			m_up = XMVectorSet(XMVectorGetX(m_up), sign * 20.f, XMVectorGetZ(m_up), 0);
			m_up = XMQuaternionNormalize(m_up);

		rotate = m_rotationQuat;
	}

	m_eyePosition += ((z * m_forward) + (y * m_up) + (x * m_right)) * m_speed * deltaTime;
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

void Camera::UpdateBuffer(ID3D11DeviceContext* deferredContext, bool shadow)
{
	Mathf::xMatrix view = CalculateView();
	Mathf::xMatrix proj = CalculateProjection(shadow);

	deferredContext->UpdateSubresource(m_ViewBuffer.Get(), 0, nullptr, &view, 0, 0);
	deferredContext->UpdateSubresource(m_ProjBuffer.Get(), 0, nullptr, &proj, 0, 0);

	deferredContext->VSSetConstantBuffers(1, 1, m_ViewBuffer.GetAddressOf());
	deferredContext->VSSetConstantBuffers(2, 1, m_ProjBuffer.GetAddressOf());
}

void Camera::ClearRenderTarget()
{
	DirectX11::ClearRenderTargetView(m_renderTarget->GetRTV(), DirectX::Colors::Transparent);
	DirectX11::ClearDepthStencilView(m_depthStencil->m_pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void Camera::PushRenderQueue(MeshRendererProxy* command)
{
	Material* mat = command->m_Material;
	if (nullptr == mat) return;

	{
		std::unique_lock lock(m_cameraMutex);

		switch (mat->m_renderingMode)
		{
		case MaterialRenderingMode::Opaque:
			m_defferdQueue.push_back(command);
			break;
		case MaterialRenderingMode::Transparent:
			m_forwardQueue.push_back(command);
			break;
		}
	}
}

void Camera::SortRenderQueue()
{
	auto sortByAnimatorAndMaterialGuid = [](MeshRendererProxy* a, MeshRendererProxy* b)
	{
		if (a->m_animatorGuid == b->m_animatorGuid)
		{
			return a->m_materialGuid < b->m_materialGuid;
		}
		return a->m_animatorGuid < b->m_animatorGuid;
	};

	if(!m_defferdQueue.empty())
	{
		std::sort(m_defferdQueue.begin(), m_defferdQueue.end(), sortByAnimatorAndMaterialGuid);
	}

	if(!m_forwardQueue.empty())
	{
		std::sort(m_forwardQueue.begin(), m_forwardQueue.end(), sortByAnimatorAndMaterialGuid);
	}
}

void Camera::ClearRenderQueue()
{
	m_defferdQueue.clear();
	m_forwardQueue.clear();
}