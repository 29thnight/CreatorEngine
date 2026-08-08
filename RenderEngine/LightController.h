#pragma once
// 광원 상태 보관소. DX11 ShadowMapPass를 소유하던 시절의 그림자·구름 API는
// 그 패스와 함께 걷어냈다 — 그림자는 DX12 EnhancedShadowPass가 그린다.
// 여기 남은 것은 광원 목록과 그 속성뿐이고, 그것은 렌더러와 무관하게 씬의
// 상태다(Scene::UpdateLight가 채우고 DX12 라이브가 프레임마다 밀봉해 간다).
#include "Interfaces/LightProperty.h"

class Texture;
class Scene;
class Camera;
class RenderScene;
class LightController
{
public:
	LightController() = default;
	~LightController();

public:
	void Initialize();
	void Update();
	Light& GetLight(uint32 index);
	Mathf::Vector4 GetEyePosition();
	LightController& AddLight(Light& light);
	LightController& SetGlobalAmbient(Mathf::Color4 color);
	LightController& SetEyePosition(Mathf::xVector eyePosition);
	/// 이 광원이 그림자를 드리운다고 표시한다. 예전에는 여기서 ShadowMapPass를
	/// 세우고 캐스케이드 규격까지 정했는데, 그리는 쪽이 DX12로 옮겨 간 뒤로는
	/// 표시와 desc 보관만 남는다 — EnhancedShadowPass가 자기 캐스케이드를
	/// 스스로 정하므로 desc의 해상도 필드는 지금 소비자가 없다.
	void SetLightWithShadows(uint32 index, ShadowMapRenderDesc& desc);

	const LightProperties& GetProperties() { return m_lightProperties; }

	uint32 m_lightCount{ 0 };

private:
	friend class RenderScene;

	ShadowMapRenderDesc				m_shadowMapRenderDesc;
	ShadowMapConstant				m_shadowMapConstant;
	LightProperties					m_lightProperties;
	LightProperties                 m_lightPropertiesArr[3];
    LightCount						m_lightCountStruct;
	bool							hasLightWithShadows{ false };
	uint32                          m_frameCount{ 0 };
};
