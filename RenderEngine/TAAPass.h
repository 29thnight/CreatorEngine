#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class TAAPass final : public IRenderPass
{
public:
	TAAPass();
	~TAAPass();
	void Initialize(Texture* renderTarget, ID3D11ShaderResourceView* depth, Texture* normal);
	void Execute(Scene& scene) override;

private:
	Texture* m_prevFrameTexture;
	Texture* m_RenderTarget;
};