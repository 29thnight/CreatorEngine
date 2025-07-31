#pragma once
#include "Core.Minimal.h"
#include "Delegate.h"
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
#include "GridPass.h"
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
//작업자 규철 관련
#include "EffectEditor.h"
#endif // !DYNAMICCPP_EXPORTS

#include "Model.h"
#include "LightController.h"
#include "Camera.h"

const static float pi = XM_PIDIV2 - 0.01f;
const static float pi2 = XM_PI * 2.f;

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
	void ApplyNewCubeMap(const std::string_view& filename);
	void UnbindRenderTargets();
	void ReloadShaders();
	void ResourceTrim();

	std::shared_ptr<RenderScene> m_renderScene{};

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

	//Resources
	//buffers
	ComPtr<ID3D11Buffer>						m_ModelBuffer;
	//Textures
	UniqueTexturePtr m_diffuseTexture          { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_metalRoughTexture       { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_normalTexture           { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_emissiveTexture         { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_bitmaskTexture		   { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_ambientOcclusionTexture { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_toneMappedColourTexture { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_lightingTexture		   { TEXTURE_NULL_INITIALIZER };

	std::shared_ptr<SpriteBatch> m_spriteBatch = nullptr;
	ThreadPool<std::function<void()>>* m_threadPool = nullptr;
	std::unique_ptr<RenderThreadPool> m_commandThreadPool = nullptr;
#endif // !DYNAMICCPP_EXPORTS
	//Editor Camera
	std::shared_ptr<Camera> m_pEditorCamera{};

	lm::LightMap lightMap;
//Debug
public:
	void SetWireFrame()     { useWireFrame = !useWireFrame; }
	void SetLightmapPass()  { useTestLightmap = !useTestLightmap; }

private:
    bool useWireFrame       { false };
	bool m_bShowRenderState { false };
	std::atomic_bool useTestLightmap{ false };
	bool m_bShowGridSettings{ false };
};
