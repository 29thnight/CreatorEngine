#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Definition.h"
#include "EngineResourceCensus.h"
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

class CoreWindow;

namespace DirectX11
{
	interface IDeviceNotify
	{
		virtual void OnDeviceLost() abstract;
		virtual void OnDeviceRestored() abstract;
	};

	// 디버그 레이어가 보고한 살아있는 GPU 객체 집계.
	// "지금 몇 개가 살아있는가"를 숫자로 남겨 리소스 누수 수정의 효과를
	// 전후 비교할 수 있게 하는 것이 목적이다.
	struct GpuObjectCensus
	{
		bool available{ false };                 // 디버그 레이어가 없으면 false (VRAM 수치만 유효)
		uint32_t totalObjects{ 0 };
		std::map<std::string, uint32_t> byType;  // 예: "ID3D11BlendState" -> 47
		uint64_t vramUsedMB{ 0 };
		uint64_t vramBudgetMB{ 0 };
	};

	class DeviceResources
	{
	public:
		DeviceResources();
		~DeviceResources();
		void SetWindow(CoreWindow& window);
		void SetLogicalSize(Sizef logicalSize);
		void SetDpi(float dpi);
		void ValidateDevice();
		void HandleDeviceLost();
		void RegisterDeviceNotify(IDeviceNotify* deviceNotify);
		void Trim();
		void Present();

		void ResizeResources();

		Sizef GetOutputSize() const { return m_outputSize; }
		Sizef GetLogicalSize() const { return m_logicalSize; }
		float GetDpi() const { return m_dpi; }
		float GetAspectRatio() const { return m_logicalSize.width / m_logicalSize.height; }

		ID3D11Device3* GetD3DDevice() const { return m_d3dDevice.Get(); }
		ID3D11DeviceContext3* GetD3DDeviceContext() const { return m_d3dContext.Get(); }
		IDXGISwapChain3* GetSwapChain() const { return m_swapChain.Get(); }
		void ReleaseSwapChain() { m_swapChain.Reset(); m_swapChain = nullptr; }

		// ── 프레젠트 소유권 (PHASE 3-1 재정의, D2) ──
		//
		// 참이면 이 클래스는 스왑체인을 만들지 않는다. 창에 스왑체인을 붙이는
		// 것은 다른 쪽(ImGui DX12 셸)이고, 여기는 디바이스·깊이·상태 객체·
		// 뷰포트만 맡는다.
		//
		// ★ 왜 플래그인가: DXGI는 한 HWND에 스왑체인 둘을 허용하지 않는다.
		//   예전에는 이 클래스가 초기화 때 무조건 만들었고, 나중에 셸이 같은
		//   창에 붙으려다 DX11 것을 무효화해 종료가 크래시했다. 소유권을
		//   '먼저 만든 쪽이 이긴다'가 아니라 명시적 결정으로 바꾼다.
		//
		// ★ 왜 설정을 직접 읽지 않는가: 이 클래스는 Utility_Framework 소속이라
		//   EngineSetting(EngineEntry)을 참조하면 계층이 거꾸로 선다. 결정은
		//   위층이 하고 여기는 주입받는다.
		//
		// 초기화(CreateWindowSizeDependentResources) 전에 정해야 한다. 셸
		// 초기화가 실패해 DX11로 되돌아갈 때는 거짓으로 되돌리고 창 크기
		// 의존 리소스를 다시 만들면 그 자리에서 스왑체인이 생긴다.
		void SetPresentOwnedExternally(bool owned) { m_presentOwnedExternally = owned; }
		bool IsPresentOwnedExternally() const { return m_presentOwnedExternally; }

		/// 소유권을 되찾아 스왑체인을 만든다. 셸 초기화가 실패했을 때만 쓴다.
		///
		/// 창 크기 의존 리소스를 다시 만드는 것이 전부다 — m_swapChain이 널이라
		/// 생성 분기를 타고, 그 자리에서 백버퍼·RTV까지 함께 선다.
		/// 전용 진입점을 두는 이유는 CreateWindowSizeDependentResources가
		/// private이기도 하지만, '폴백'이라는 의도가 호출부에 드러나야 해서다.
		void ReclaimPresentOwnership()
		{
			m_presentOwnedExternally = false;
			CreateWindowSizeDependentResources();
		}
		D3D_FEATURE_LEVEL GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }

