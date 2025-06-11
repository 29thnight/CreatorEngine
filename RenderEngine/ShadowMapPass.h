#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "IRenderPass.h"
#include "Camera.h"
#include "LightProperty.h"
class Texture;
class Scene;
class LightController;

constexpr int cascadeCount = 3;

struct ShadowInfo
{
	Mathf::xVector m_eyePosition{};
	Mathf::xVector m_lookAt{};
	float m_nearPlane{};
	float m_farPlane{};
	float m_viewWidth{};
	float m_viewHeight{};
	Mathf::xMatrix m_lightViewProjection{};
};

class ShadowMapPass final : public IRenderPass
{
public:
	using FrustumContainer = std::array<std::array<Mathf::Vector3, 8>, cascadeCount>;

public:
	ShadowMapPass();
	~ShadowMapPass() {};

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void CreateRenderCommandList(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	virtual void Resize(uint32_t width, uint32_t height) override;

	Camera m_shadowCamera;

	//UniqueTexturePtr			m_shadowMapTexture{ TEXTURE_NULL_INITIALIZER };
	//ID3D11ShaderResourceView*	sliceSRV[3]{};
	//ID3D11DepthStencilView*		m_shadowMapDSVarr[cascadeCount]{};
	ComPtr<ID3D11Buffer>		m_boneBuffer;
	D3D11_VIEWPORT				shadowViewport;
	ShadowMapConstant			m_settingConstant;
	FrustumContainer			sliceFrustums;

	std::vector<float>			m_cascadeDevideRatios = { 0.15f, 0.5f };
	std::vector<float>			m_cascadeEnd;
	std::vector<ShadowInfo>		m_cascadeinfo;

	ThreadPool* m_threadPool{ nullptr };

	ComPtr<ID3D11DeviceContext> defferdContext1{ nullptr };
	ComPtr<ID3D11DeviceContext> defferdContext2{ nullptr };

	size_t m_cascadeRatioSize{ 2 };
	bool m_useCascade{ true };

	void DevideCascadeEnd(Camera& camera);
	void DevideShadowInfo(Camera& camera, Mathf::Vector4 LightDir);

private:
	void CreateCommandListCascadeShadow(RenderScene& scene, Camera& camera);
	void CreateCommandListNormalShadow(RenderScene& scene, Camera& camera);
	void CreateCommandListProxyToShadow(RenderScene& scene, Camera& camera);

};

