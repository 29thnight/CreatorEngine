#pragma once
#include <d3d11.h>
#include <string>

class Sampler
{
public:
	Sampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode);
	~Sampler();

	void Use(uint32_t slot);
	void Use(ID3D11DeviceContext* deferredContext, uint32_t slot);

	std::string GetAddressAsString() const {
		return std::to_string(reinterpret_cast<uintptr_t>(this));
	}

	ID3D11SamplerState* m_SamplerState;
};
