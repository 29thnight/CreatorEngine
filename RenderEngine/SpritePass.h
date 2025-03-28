#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "Mesh.h"
#include "Camera.h"

class SpritePass final : public IRenderPass
{
public:
	SpritePass();
	~SpritePass();

	void Initialize(Texture* renderTarget);
	void Execute(RenderScene& scene, Camera& camera) override;

private:
	Texture* m_RenderTarget{};
	std::unique_ptr<Mesh> m_QuadMesh{};
	ComPtr<ID3D11DepthStencilState> m_NoWriteDepthStencilState{};
};