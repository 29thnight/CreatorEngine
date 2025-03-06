#include "GBufferPass.h"

GBufferPass::~GBufferPass()
{
}

void GBufferPass::SetRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size)
{
	for (uint32 i = 0; i < size; i++)
	{
		m_renderTargetViews[i] = renderTargetViews[i];
	}
}

void GBufferPass::Execute(Scene& scene)
{
	auto& deviceContext = DeviceState::g_pDeviceContext;
	for (auto& RTV : m_renderTargetViews)
	{
		deviceContext->ClearRenderTargetView(RTV, Colors::Transparent);
	}

	deviceContext->OMSetRenderTargets(RTV_TypeMax, m_renderTargetViews, nullptr);
}
