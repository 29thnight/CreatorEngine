#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class SSGIPass final : public IRenderPass
{
public:
	SSGIPass();
	~SSGIPass();
	
	void Initialize(Texture* diffuse, Texture* normal, Texture* lightEmissive);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	void Resize(uint32_t width, uint32_t height) override;

private:
	Texture* m_pDiffuseTexture{ nullptr };
	Texture* m_pNormalTexture{ nullptr };
	Texture* m_pLightEmissiveTexture{ nullptr };

	Texture* m_pTempTexture{ nullptr };
	ComPtr<ID3D11Buffer> m_Buffer;

	ComputeShader* m_pSSGIShader{};

	Sampler* sample = nullptr;
	Sampler* pointSample = nullptr;

	float radius = 0.131f;
	float thickness = 0.155f;
};

