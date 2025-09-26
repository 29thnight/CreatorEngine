#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "../ScriptBinder/GameObject.h"

struct RenderPassSettings;
class ForwardPass final : public IRenderPass
{
public:
	ForwardPass();
	~ForwardPass();

	void UseEnvironmentMap(
		Managed::SharedPtr<Texture> envMap,
		Managed::SharedPtr<Texture> preFilter,
		Managed::SharedPtr<Texture> brdfLut);

	void Execute(RenderScene& scene, Camera& camera);
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void CreateFoliageCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
	void ControlPanel() override;

	void ApplySettings(const RenderPassSettings& setting);

	void SetTexture(Texture* normalTexture) { m_normalTexture = normalTexture; }
private:
	std::unique_ptr<PipelineStateObject> m_instancePSO;
	std::unique_ptr<PipelineStateObject> m_instanceFoliagePSO;
	Managed::WeakPtr<Texture> m_EnvironmentMap{};
	Managed::WeakPtr<Texture> m_PreFilter{};
	Managed::WeakPtr<Texture> m_BrdfLut{};

	uint32 m_maxInstanceCount{};
	bool m_UseEnvironmentMap{ true };
	float m_envMapIntensity{ 1.0f };

	ComPtr<ID3D11Buffer> m_Buffer{};
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
	ComPtr<ID3D11Buffer> m_instanceBuffer;
	ComPtr<ID3D11Buffer> m_TimeBuffer;
	ComPtr<ID3D11Buffer> m_windBuffer;
	ComPtr<ID3D11ShaderResourceView> m_instanceBufferSRV;
	ComPtr<ID3D11DepthStencilState> m_depthNoWrite;
	ComPtr<ID3D11BlendState1> m_blendPassState;

	Texture* m_CopiedTexture{};
	Texture* m_normalTexture{};
	ComPtr<ID3D11Buffer> m_MatrixBuffer{};

	float ior = 0.f;
	float add = 0.f;
private:
	Mathf::Vector3 m_windDirection{ 1.f, 0.f, 0.f };
	float m_windStrength{ 0.1f };
	float m_windSpeed{ 1.0f };
	float m_windWaveFrequency{ 0.2f };
};