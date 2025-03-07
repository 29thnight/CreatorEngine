#include "PSO.h"

void PipelineStateObject::Apply()
{
	DeviceState::g_pDeviceContext->IASetInputLayout(m_inputLayout);
	DeviceState::g_pDeviceContext->IASetPrimitiveTopology(m_primitiveTopology);

	DeviceState::g_pDeviceContext->VSSetShader(m_vertexShader->GetShader(), nullptr, 0);
	DeviceState::g_pDeviceContext->PSSetShader(m_pixelShader->GetShader(), nullptr, 0);
	DeviceState::g_pDeviceContext->HSSetShader(m_hullShader->GetShader(), nullptr, 0);
	DeviceState::g_pDeviceContext->DSSetShader(m_domainShader->GetShader(), nullptr, 0);
	DeviceState::g_pDeviceContext->GSSetShader(m_geometryShader->GetShader(), nullptr, 0);
	DeviceState::g_pDeviceContext->CSSetShader(m_computeShader->GetShader(), nullptr, 0);

	DeviceState::g_pDeviceContext->RSSetState(m_rasterizerState);
	DeviceState::g_pDeviceContext->OMSetBlendState(m_blendState, nullptr, 0xffffffff);
	DeviceState::g_pDeviceContext->OMSetDepthStencilState(m_depthStencilState, 0);

	for (uint32 i = 0; i < m_samplers.size(); ++i)
	{
		m_samplers[i].Use(i);
	}
}
