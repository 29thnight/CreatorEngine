#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "../ScriptBinder/GameObject.h"

class ForwardPass final : public IRenderPass
{
public:
	ForwardPass();
	~ForwardPass();

	void UseEnvironmentMap(Texture* envMap, Texture* preFilter, Texture* brdfLut);

	void Execute(RenderScene& scene, Camera& camera);
	void CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
private:
	Texture* m_EnvironmentMap{};
	Texture* m_PreFilter{};
	Texture* m_BrdfLut{};

	bool m_UseEnvironmentMap{ true };
	float m_envMapIntensity{ 0.2f };

	ComPtr<ID3D11Buffer> m_Buffer{};
	ComPtr<ID3D11Buffer> m_materialBuffer;
	ComPtr<ID3D11Buffer> m_boneBuffer;
};