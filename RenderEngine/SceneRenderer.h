#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"
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

#include "Model.h"
#include "Light.h"
#include "Camera.h"

const static float pi = XM_PIDIV2 - 0.01f;
const static float pi2 = XM_PI * 2.f;

struct EditorCamera
{
	bool isOrthographic = false;
	Camera* m_perspacetiveEditCamera{};
	Camera* m_orthographicEditCamera{};

	EditorCamera();
	~EditorCamera()
	{
		delete m_perspacetiveEditCamera;
		delete m_orthographicEditCamera;
	}

	bool ToggleCamera()
	{
		isOrthographic = !isOrthographic;
		return isOrthographic;
	}

	Camera* GetCamera()
	{
		return isOrthographic ? m_orthographicEditCamera : m_perspacetiveEditCamera;
	}
};

class Scene;
class SceneRenderer
{
public:
	SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);

	void Initialize(Scene* _pScene = nullptr);
	void Update(float deltaTime);
	void Render();

private:
	void InitializeTextures();
	void PrepareRender();
	void Clear(const float color[4], float depth, uint8_t stencil);
	void SetRenderTargets(Texture& texture, bool enableDepthTest = true);
	void UnbindRenderTargets();

	Scene* m_currentScene{};
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

	//buffers
	ComPtr<ID3D11Buffer> m_ModelBuffer;
	ComPtr<ID3D11Buffer> m_ViewBuffer;
	ComPtr<ID3D11Buffer> m_ProjBuffer;

	//textures
	std::unique_ptr<Texture> m_colorTexture;
	std::unique_ptr<Texture> m_editColorTexture;

	//game view
	std::unique_ptr<Texture> m_diffuseTexture;
	std::unique_ptr<Texture> m_metalRoughTexture;
	std::unique_ptr<Texture> m_normalTexture;
	std::unique_ptr<Texture> m_emissiveTexture;
	//editor view
	std::unique_ptr<Texture> m_editDiffuseTexture;
	std::unique_ptr<Texture> m_editMetalRoughTexture;
	std::unique_ptr<Texture> m_editNormalTexture;
	std::unique_ptr<Texture> m_editEmissiveTexture;

	std::unique_ptr<Texture> m_ambientOcclusionTexture;
	std::unique_ptr<Texture> m_toneMappedColourTexture;
    std::unique_ptr<Texture> m_gridTexture;

	Sampler* m_linearSampler{};
	Sampler* m_pointSampler{};

	EditorCamera m_editorCamera{};

	//render queue

	std::queue<SceneObject*> m_forwardQueue;

	Model* model{};

//Debug
public:
	void SetWireFrame() { useWireFrame = !useWireFrame; }
private:
	bool useWireFrame = false;

public:
	void EditorView();
	void EditTransform(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, SceneObject* obj, Camera* cam);
};
