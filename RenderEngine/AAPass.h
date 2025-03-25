#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class Camera;
class AAPass final : public IRenderPass
{
public:
	AAPass();
	~AAPass();
	void Initialize(Texture* renderTarget, ID3D11ShaderResourceView* depth, Texture* normal);
	void Execute(Scene& scene, Camera& camera) override;

private:
	Texture* m_prevFrameTexture;
	Texture* m_RenderTarget;
};