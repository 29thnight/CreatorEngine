#pragma once
#include "IRenderPass.h"
#include "Texture.h"
class LightMapPass final : public IRenderPass
{
public:
	LightMapPass();

	void Initialize(std::vector<Texture*>& lightmaps, std::vector<Texture*>& directionalmaps);
	void Execute(RenderScene& scene, Camera& camera) override;
	virtual void Resize(uint32_t width, uint32_t height) override;
private:
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
	ComPtr<ID3D11Buffer> m_cbuffer;

	std::vector<Texture*>* m_plightmaps{};
	std::vector<Texture*>* m_pDirectionalMaps{};
};

