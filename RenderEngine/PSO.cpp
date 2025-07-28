#ifndef DYNAMICCPP_EXPORTS
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

	inline void VSSetShader(ID3D11DeviceContext* pDeviceContext, VertexShader* shader)
	{
		if (!shader)
		{
			pDeviceContext->VSSetShader(nullVertexShader, nullptr, 0);
			return;
		}

		pDeviceContext->VSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void PSSetShader(ID3D11DeviceContext* pDeviceContext, PixelShader* shader)
	{
		if (!shader)
		{
			pDeviceContext->PSSetShader(nullPixelShader, nullptr, 0);
			return;
		}
		pDeviceContext->PSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void HSSetShader(ID3D11DeviceContext* pDeviceContext, HullShader* shader)
	{
		if (!shader)
		{
			pDeviceContext->HSSetShader(nullHullShader, nullptr, 0);
			return;
		}
		pDeviceContext->HSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void DSSetShader(ID3D11DeviceContext* pDeviceContext, DomainShader* shader)
	{
		if (!shader)
		{
			pDeviceContext->DSSetShader(nullDomainShader, nullptr, 0);
			return;
		}
		pDeviceContext->DSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void GSSetShader(ID3D11DeviceContext* pDeviceContext, GeometryShader* shader)
	{
		if (!shader)
		{
			pDeviceContext->GSSetShader(nullGeometryShader, nullptr, 0);
			return;
		}
		pDeviceContext->GSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void CSSetShader(ID3D11DeviceContext* pDeviceContext, ComputeShader* shader)
	{
		if (!shader)
		{
			pDeviceContext->CSSetShader(nullComputeShader, nullptr, 0);
			return;
		}
		pDeviceContext->CSSetShader(shader->GetShader(), nullptr, 0);
	}

	inline void IASetInputLayout(ID3D11DeviceContext* pDeviceContext, ID3D11InputLayout* inputLayout)
	{
		if (!inputLayout) return;
		pDeviceContext->IASetInputLayout(inputLayout);
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

	Memory::SafeDelete(m_inputLayout);
	Memory::SafeDelete(m_rasterizerState);
}

void PipelineStateObject::Apply()
{
	DeviceState::g_pDeviceContext->IASetInputLayout(m_inputLayout);
	DeviceState::g_pDeviceContext->IASetPrimitiveTopology(m_primitiveTopology);

	PSOHelper::VSSetShader(DeviceState::g_pDeviceContext, m_vertexShader);
	PSOHelper::PSSetShader(DeviceState::g_pDeviceContext, m_pixelShader);
	PSOHelper::HSSetShader(DeviceState::g_pDeviceContext, m_hullShader);
	PSOHelper::DSSetShader(DeviceState::g_pDeviceContext, m_domainShader);
	PSOHelper::GSSetShader(DeviceState::g_pDeviceContext, m_geometryShader);
	PSOHelper::CSSetShader(DeviceState::g_pDeviceContext, m_computeShader);

	DeviceState::g_pDeviceContext->RSSetState(m_rasterizerState);
	DeviceState::g_pDeviceContext->OMSetBlendState(m_blendState, nullptr, 0xffffffff);
	DeviceState::g_pDeviceContext->OMSetDepthStencilState(m_depthStencilState, 0);

	for (uint32 i = 0; i < m_samplers.size(); ++i)
	{
		m_samplers[i]->Use(i);
	}
}

void PipelineStateObject::Apply(ID3D11DeviceContext* deferredContext)
{
	deferredContext->IASetInputLayout(m_inputLayout);
	deferredContext->IASetPrimitiveTopology(m_primitiveTopology);

	PSOHelper::VSSetShader(deferredContext, m_vertexShader);
	PSOHelper::PSSetShader(deferredContext, m_pixelShader);
	PSOHelper::HSSetShader(deferredContext, m_hullShader);
	PSOHelper::DSSetShader(deferredContext, m_domainShader);
	PSOHelper::GSSetShader(deferredContext, m_geometryShader);
	PSOHelper::CSSetShader(deferredContext, m_computeShader);

	deferredContext->RSSetState(m_rasterizerState);
	deferredContext->OMSetBlendState(m_blendState, nullptr, 0xffffffff);
	deferredContext->OMSetDepthStencilState(m_depthStencilState, 0);

	for (uint32 i = 0; i < m_samplers.size(); ++i)
	{
		m_samplers[i]->Use(deferredContext, i);
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

    PSOHelper::VSSetShader(DeviceState::g_pDeviceContext, nullptr);
	PSOHelper::PSSetShader(DeviceState::g_pDeviceContext, nullptr);
	PSOHelper::HSSetShader(DeviceState::g_pDeviceContext, nullptr);
	PSOHelper::DSSetShader(DeviceState::g_pDeviceContext, nullptr);
	PSOHelper::GSSetShader(DeviceState::g_pDeviceContext, nullptr);
	PSOHelper::CSSetShader(DeviceState::g_pDeviceContext, nullptr);

    DeviceState::g_pDeviceContext->RSSetState(nullptr);
    DeviceState::g_pDeviceContext->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    DeviceState::g_pDeviceContext->OMSetDepthStencilState(nullptr, 0);
}
#endif // !DYNAMICCPP_EXPORTS