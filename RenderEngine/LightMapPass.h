#pragma once
#include "IRenderPass.h"
#include "Texture.h"
class LightMapPass final : public IRenderPass
{
public:
	LightMapPass();

	void Initialize(std::vector<Texture*>& lightmaps);
	void Execute(RenderScene& scene, Camera& camera) override;
	virtual void Resize() override;
private:
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
	ComPtr<ID3D11Buffer> m_cbuffer;

	std::vector<Texture*>* m_plightmaps{};
};

