#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"
#include "Delegate.h"
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

#include "Model.h"
#include "LightController.h"
#include "Camera.h"
#include "UIPass.h"

#include "LightMap.h"
#include "LightmapShadowPass.h"
#include "PositionMapPass.h"
#include "LightMapPass.h"
#include "EffectManager.h"

const static float pi = XM_PIDIV2 - 0.01f;
const static float pi2 = XM_PI * 2.f;

class Scene;
class RenderPassWindow;
class SceneViewWindow;
class MenuBarWindow;
class GameViewWindow;
class HierarchyWindow;
class InspectorWindow;
class SceneRenderer
{
private:
	friend class RenderPassWindow;
	friend class SceneViewWindow;
	friend class MenuBarWindow;
	friend class GameViewWindow;
	friend class HierarchyWindow;
	friend class InspectorWindow;
public:
	SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);

	void NewCreateSceneInitialize();
	void OnWillRenderObject(float deltaTime);
	void SceneRendering();

private:
	void InitializeDeviceState();
	void InitializeImGui();
	void InitializeTextures();
	void PrepareRender();
	void Clear(const float color[4], float depth, uint8_t stencil);
	void SetRenderTargets(Texture& texture, bool enableDepthTest = true);
	void UnbindRenderTargets();
	void ReloadShaders();

	std::shared_ptr<DirectX11::DeviceResources> m_deviceResources{};

	RenderScene* m_renderScene{};

	ID3D11DepthStencilView*     m_depthStencilView{};
	ID3D11ShaderResourceView*   m_depthStencilSRV{};

    Core::DelegateHandle m_newSceneCreatedEventHandle{};

	//pass
	std::unique_ptr<ShadowMapPass>      m_pShadowMapPass{};
	std::unique_ptr<GBufferPass>        m_pGBufferPass{};
	std::unique_ptr<SSAOPass>           m_pSSAOPass{};
	std::unique_ptr<DeferredPass>       m_pDeferredPass{};
	std::unique_ptr<ForwardPass>        m_pForwardPass{};
	std::unique_ptr<SkyBoxPass>         m_pSkyBoxPass{};
	std::unique_ptr<ToneMapPass>        m_pToneMapPass{};
	std::unique_ptr<SpritePass>         m_pSpritePass{};
	std::unique_ptr<BlitPass>           m_pBlitPass{};
	std::unique_ptr<WireFramePass>      m_pWireFramePass{};
    std::unique_ptr<GridPass>           m_pGridPass{};
	std::unique_ptr<AAPass>             m_pAAPass{};
	std::unique_ptr<PostProcessingPass> m_pPostProcessingPass{};
	std::unique_ptr<EffectManager>      m_pEffectPass{};

	std::unique_ptr<LightmapShadowPass> m_pLightmapShadowPass{};
	std::unique_ptr<PositionMapPass>    m_pPositionMapPass{};
	std::unique_ptr<LightMapPass>       m_pLightMapPass{};

	std::unique_ptr<UIPass>             m_pUIPass{};
	//buffers
	ComPtr<ID3D11Buffer>				m_ModelBuffer;

	//Textures
	UniqueTexturePtr m_diffuseTexture          { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_metalRoughTexture       { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_normalTexture           { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_emissiveTexture         { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_ambientOcclusionTexture { TEXTURE_NULL_INITIALIZER };
	UniqueTexturePtr m_toneMappedColourTexture { TEXTURE_NULL_INITIALIZER };
    UniqueTexturePtr m_gridTexture             { TEXTURE_NULL_INITIALIZER };

	//sampler
	Sampler* m_linearSampler{};
	Sampler* m_pointSampler{};

	//Editor Camera
	std::unique_ptr<Camera> m_pEditorCamera{};

	lm::LightMap lightMap;

	std::shared_ptr<SpriteBatch> m_spriteBatch = nullptr;
//Debug
public:
	void SetWireFrame()     { useWireFrame = !useWireFrame; }
	void SetLightmapPass()  { useTestLightmap = !useTestLightmap; }

private:
	int  selected_log_index{};

private:
    bool useWireFrame       { false };
	bool m_bIsClicked       { false };
	bool m_bShowLogWindow   { false };
	bool m_bShowRenderState { false };
	bool useTestLightmap    { false };
	bool m_bShowGridSettings{ false };

public:
	void EditorView();
	void ShowLogWindow();
	void ShowGridSettings();
};
