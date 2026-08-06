#include "Sampler.h"
#include "DeviceState.h"
#include "Core.Memory.hpp"

Sampler::Sampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode)
	: Sampler(filter, addressMode, addressMode, addressMode)
{
}

Sampler::Sampler(D3D11_FILTER filter,
	D3D11_TEXTURE_ADDRESS_MODE addressU,
	D3D11_TEXTURE_ADDRESS_MODE addressV,
	D3D11_TEXTURE_ADDRESS_MODE addressW)
{
	CD3D11_SAMPLER_DESC samplerDesc{ CD3D11_DEFAULT() };
	samplerDesc.Filter = filter;
	samplerDesc.AddressU = addressU;
	samplerDesc.AddressV = addressV;
	samplerDesc.AddressW = addressW;
	samplerDesc.MaxAnisotropy = 16u;

	DirectX11::CreateSamplerState(&samplerDesc,	&m_SamplerState);
	DirectX::SetName(m_SamplerState, GetAddressAsString() + " SamplerType : " + std::to_string(filter));
}

Sampler::~Sampler()
{
	Memory::SafeDelete(m_SamplerState);
}

void Sampler::Use(uint32_t slot)
{
	DirectX11::DeviceStates->g_pDeviceContext->PSSetSamplers(slot, 1, &m_SamplerState);
}

void Sampler::Use(ID3D11DeviceContext* deferredContext, uint32_t slot)
{
	deferredContext->PSSetSamplers(slot, 1, &m_SamplerState);
}
