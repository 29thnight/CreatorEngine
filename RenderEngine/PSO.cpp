#include "PSO.h"

namespace PSOHelper
{
	inline void VSSetShader(VertexShader* shader)
	{
		if (!shader) return;

		DeviceState::g_pDeviceContext->VSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void PSSetShader(PixelShader* shader)
	{
		if (!shader) return;
		DeviceState::g_pDeviceContext->PSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void HSSetShader(HullShader* shader)
	{
		if (!shader) return;
		DeviceState::g_pDeviceContext->HSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void DSSetShader(DomainShader* shader)
	{
		if (!shader) return;
		DeviceState::g_pDeviceContext->DSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void GSSetShader(GeometryShader* shader)
	{
		if (!shader) return;
		DeviceState::g_pDeviceContext->GSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void CSSetShader(ComputeShader* shader)
	{
		if (!shader) return;
		DeviceState::g_pDeviceContext->CSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void IASetInputLayout(ID3D11InputLayout* inputLayout)
	{
		if (!inputLayout) return;
		DeviceState::g_pDeviceContext->IASetInputLayout(inputLayout);
	}
}

void PipelineStateObject::Apply()
{
	DeviceState::g_pDeviceContext->IASetInputLayout(m_inputLayout);
	DeviceState::g_pDeviceContext->IASetPrimitiveTopology(m_primitiveTopology);

	PSOHelper::VSSetShader(m_vertexShader);
	PSOHelper::PSSetShader(m_pixelShader);
	PSOHelper::HSSetShader(m_hullShader);
	PSOHelper::DSSetShader(m_domainShader);
	PSOHelper::GSSetShader(m_geometryShader);
	PSOHelper::CSSetShader(m_computeShader);

	DeviceState::g_pDeviceContext->RSSetState(m_rasterizerState);
	DeviceState::g_pDeviceContext->OMSetBlendState(m_blendState, nullptr, 0xffffffff);
	DeviceState::g_pDeviceContext->OMSetDepthStencilState(m_depthStencilState, 0);

	for (uint32 i = 0; i < m_samplers.size(); ++i)
	{
		//m_samplers[i].Use(i);
		ID3D11SamplerState* sampler = m_samplers[i]->m_SamplerState;
		DeviceState::g_pDeviceContext->PSSetSamplers(i, 1, &sampler);
	}
}

void PipelineStateObject::Reset()
{

    DeviceState::g_pDeviceContext->IASetInputLayout(nullptr);
    DeviceState::g_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    PSOHelper::VSSetShader(nullptr);
    PSOHelper::PSSetShader(nullptr);
    PSOHelper::HSSetShader(nullptr);
    PSOHelper::DSSetShader(nullptr);
    PSOHelper::GSSetShader(nullptr);
    PSOHelper::CSSetShader(nullptr);

    DeviceState::g_pDeviceContext->RSSetState(nullptr);
    DeviceState::g_pDeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    DeviceState::g_pDeviceContext->OMSetDepthStencilState(nullptr, 0);
}
