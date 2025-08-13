#pragma once
#include "Core.Minimal.h"
#ifndef DYNAMICCPP_EXPORTS //PassData
#include "DeviceResources.h"
#include "ForwardPass.h"
#include "ShadowMapPass.h"
#include "GBufferPass.h"
#include "SSAOPass.h"
#include "DeferredPass.h"
#include "SkyBoxPass.h"
#include "ToneMapPass.h"
#include "SpritePass.h"
#include "BlitPass.h"
#include "WireFramePass.h"
#include "AAPass.h"
#include "PostProcessingPass.h"
//작업자 용우 관련
#include "ScreenSpaceReflectionPass.h"
#include "SubsurfaceScatteringPass.h"
#include "VignettePass.h"
#include "ColorGradingPass.h"
#include "VolumetricFogPass.h"
#include "RenderJob.h"
#include "UIPass.h"
#include "LightMap.h"
#include "PositionMapPass.h"
#include "LightMapPass.h"
#include "EffectManager.h"
#include "SSGIPass.h"
#include "BitMaskPass.h"
#include "TerrainGizmoPass.h"
#include "DecalPass.h"
//작업자 규철 관련
#include "EffectEditor.h"
#endif // !DYNAMICCPP_EXPORTS

#include "LightController.h"
#include "Camera.h"

class Scene;
class RenderPassWindow;
class SceneViewWindow;
class MenuBarWindow;
class GameViewWindow;
class HierarchyWindow;
class InspectorWindow;
class GizmoRenderer;
class SceneRenderer
{
private:
	friend class RenderPassWindow;
	friend class SceneViewWindow;
	friend class MenuBarWindow;
	friend class GameViewWindow;
	friend class HierarchyWindow;
	friend class InspectorWindow;
	friend class GizmoRenderer;
public:
	SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);
	~SceneRenderer();

	void Finalize();

	void NewCreateSceneInitialize();
	void OnWillRenderObject(float deltaTime);
	void EndOfFrame(float deltaTime);
	void PrepareRender();
	void SceneRendering();
	void CreateCommandListPass();
	void ReApplyCurrCubeMap();
	void ApplyVolumeProfile();

private:
	void InitializeDeviceState();
	void InitializeShadowMapDesc();
	void InitializeTextures();
	void Clear(const float color[4], float depth, uint8_t stencil);
	void SetRenderTargets(Texture& texture, bool enableDepthTest = true);
	void ApplyNewCubeMap(std::string_view filename);
	void ApplyNewColorGrading(std::string_view filename);
	void UnbindRenderTargets();
	void ReloadShaders();
	void ResourceTrim();

	std::shared_ptr<RenderScene>				m_renderScene{};

#ifndef DYNAMICCPP_EXPORTS //PassData
	//device
	std::shared_ptr<DirectX11::DeviceResources> m_deviceResources{};
	//DelegateHandle
    Core::DelegateHandle						m_newSceneCreatedEventHandle{};
	Core::DelegateHandle						m_activeSceneChangedEventHandle{};
	Core::DelegateHandle						m_resizeEventHandle{};
	Core::DelegateHandle						m_trimEventHandle{};
	Core::DelegateHandle                        m_volumeProfileApplyEventHandle{};
	//pass
	std::unique_ptr<ShadowMapPass>				m_pShadowMapPass{};
	std::unique_ptr<GBufferPass>				m_pGBufferPass{};
	std::unique_ptr<SSAOPass>					m_pSSAOPass{};
	std::unique_ptr<DeferredPass>				m_pDeferredPass{};
	std::unique_ptr<ForwardPass>				m_pForwardPass{};
	std::unique_ptr<SkyBoxPass>					m_pSkyBoxPass{};
	std::unique_ptr<ToneMapPass>				m_pToneMapPass{};
	std::unique_ptr<SpritePass>					m_pSpritePass{};
	std::unique_ptr<BlitPass>					m_pBlitPass{};
	std::unique_ptr<AAPass>						m_pAAPass{};
	std::unique_ptr<PostProcessingPass>			m_pPostProcessingPass{};
	std::unique_ptr<EffectEditor>				m_EffectEditor;

	std::unique_ptr<PositionMapPass>			m_pPositionMapPass{};
	std::unique_ptr<LightMapPass>				m_pLightMapPass{};
	std::unique_ptr<ScreenSpaceReflectionPass>	m_pScreenSpaceReflectionPass{};
	std::unique_ptr<SubsurfaceScatteringPass>	m_pSubsurfaceScatteringPass{};
	std::unique_ptr<VignettePass>				m_pVignettePass{};
	std::unique_ptr<ColorGradingPass>			m_pColorGradingPass{};
	std::unique_ptr<VolumetricFogPass>			m_pVolumetricFogPass{};

	std::unique_ptr<UIPass>						m_pUIPass{};
	std::unique_ptr<SSGIPass>					m_pSSGIPass{};
	std::unique_ptr<BitMaskPass>				m_pBitMaskPass{};
	std::unique_ptr<TerrainGizmoPass>			m_pTerrainGizmoPass{};
	std::unique_ptr<DecalPass>					m_pDecalPass{};

	//Resources
	//buffers
	ComPtr<ID3D11Buffer>						m_ModelBuffer;
	//Textures
	 Managed::SharedPtr<Texture>				m_diffuseTexture{};
	 Managed::SharedPtr<Texture>				m_metalRoughTexture{};
	 Managed::SharedPtr<Texture>				m_normalTexture{};
	 Managed::SharedPtr<Texture>				m_emissiveTexture{};
	 Managed::SharedPtr<Texture>				m_bitmaskTexture{};
	 Managed::SharedPtr<Texture>				m_ambientOcclusionTexture{};
	 Managed::SharedPtr<Texture>				m_toneMappedColourTexture{};
	 Managed::SharedPtr<Texture>				m_lightingTexture{};

	ThreadPool<std::function<void()>>*			m_threadPool = nullptr;
	std::unique_ptr<RenderThreadPool>			m_commandThreadPool = nullptr;
#endif // !DYNAMICCPP_EXPORTS
	//Editor Camera
	std::shared_ptr<Camera>						m_pEditorCamera{};

	lm::LightMap lightMap;
//Debug
public:
	void SetWireFrame()     { useWireFrame = !useWireFrame; }
	void SetLightmapPass()  { useTestLightmap = !useTestLightmap; }

private:
	std::string                                 m_currentSkyTextureName{};
    bool										useWireFrame		{ false };
	bool										m_bShowRenderState	{ false };
	std::atomic_bool							useTestLightmap		{ false };
	bool										m_bShowGridSettings	{ false };
};
