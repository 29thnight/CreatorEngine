#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "IRenderPass.h"
#include "Camera.h"
#include "LightProperty.h"
class Texture;
class Scene;
class LightController;

struct ShadowMapPassSetting;
constexpr int cascadeCount = 3;

class ShadowMapPass final : public IRenderPass
{
public:
	using FrustumContainer = std::array<std::array<Mathf::Vector3, 8>, cascadeCount>;

public:
	ShadowMapPass();
	~ShadowMapPass();

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
    void ApplySettings(const ShadowMapPassSetting& setting);
	virtual void Resize(uint32_t width, uint32_t height) override;
	ComPtr<ID3D11Buffer>		m_boneBuffer;
	D3D11_VIEWPORT				shadowViewport;
	ShadowMapConstant			m_settingConstant;
	FrustumContainer			sliceFrustums;

	UniqueTexturePtr				m_cloudShadowMapTexture{ nullptr };
	ID3D11Buffer*					m_cloudShadowMapBuffer{ nullptr };

	size_t m_cascadeRatioSize{ 2 };
	bool m_useCascade{ true };

	Mathf::Vector2 cloudSize = { 4,4 };
	Mathf::Vector2 cloudDirection = { 1,1 };
	float cloudMoveSpeed = 0.0003f;
	bool isCloudOn = true;

	void DevideCascadeEnd(Camera& camera);
	void DevideShadowInfo(Camera& camera, Mathf::Vector4 LightDir);


	void UseCloudShadowMap(const std::string_view& filename);
	void UpdateCloudBuffer(ID3D11DeviceContext* defferdContext, LightController* lightcontroller);
	void PSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn = true);
	void CSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn = true);

private:
	void CreateCommandListCascadeShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
	void CreateCommandListNormalShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
	void CreateCommandListProxyToShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);

	void CreateTerrainRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
};

