#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"
#include "ShadowMapPass.h"
#include "Light.h"

class Scene;
class SceneRenderer
{
public:
	SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);

	void Initialize(Scene* _pScene = nullptr);
	void Render();

private:
	Scene* m_currentScene{};
	std::shared_ptr<DirectX11::DeviceResources> m_deviceResources{};
	std::unique_ptr<LightController> m_lightController{};
	std::unique_ptr<ShadowMapPass> m_pShadowMapPass{};

	ID3D11Buffer* m_ModelBuffer;
	ID3D11Buffer* m_ViewBuffer;
	ID3D11Buffer* m_ProjBuffer;
};