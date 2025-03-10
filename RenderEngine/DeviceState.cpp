#include "DeviceState.h"

ID3D11Device3* DeviceState::g_pDevice = nullptr;
ID3D11DeviceContext3* DeviceState::g_pDeviceContext = nullptr;
ID3D11DepthStencilView* DeviceState::g_pDepthStencilView = nullptr;
ID3D11DepthStencilState* DeviceState::g_pDepthStencilState = nullptr;
ID3D11RasterizerState* DeviceState::g_pRasterizerState = nullptr;
ID3D11BlendState* DeviceState::g_pBlendState = nullptr;
D3D11_VIEWPORT DeviceState::g_Viewport = {};
ID3D11RenderTargetView* DeviceState::g_backBufferRTV = nullptr;
ID3D11ShaderResourceView* DeviceState::g_depthStancilSRV = nullptr;
DirectX11::Sizef DeviceState::g_ClientRect = {};
float DeviceState::g_aspectRatio = 0.f;