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


//cascade마다 값 나눠주기
std::vector<float>      devideCascadeEnd(Camera& camera, std::vector<float> ratios);
std::vector<ShadowInfo> devideShadowInfo(Camera& camera, std::vector<float> cascadeEnd, Mathf::Vector4 LightDir);

class ShadowMapPass final : public IRenderPass
{
public:
	ShadowMapPass();
	~ShadowMapPass() {};

	void Initialize(uint32 width, uint32 height);
	void Execute(RenderScene& scene, Camera& camera) override;
	void ControlPanel() override;
	Camera m_shadowCamera{};

	std::unique_ptr<Texture> m_shadowMapTexture{};
	ID3D11DepthStencilView* m_shadowMapDSV{ nullptr };


	D3D11_VIEWPORT shadowViewport;
	ID3D11ShaderResourceView* shadowMapSRV = nullptr;

	//그림자 적용할 빛마다 3개 현재는 빛 1개에만 적용가능
	ID3D11DepthStencilView* m_shadowMapDSVarr[cascadeCount]{};

	ShadowMapConstant shadowMapConstant2;
};

