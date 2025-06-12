#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "IRenderPass.h"
#include "Camera.h"
#include "LightProperty.h"
class Texture;
class Scene;
class LightController;

constexpr int cascadeCount = 3;

class ShadowMapPass final : public IRenderPass
{
public:
	using FrustumContainer = std::array<std::array<Mathf::Vector3, 8>, cascadeCount>;

public:
	ShadowMapPass();
	~ShadowMapPass() {};

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	virtual void Resize(uint32_t width, uint32_t height) override;

	ComPtr<ID3D11Buffer>		m_boneBuffer;
	D3D11_VIEWPORT				shadowViewport;
	ShadowMapConstant			m_settingConstant;
	FrustumContainer			sliceFrustums;

	size_t m_cascadeRatioSize{ 2 };
	bool m_useCascade{ true };

	void DevideCascadeEnd(Camera& camera);
	void DevideShadowInfo(Camera& camera, Mathf::Vector4 LightDir);

private:
	void CreateCommandListCascadeShadow(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera);
	void CreateCommandListNormalShadow(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera);
	void CreateCommandListProxyToShadow(ID3D11DeviceContext* defferdContext, RenderScene& scene, Camera& camera);

};

