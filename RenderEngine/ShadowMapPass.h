#pragma once
#include "IRenderPass.h"
#include "Texture.h"

constexpr int cascadeCount = 3;

cbuffer CascadeIndexBuffer
{
	uint32_t cascadeIndex;
	uint32_t padding[3]; // 16����Ʈ ����
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
	void CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera) override;
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

	// 캐스케이드 분할과 그림자 정보를 계산한다.
	//
	// 결과는 카메라가 아니라 RenderPassData(렌더 측 소유)에 쓴다. 카메라는
	// 게임 스레드 소유라 렌더 스레드가 쓰면 안 된다 — 예전에는 여기서
	// camera.m_cascadeEnd를 매 프레임 재할당했다(PHASE 3-2).
	void DevideCascadeEnd(Camera& camera, RenderPassData& renderData);
	void DevideShadowInfo(Camera& camera, RenderPassData& renderData, Mathf::Vector4 LightDir);


	void UseCloudShadowMap(std::string_view filename);
	void UpdateCloudBuffer(ID3D11DeviceContext* defferdContext, LightController* lightcontroller);
	void PSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn = true);
	void CSBindCloudShadowMap(ID3D11DeviceContext* defferdContext, LightController* lightcontroller, bool isOn = true);

private:
	void CreateCommandListCascadeShadow(RHICommandContext& context, RenderScene& scene, Camera& camera);
	void CreateCommandListNormalShadow(RHICommandContext& context, RenderScene& scene, Camera& camera);
	void CreateCommandListProxyToShadow(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);

	void CreateTerrainRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera);
};

