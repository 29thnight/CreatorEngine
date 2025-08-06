#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "../ScriptBinder/GameObject.h"

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
private:
	std::unique_ptr<PipelineStateObject> m_instancePSO;
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
	ComPtr<ID3D11ShaderResourceView> m_instanceBufferSRV;
};