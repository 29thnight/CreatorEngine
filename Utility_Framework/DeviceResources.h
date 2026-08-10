#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Definition.h"
// EngineResourceCensus.h include가 여기 있었다 — 기준선 장부가 쓰던 것이고,
// 그 장부는 RenderEngine/GpuDiagnostics로 갔다(2026-08-10).
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

	// ★ GpuObjectCensus가 여기 있었다 (2026-08-10, DX12 이관).
	//   RHIGpuObjectCensus로 옮겼다 - RenderEngine/RHI/IRHIDeviceResources.h.

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
		// ★ GPU 진단 표면이 여기 있었다 (2026-08-10, DX12 이관).
		//
		//   GetVideoMemoryInfo · ReportLiveDeviceObjects · CaptureLiveObjectCensus ·
		//   LogLiveObjectCensus · LogLiveObjectDelta · ResetLiveObjectBaseline ·
		//   GetActive.
		//
		//   이 중 원자료를 내는 둘(VRAM · 라이브 객체 집계)은 IRHIDeviceResources로
		//   갔고, 그 원자료로 하던 일(기준선 · 증감 · 서술)은 RenderEngine/
		//   GpuDiagnostics로 갔다. 둘을 나눈 이유는 후자가 백엔드와 무관해
		//   구현마다 복사될 이유가 없어서다.
		//
		//   ★ VRAM 조회는 원래도 DXGI 어댑터 질의라 백엔드와 무관했다 —
		//     DX11이 쥐고 있을 이유가 없었고, 이관이 그것을 바로잡는다.

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

		// 기준선 장부(m_baselineCensus · m_baselineResources · m_hasBaseline)와
		// 출력 헬퍼 둘, 그리고 s_active가 여기 있었다 (2026-08-10, DX12 이관).
		// RenderEngine/GpuDiagnostics로 갔고, 활성 인스턴스 추적은
		// GetDiagnosticsDeviceResources()가 명시 등록으로 대신한다.

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