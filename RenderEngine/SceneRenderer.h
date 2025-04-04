#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "../Utility_Framework/DeviceResources.h"
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
#include "Light.h"
#include "Camera.h"
#include "UIsprite.h"
#include "UIPass.h"

#include "LightMap.h"
#include "LightmapShadowPass.h"
#include "PositionMapPass.h"
#include "NormalMapPass.h"

const static float pi = XM_PIDIV2 - 0.01f;
const static float pi2 = XM_PI * 2.f;

class Scene;
class SceneRenderer
{
public:
	SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);

	void Initialize(Scene* _pScene = nullptr);
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

	Scene* m_currentScene{};
	RenderScene* m_renderScene{};
	std::shared_ptr<DirectX11::DeviceResources> m_deviceResources{};

	ID3D11DepthStencilView* m_depthStencilView{};
	ID3D11ShaderResourceView* m_depthStencilSRV{};

	//pass
	std::unique_ptr<ShadowMapPass> m_pShadowMapPass{};
	std::unique_ptr<GBufferPass> m_pGBufferPass{};
	std::unique_ptr<SSAOPass> m_pSSAOPass{};
	std::unique_ptr<DeferredPass> m_pDeferredPass{};
	std::unique_ptr<SkyBoxPass> m_pSkyBoxPass{};
	std::unique_ptr<ToneMapPass> m_pToneMapPass{};
	std::unique_ptr<SpritePass> m_pSpritePass{};
	std::unique_ptr<BlitPass> m_pBlitPass{};
	std::unique_ptr<WireFramePass> m_pWireFramePass{};
    std::unique_ptr<GridPass> m_pGridPass{};
	std::unique_ptr<AAPass> m_pAAPass{};
	std::unique_ptr<PostProcessingPass> m_pPostProcessingPass{};

	std::unique_ptr<LightmapShadowPass> m_pLightmapShadowPass{};
	std::unique_ptr<PositionMapPass> m_pPositionMapPass{};
	std::unique_ptr<NormalMapPass> m_pNormalMapPass{};

	std::unique_ptr<UIPass> m_pUIPass{};
	//buffers
	ComPtr<ID3D11Buffer> m_ModelBuffer;

	//Textures
	std::unique_ptr<Texture> m_diffuseTexture;
	std::unique_ptr<Texture> m_metalRoughTexture;
	std::unique_ptr<Texture> m_normalTexture;
	std::unique_ptr<Texture> m_emissiveTexture;
	std::unique_ptr<Texture> m_ambientOcclusionTexture;
	std::unique_ptr<Texture> m_toneMappedColourTexture;
    std::unique_ptr<Texture> m_gridTexture;

	//sampler
	Sampler* m_linearSampler{};
	Sampler* m_pointSampler{};

	//Editor Camera
	std::unique_ptr<Camera> m_pEditorCamera{};

	//render queue
	std::queue<GameObject*> m_forwardQueue;

	Model* model{};
	Model* testModel{};
	UIsprite testUI{};
	UIsprite testUI2{};

	lm::LightMap lightMap;
//Debug
public:
	void SetWireFrame() { useWireFrame = !useWireFrame; }
private:
	int selected_log_index{};
	bool useWireFrame = false;
	bool m_bIsClicked{ false };
	bool m_bShowLogWindow{ false };

public:
	void EditorView();
	void ShowLogWindow();
	void EditTransform(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, GameObject* obj, Camera* cam);
};
