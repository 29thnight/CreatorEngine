#pragma once
#include "IRenderPass.h"
#include "Texture.h"
#include "LightProperty.h"

constexpr int cascadeCount = 3;

cbuffer CascadeIndexBuffer
{
	uint32_t cascadeIndex;
	uint32_t padding[3]; // 16바이트 정렬
};

class Camera;
class Texture;
class Scene;
class LightController;
struct ShadowMapPassSetting;
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

public:
	std::unique_ptr<PipelineStateObject>	m_instancePSO;
	ComPtr<ID3D11Buffer>					m_boneBuffer;
	ComPtr<ID3D11Buffer>					m_cascadeIndexBuffer;
	D3D11_VIEWPORT							shadowViewport;
	ShadowMapConstant						m_settingConstant;
	FrustumContainer						sliceFrustums;

	Managed::UniquePtr<Texture>				m_cloudShadowMapTexture{ nullptr };
	ID3D11Buffer*							m_cloudShadowMapBuffer{ nullptr };

	uint32 m_maxInstanceCount{};
	size_t m_cascadeRatioSize{ 2 };
	bool m_useCascade{ true };

	Mathf::Vector2 cloudSize = { 4,4 };
	Mathf::Vector2 cloudDirection = { 1,1 };
	float cloudMoveSpeed = 0.0003f;
	float cloudAlpha = 1.f;
	bool isCloudOn = true;

	void DevideCascadeEnd(Camera& camera);
	void DevideShadowInfo(Camera& camera, Mathf::Vector4 LightDir);


	void UseCloudShadowMap(std::string_view filename);
	void UpdateCloudBuffer(ID3D11DeviceContext* defferdContext, LightController* lightcontroller);
	void PSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn = true);
	void CSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn = true);

private:
	void CreateCommandListCascadeShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
	void CreateCommandListNormalShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
	void CreateCommandListProxyToShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);

	void CreateTerrainRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
};

