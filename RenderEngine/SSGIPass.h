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
	Texture* m_pTempTexture2{ nullptr };

	Texture* m_pTempTexture3{ nullptr };

	ComPtr<ID3D11Buffer> m_SSGIBuffer;
	ComPtr<ID3D11Buffer> m_CompositeBuffer;

	ComputeShader* m_pSSGIShader{};
	ComputeShader* m_pCompositeShader{};

	ComputeShader* m_pDownDualFilteringShader{};
	ComputeShader* m_pUpDualFilteringShaeder{};

	Sampler* sample = nullptr;
	Sampler* pointSample = nullptr;

	float radius = 4.f;//0.131f;
	float thickness = 0.5f;//0.155f;

	float intensity = 1.f;
	bool useOnlySSGI = false;
	int useDualFilteringStep = 2;

	bool isOn = true;
};

