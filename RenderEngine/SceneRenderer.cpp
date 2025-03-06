#include "SceneRenderer.h"
#include "DeviceState.h"
#include "Scene.h"

SceneRenderer::SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
	DeviceState::g_pDevice = m_deviceResources->GetD3DDevice();
	DeviceState::g_pDeviceContext = m_deviceResources->GetD3DDeviceContext();

	m_lightController = std::make_unique<LightController>();
	m_pShadowMapPass = std::make_unique<ShadowMapPass>(m_lightController.get());
}

void SceneRenderer::Initialize(Scene* _pScene)
{
	m_currentScene = _pScene;
	m_lightController->Initialize();

	if (!_pScene)
	{
		//m_currentScene = new Scene();
	}
	else
	{
		//m_currentScene = _pScene;
	}
}

void SceneRenderer::Render()
{
}
