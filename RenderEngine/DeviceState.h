#pragma once
#include "Core.Minimal.h"

namespace DeviceState
{
	extern ID3D11Device3* g_pDevice;
	extern ID3D11DeviceContext3* g_pDeviceContext;
}

inline ID3D11Buffer* CreateBuffer(uint32 size, D3D11_BIND_FLAG bindFlag, const void* data = nullptr)
{
	CD3D11_BUFFER_DESC bufferDesc{
		size,
		(uint32)bindFlag
	};
	ID3D11Buffer* buffer{};
	if (data)
	{
		D3D11_SUBRESOURCE_DATA initData{};
		initData.pSysMem = data;
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateBuffer(
				&bufferDesc,
				&initData,
				&buffer
			)
		);
	}
	else
	{
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateBuffer(
				&bufferDesc,
				nullptr,
				&buffer
			)
		);
	}
	return buffer;
}