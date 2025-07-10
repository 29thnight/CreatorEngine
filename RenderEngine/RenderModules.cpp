#include "RenderModules.h"
#include "../ShaderSystem.h"

void RenderModules::CleanupRenderState()
{
	ID3D11ShaderResourceView* nullSRV = nullptr;
	DeviceState::g_pDeviceContext->PSSetShaderResources(0, 1, &nullSRV);

	DeviceState::g_pDeviceContext->GSSetShader(nullptr, nullptr, 0);
}

void RenderModules::SaveRenderState()
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	if (m_prevDepthState) m_prevDepthState->Release();
	if (m_prevBlendState) m_prevBlendState->Release();
	if (m_prevRasterizerState) m_prevRasterizerState->Release();

	deviceContext->OMGetDepthStencilState(&m_prevDepthState, &m_prevStencilRef);
	deviceContext->OMGetBlendState(&m_prevBlendState, m_prevBlendFactor, &m_prevSampleMask);
	deviceContext->RSGetState(&m_prevRasterizerState);
}

void RenderModules::RestoreRenderState()
{
	auto& deviceContext = DeviceState::g_pDeviceContext;

	deviceContext->OMSetDepthStencilState(m_prevDepthState, m_prevStencilRef);
	deviceContext->OMSetBlendState(m_prevBlendState, m_prevBlendFactor, m_prevSampleMask);
	deviceContext->RSSetState(m_prevRasterizerState);

	DirectX11::UnbindRenderTargets();
}

void RenderModules::EnableClipping(bool enable)
{
	if (!SupportsClipping()) return;

	m_clippingEnabled = enable;
	m_clippingParams.clippingEnabled = enable ? 1.0f : 0.0f;

	OnClippingStateChanged();
	UpdateClippingBuffer();
}

void RenderModules::SetClippingProgress(float progress)
{
	if (!SupportsClipping()) return;
	// -1.0 ~ 1.0 범위로 확장 (음수는 역방향)
	m_clippingParams.clippingProgress = std::clamp(progress, -1.0f, 1.0f);
	UpdateClippingBuffer();
}

void RenderModules::SetClippingAxis(const Mathf::Vector3& axis)
{
	if (!SupportsClipping()) return;

	// 벡터 정규화
	Mathf::Vector3 normalizedAxis = axis;
	float length = sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);

	if (length > 0.0001f) {
		normalizedAxis.x /= length;
		normalizedAxis.y /= length;
		normalizedAxis.z /= length;
	}
	else {
		// 기본값으로 Y축 설정
		normalizedAxis = Mathf::Vector3(0.0f, 1.0f, 0.0f);
	}

	m_clippingParams.clippingAxis = normalizedAxis;

	UpdateClippingBuffer();
}

void RenderModules::SetClippingBounds(const Mathf::Vector3& min, const Mathf::Vector3& max)
{
	if (!SupportsClipping()) return;

	// min과 max 값 검증 및 정렬
	m_clippingParams.boundsMin.x = std::min(min.x, max.x);
	m_clippingParams.boundsMin.y = std::min(min.y, max.y);
	m_clippingParams.boundsMin.z = std::min(min.z, max.z);

	m_clippingParams.boundsMax.x = std::max(min.x, max.x);
	m_clippingParams.boundsMax.y = std::max(min.y, max.y);
	m_clippingParams.boundsMax.z = std::max(min.z, max.z);

	UpdateClippingBuffer();
}