		// 백버퍼 RTV가 다시 만들어질 때 위층(렌더 전역 상태)에 알리는 콜백.
		//
		// 예전에는 이 파일(.cpp)이 RenderEngine/DeviceState.h를 직접 include해
		// DeviceStates->g_backBufferRTV에 대입했다 — 코어가 렌더를 아는 역방향
		// 간선이다. 방향을 뒤집는다: 코어는 "바뀌었다"고만 알리고, 무엇을
		// 갱신할지는 진입점(Dx11Main·GameMain)이 등록한다.
		//
		// 등록 시점에 RTV가 이미 서 있으면 즉시 한 번 호출한다 — 구 코드는
		// 생성 직후 대입이 항상 일어났으므로 그 타이밍 보장을 유지한다.
		void SetBackBufferPublishCallback(std::function<void(ID3D11RenderTargetView*)> callback)
		{
			m_backBufferPublish = std::move(callback);
			if (m_backBufferPublish && m_d3dRenderTargetView)
			{
				m_backBufferPublish(m_d3dRenderTargetView.Get());
			}
		}

		ID3D11RenderTargetView1* GetBackBufferRenderTargetView() const { return m_d3dRenderTargetView.Get(); }
		ID3D11Texture2D1* GetBackBuffer() const { return m_backBuffer.Get(); }
		ID3D11ShaderResourceView* GetBackBufferSRV() const { return m_backBufferSRV.Get(); }
		ID3D11DepthStencilView* GetDepthStencilView() const { return m_d3dDepthStencilView.Get(); }
        ID3D11ShaderResourceView* GetDepthStencilViewSRV() const { return m_DepthStencilViewSRV.Get(); }
		ID3D11RasterizerState* GetRasterizerState() const { return m_rasterizerState.Get(); }
		ID3D11DepthStencilState* GetDepthStencilState() const { return m_depthStencilState.Get(); }
		ID3D11BlendState* GetBlendState() const { return m_blendState.Get(); }
		D3D11_VIEWPORT GetScreenViewport() const { return m_screenViewport; }
		ID3DUserDefinedAnnotation* GetAnnotation() const { return m_annotation.Get(); }
		DXGI_QUERY_VIDEO_MEMORY_INFO GetVideoMemoryInfo() const;
		//PROCESS_MEMORY_COUNTERS_EX _GetProcessMemoryInfo() const;
		void ReportLiveDeviceObjects();

		// 살아있는 GPU 객체를 타입별로 집계한다. 디버그 레이어가 없으면
		// available=false인 채 VRAM 수치만 채워 반환한다.
		//
		// 주의: 타입별 집계는 ReportLiveDeviceObjects(D3D11_RLDO_DETAIL)로 디바이스의
		// 자식 객체를 순회한다. 이 호출은 "디바이스를 파괴하기 직전에 한 번" 쓰라고
		// 만들어진 것이고, 실행 중에 부르면 디버그 레이어가 관리하던 커맨드 리스트
		// 재활용 경로가 망가진다. 순회가 끝난 뒤 렌더가 재개되면 워커 스레드의
		// FinishCommandList가 d3d11!CContext::RecycleCommandLists에서 죽는다.
		//
		// 렌더 스레드를 멈춰도 소용없다 — 순회 시점의 경합이 아니라 순회가 남긴
		// 부작용이라, 집계 한 번이면 그 뒤 아무 때나 터진다(재현 3/3).
		// 그래서 allowDeviceEnumeration=true는 종료 처리처럼 이후 렌더가 없는
		// 지점에서만 쓴다. 그 외에는 VRAM 수치만 채워 돌아온다.
		GpuObjectCensus CaptureLiveObjectCensus(bool allowDeviceEnumeration = false);

		// 현재 집계를 로그에 남긴다. label은 측정 시점을 식별하는 이름
		// (예: "씬 로드 완료", "씬 언로드 후").
		void LogLiveObjectCensus(std::string_view label, bool allowDeviceEnumeration = false);

