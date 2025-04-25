#include "PSO.h"
#include "ShaderSystem.h"

namespace PSOHelper
{
	ID3D11VertexShader* nullVertexShader = nullptr;
	ID3D11PixelShader* nullPixelShader = nullptr;
	ID3D11HullShader* nullHullShader = nullptr;
	ID3D11DomainShader* nullDomainShader = nullptr;
	ID3D11GeometryShader* nullGeometryShader = nullptr;
	ID3D11ComputeShader* nullComputeShader = nullptr;

	inline void VSSetShader(VertexShader* shader)
	{
		if (!shader)
		{
			DeviceState::g_pDeviceContext->VSSetShader(nullVertexShader, nullptr, 0);
			return;
		}

		DeviceState::g_pDeviceContext->VSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void PSSetShader(PixelShader* shader)
	{
		if (!shader)
		{
			DeviceState::g_pDeviceContext->PSSetShader(nullPixelShader, nullptr, 0);
			return;
		}
		DeviceState::g_pDeviceContext->PSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void HSSetShader(HullShader* shader)
	{
		if (!shader)
		{
			DeviceState::g_pDeviceContext->HSSetShader(nullHullShader, nullptr, 0);
			return;
		}
		DeviceState::g_pDeviceContext->HSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void DSSetShader(DomainShader* shader)
	{
		if (!shader)
		{
			DeviceState::g_pDeviceContext->DSSetShader(nullDomainShader, nullptr, 0);
			return;
		}
		DeviceState::g_pDeviceContext->DSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void GSSetShader(GeometryShader* shader)
	{
		if (!shader)
		{
			DeviceState::g_pDeviceContext->GSSetShader(nullGeometryShader, nullptr, 0);
			return;
		}
		DeviceState::g_pDeviceContext->GSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void CSSetShader(ComputeShader* shader)
	{
		if (!shader)
		{
			DeviceState::g_pDeviceContext->CSSetShader(nullComputeShader, nullptr, 0);
			return;
		}
		DeviceState::g_pDeviceContext->CSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void IASetInputLayout(ID3D11InputLayout* inputLayout)
	{
		if (!inputLayout) return;
		DeviceState::g_pDeviceContext->IASetInputLayout(inputLayout);
	}
}

PipelineStateObject::PipelineStateObject()
{
	m_shaderReloadEventHandle = ShaderSystem->m_shaderReloadedDelegate.AddLambda([&]()
	{
		ReloadShaders();
	});
}

PipelineStateObject::~PipelineStateObject()
{
	ShaderSystem->m_shaderReloadedDelegate.Remove(m_shaderReloadEventHandle);
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
		m_samplers[i]->Use(i);
		//ID3D11SamplerState* sampler = m_samplers[i]->m_SamplerState;
		//DeviceState::g_pDeviceContext->PSSetSamplers(i, 1, &sampler);
	}
}

void PipelineStateObject::CreateInputLayout()
{
	if(!m_inputLayoutDescContainer.empty())
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateInputLayout(
				m_inputLayoutDescContainer.data(),
				m_inputLayoutDescContainer.size(),
				m_vertexShader->GetBufferPointer(),
				m_vertexShader->GetBufferSize(),
				&m_inputLayout
			)
		);
	}
}

void PipelineStateObject::CreateInputLayout(InputLayOutContainer&& vertexLayoutDesc)
{
	m_inputLayoutDescContainer = vertexLayoutDesc;
	if (!m_inputLayoutDescContainer.empty())
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateInputLayout(
				m_inputLayoutDescContainer.data(),
				m_inputLayoutDescContainer.size(),
				m_vertexShader->GetBufferPointer(),
				m_vertexShader->GetBufferSize(),
				&m_inputLayout
			)
		);
	}
}

void PipelineStateObject::ReloadShaders()
{
	if (nullptr != m_vertexShader)
	{
		m_vertexShader = &ShaderSystem->VertexShaders[m_vertexShader.m_shader_identifier];
		if(nullptr != m_vertexShader->GetShader())
		{
			CreateInputLayout();
		}
	}
	if (nullptr != m_pixelShader)
	{
		m_pixelShader = &ShaderSystem->PixelShaders[m_pixelShader.m_shader_identifier];
	}
	if (nullptr != m_geometryShader)
	{
		m_geometryShader = &ShaderSystem->GeometryShaders[m_geometryShader.m_shader_identifier];
	}
	if (nullptr != m_hullShader)
	{
		m_hullShader = &ShaderSystem->HullShaders[m_hullShader.m_shader_identifier];
	}
	if (nullptr != m_domainShader)
	{
		m_domainShader = &ShaderSystem->DomainShaders[m_domainShader.m_shader_identifier];
	}
	if (nullptr != m_computeShader)
	{
		m_computeShader = &ShaderSystem->ComputeShaders[m_computeShader.m_shader_identifier];
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
