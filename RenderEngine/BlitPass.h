#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class BlitPass final : public IRenderPass
{
public:
	BlitPass();
	~BlitPass();
	void Initialize(ID3D11RenderTargetView* backBufferRTV);
	void Execute(RenderScene& scene, Camera& camera) override;

private:
	ID3D11RenderTargetView* m_backBufferRTV{};
};