		// 직전 기준선 대비 증감을 로그에 남기고, 현재 값을 새 기준선으로 삼는다.
		// 씬 전환 전후로 호출하면 회수되지 않은 리소스가 그대로 드러난다.
		void LogLiveObjectDelta(std::string_view label, bool allowDeviceEnumeration = false);

		// 기준선을 현재 상태로 초기화한다(측정 구간의 시작점 지정).
		void ResetLiveObjectBaseline();


		// 현재 활성 인스턴스. 씬 매니저 등 DeviceResources를 직접 소유하지 않는
		// 계층에서 진단을 호출하기 위한 접근자다. 아직 생성 전이거나 파괴 후면 nullptr.
		// 주의: 정적 링크되는 모듈마다 별도 실체를 가지므로, 엔진 실행 파일 내부에서만 사용한다.
		static DeviceResources* GetActive() noexcept { return s_active; }

		CoreWindow* GetWindow() const { return m_window; }

	private:
		bool CheckHDRSupport(ComPtr<IDXGIAdapter> adapter);
		void CreateDeviceIndependentResources();
		void CreateDeviceResources();
		void CreateWindowSizeDependentResources();
		void UpdateRenderTargetSize();
		void HandleLostSwapChain();

	private:
		ComPtr<IDXGIDevice3> m_dxgiDevice;
		ComPtr<ID3D11Device3> m_d3dDevice;
		ComPtr<IDXGIAdapter> m_deviceAdapter;
		ComPtr<ID3D11DeviceContext3> m_d3dContext;
		ComPtr<IDXGISwapChain3> m_swapChain;
		ComPtr<IDXGIDebug> m_dxgiDebug;
        ComPtr<ID3DUserDefinedAnnotation> m_annotation;
		ComPtr<ID3D11Debug> m_debugDevice;
		ComPtr<ID3D11InfoQueue> m_infoQueue;
		ComPtr<IDXGIInfoQueue> m_dxgiInfoQueue;

		ComPtr<ID3D11RenderTargetView1>		m_d3dRenderTargetView;
		std::function<void(ID3D11RenderTargetView*)> m_backBufferPublish;
		ComPtr<ID3D11Texture2D1>			m_backBuffer;
		ComPtr<ID3D11ShaderResourceView>	m_backBufferSRV;
		ComPtr<ID3D11DepthStencilView>		m_d3dDepthStencilView;
		ComPtr<ID3D11ShaderResourceView>	m_DepthStencilViewSRV;
		ComPtr<ID3D11RasterizerState>		m_rasterizerState;
		ComPtr<ID3D11DepthStencilState>		m_depthStencilState;
		ComPtr<ID3D11BlendState>			m_blendState;
		D3D11_VIEWPORT m_screenViewport;
		CoreWindow* m_window;

		GpuObjectCensus m_baselineCensus;
		Diagnostics::ResourceSnapshot m_baselineResources;
		bool m_hasBaseline{ false };

		// 이미 캡처한 집계를 로그로 옮기는 부분. 캡처와 출력을 나눠두어야
		// 요청 처리 경로가 순회를 한 번만 수행한다.
		void LogCensus(const GpuObjectCensus& census, std::string_view label);
		void LogLiveObjectDeltaFrom(const GpuObjectCensus& current, std::string_view label);

		static inline DeviceResources* s_active{ nullptr };

		D3D_FEATURE_LEVEL m_d3dFeatureLevel;
		Sizef m_d3dRenderTargetSize;
		Sizef m_outputSize;
		Sizef m_logicalSize;
		float m_dpi;
		float m_effectiveDpi;
		bool  m_supportHDR;

		// 스왑체인을 이 클래스가 만들지 않는다(D2). 기본은 거짓 — 지금까지의
		// 동작을 그대로 유지하고, 켜는 것은 셸 모드를 고른 위층의 결정이다.
		bool  m_presentOwnedExternally{ false };

		IDeviceNotify* m_deviceNotify;
	};
}
#endif // !DYNAMICCPP_EXPORTS