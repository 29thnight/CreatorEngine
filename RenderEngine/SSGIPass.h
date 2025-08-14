#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct SSGIPassSetting;
class SSGIPass final : public IRenderPass
{
public:
	SSGIPass();
	~SSGIPass();
	
	void Initialize(Texture* diffuse, Texture* normal, Texture* lightEmissive, Texture* metalroughocclu, Texture* SSAO);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
        void ApplySettings(const SSGIPassSetting& setting);
	void Resize(uint32_t width, uint32_t height) override;

private:
	Texture* m_pDiffuseTexture{ nullptr };
	Texture* m_pNormalTexture{ nullptr };
	Texture* m_pLightEmissiveTexture{ nullptr };
	Texture* m_pMetalRoughOcclu{ nullptr };
	Texture* m_pSSAOTexture{ nullptr };

	Texture* m_pTempTexture{ nullptr };
	Texture* m_pTempTexture2{ nullptr };
	Texture* m_pTempTexture3{ nullptr };

	//Texture* m_pBilateralTexture{ nullptr };

	ComPtr<ID3D11Buffer> m_SSGIBuffer;
	ComPtr<ID3D11Buffer> m_CompositeBuffer;
	//ComPtr<ID3D11Buffer> m_BilateralBuffer;

	ComputeShader* m_pSSGIShader{};
	ComputeShader* m_pCompositeShader{};

	ComputeShader* m_pDownDualFilteringShader{};
	ComputeShader* m_pUpDualFilteringShaeder{};

	//ComputeShader* m_pBilateralFilterShader{};

	Sampler* sample = nullptr;
	Sampler* pointSample = nullptr;

	float radius = 4.f;//0.131f;
	float thickness = 0.5f;//0.155f;

	float intensity = 1.f;
	bool useOnlySSGI = false;
	int useDualFilteringStep = 2;
	bool isOn = true;

	//bool useBilateralFiltering = true;
	//float sigmaSpace = 0.1f;
	//float sigmaRange = 0.1f;
};

