#include "Sampler.h"
#include "DeviceState.h"

Sampler::Sampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode)
{
	CD3D11_SAMPLER_DESC samplerDesc{ CD3D11_DEFAULT() };
	samplerDesc.Filter = filter;
	samplerDesc.AddressU = addressMode;
	samplerDesc.AddressV = addressMode;
	samplerDesc.AddressW = addressMode;

	DirectX11::CreateSamplerState(&samplerDesc,	&m_SamplerState);
	DirectX::SetName(m_SamplerState, GetAddressAsString() + " SamplerType : " + std::to_string(filter));
}

Sampler::~Sampler()
{
	Memory::SafeDelete(m_SamplerState);
}

void Sampler::Use(uint32 slot)
{
	DeviceState::g_pDeviceContext->PSSetSamplers(slot, 1, &m_SamplerState);
}

void Sampler::Use(ID3D11DeviceContext* defferedContext, uint32 slot)
{
	defferedContext->PSSetSamplers(slot, 1, &m_SamplerState);
}
