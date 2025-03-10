#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class BlitPass final : public IRenderPass
{
public:
	BlitPass();
	~BlitPass();
	void Initialize(Texture* src, ID3D11RenderTargetView* backBufferRTV);
	void Execute(Scene& scene) override;

private:
	Texture* m_srcTexture{};
	ID3D11RenderTargetView* m_backBufferRTV{};
};