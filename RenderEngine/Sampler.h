#pragma once
#include "Core.Minimal.h"

class Sampler
{
public:
	Sampler(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE addressMode);
	~Sampler();

	void Use(uint32 slot);
	void Use(ID3D11DeviceContext* defferedContext, uint32 slot);

	std::string GetAddressAsString() const {
		return std::to_string(reinterpret_cast<uintptr_t>(this));
	}

	ID3D11SamplerState* m_SamplerState;
};
