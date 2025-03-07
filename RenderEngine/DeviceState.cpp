#include "DeviceState.h"

ID3D11Device3* DeviceState::g_pDevice = nullptr;
ID3D11DeviceContext3* DeviceState::g_pDeviceContext = nullptr;
ID3D11DepthStencilView* DeviceState::g_pDepthStencilView = nullptr;
DirectX11::Sizef DeviceState::g_ClientRect = {};
float DeviceState::g_aspectRatio = 0.f;