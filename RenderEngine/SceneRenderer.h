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
//�۾��� ��� ����
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
//�۾��� ��ö ����
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

	// 진단/계측용 읽기 전용 접근자.
	// 소유권을 넘기지 않으므로 friend 선언보다 결합이 약하다.
	RenderScene* GetRenderScene() const { return m_renderScene.get(); }
#ifndef DYNAMICCPP_EXPORTS
	SkyBoxPass* GetSkyBoxPass() const;
#endif

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
	// 현재 살아 있는 렌더러. 대조 검증이 DX11 결과를 찾아가는 통로다.
	//
	// 싱글턴을 새로 만들지 않고 '가장 최근에 생성된 것'을 가리키기만 한다 —
	// 엔진에 SceneRenderer는 하나뿐이지만, 그것을 규칙으로 못 박으면 나중에
	// 둘을 띄우려 할 때 이 포인터가 아니라 설계가 막게 된다. 소멸자에서
	// 자기 자신일 때만 지운다.
	static SceneRenderer* GetActive() { return s_active; }

	// DX12 이식의 정확성 대조용(PHASE 3-6). DX11이 그린 GBuffer를 밖에서
	// 읽을 수 있어야 '같은 씬을 같은 그림으로 그리는가'를 물을 수 있다.
	//
	// 소유권을 넘기지 않는다 — 대조는 읽기만 한다. 여기로 얻은 텍스처에
	// 쓰기 시작하면 렌더러가 모르는 사이에 프레임 결과가 바뀐다.
	//
	// bitmask를 고른 이유: R32_UINT이고 그려진 곳에만 값이 들어가 커버리지를
	// 그대로 읽을 수 있다. 색 타깃은 셰이더가 다르면 값도 다르지만, '어느
	// 픽셀이 그려졌는가'는 셰이더와 무관하게 기하·변환·컬링이 정한다.
	const Texture* GetGBufferBitmaskTexture() const { return m_bitmaskTexture.get(); }
	const Texture* GetGBufferDiffuseTexture() const { return m_diffuseTexture.get(); }

	// ── GBuffer 캡처 ──
	//
	// 텍스처 포인터를 넘겨 주는 것만으로는 부족했다. DX11 즉시 컨텍스트는
	// 스레드 안전하지 않은데, 콘솔 명령은 게임 스레드에서 돌고 렌더는
	// CommandExecuteThread에서 돈다. 밖에서 CopyResource·Map을 부르자
	// 렌더 스레드가 그리는 중에 깨졌다(실제로 크래시했다).
	//
	// 그래서 복사는 렌더 스레드가 자기 프레임 끝에서 한다. 밖에서는 요청만
	// 걸고 결과를 가져간다 — 컨텍스트를 만지는 것은 언제나 한 스레드다.
	void RequestGBufferCapture() { m_captureRequested.store(true, std::memory_order_release); }

	/// 캡처와 짝이 되는 드로우 스냅샷.
	///
	/// 픽셀만 가져와서는 대조가 되지 않는다 — 같은 입력으로 그렸는지 알 수
	/// 없기 때문이다. 그런데 렌더 큐(m_deferredQueue)는 커맨드 리스트를 만든
	/// 직후 비워지므로, 게임 스레드가 나중에 읽으면 이미 비어 있다(실제로
	/// '그릴 것이 없다'가 나왔다). 그래서 지워지기 직전에 같이 뜬다.
	struct GBufferCaptureDraw
	{
		Mesh*         mesh{ nullptr };
		Mathf::xMatrix worldMatrix{};
		Texture*      baseColor{ nullptr };
		Texture*      normalMap{ nullptr };
		Texture*      occRoughMetal{ nullptr };
		Texture*      emissive{ nullptr };

		// 본 팔레트는 값으로 복사한 것을 따로 둔다(GetCapturePalettes 참조).
		// 여기서는 그 사전을 찾을 키만 든다.
		size_t        animatorKey{ 0 };
		bool          hasSkinning{ false };
	};

	/// 캡처 시점의 본 팔레트 사본. 애니메이터 키 → 512행렬.
	///
	/// ★ shared_ptr로 버퍼를 붙드는 것으로는 부족하다. 붙드는 것은 수명이지
	/// 내용이 아니고, 애니메이션은 계속 갱신되므로 게임 스레드가 나중에 읽을
	/// 때는 이미 다른 포즈다. 대조의 전제는 'DX11이 그 픽셀을 그릴 때 쓴
	/// 바로 그 입력'이므로 값을 복사해야 성립한다.
	/// (실측: 포인터만 나르던 동안 겹침 0.70에 머물렀고, 값 복사로 바뀐 뒤
	///  포즈가 일치했다.)
	using CapturePalettes = std::unordered_map<size_t, std::vector<Mathf::xMatrix>>;
	const CapturePalettes& GetCapturePalettes() const { return m_capturePalettes; }

	const std::vector<GBufferCaptureDraw>& GetCaptureDraws() const { return m_captureDraws; }

	/// 준비돼 있으면 결과를 옮겨 담고 true. 아직이면 false — 호출부가 몇 프레임
	/// 기다렸다 다시 물어야 한다.
	bool ConsumeGBufferCapture(std::vector<uint8_t>& outBytes, uint32_t& outWidth,
		uint32_t& outHeight, uint32_t& outRowPitch, DXGI_FORMAT& outFormat);

	void SetWireFrame()     { useWireFrame = !useWireFrame; }
	void SetLightmapPass()  { useTestLightmap = !useTestLightmap; }

private:
	std::string                                 m_currentSkyTextureName{};
    bool										useWireFrame		{ false };
	bool										m_bShowRenderState	{ false };
	std::atomic_bool							useTestLightmap		{ false };
	bool										m_bShowGridSettings	{ false };

	static SceneRenderer* s_active;

	// GBuffer 캡처 상태. 렌더 스레드가 쓰고 게임 스레드가 읽는다.
	void ProcessGBufferCapture();
	void CaptureDrawSnapshot(RenderPassData* data);

	std::atomic_bool     m_captureRequested{ false };
	std::atomic_bool     m_captureReady{ false };
	std::mutex           m_captureMutex;
	std::vector<uint8_t> m_captureBytes;
	uint32_t             m_captureWidth{ 0 };
	uint32_t             m_captureHeight{ 0 };
	uint32_t             m_captureRowPitch{ 0 };
	DXGI_FORMAT          m_captureFormat{ DXGI_FORMAT_UNKNOWN };
	std::vector<GBufferCaptureDraw> m_captureDraws;
	CapturePalettes                 m_capturePalettes;
};
