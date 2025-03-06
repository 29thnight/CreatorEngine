#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class GBufferPass final : public IRenderPass
{
public:
	GBufferPass() = default;
	~GBufferPass();

	void Initialize(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);
	void SetRenderTargetViews(ID3D11RenderTargetView** renderTargetViews, uint32 size);
	void Execute(Scene& scene) override;

private:
	ID3D11Buffer* m_buffer{};
	ID3D11Buffer* m_boneBuffer{};
	ID3D11RenderTargetView* m_renderTargetViews[RTV_TypeMax]{};
};