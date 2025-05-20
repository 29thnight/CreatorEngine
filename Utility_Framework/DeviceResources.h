#pragma once
#include "Core.Definition.h"
#include "Delegate.h"

using namespace Microsoft::WRL;

class CoreWindow;

namespace DirectX11
{
	struct Sizef
	{
		float width;
		float height;
	};

	interface IDeviceNotify
	{
		virtual void OnDeviceLost() abstract;
		virtual void OnDeviceRestored() abstract;
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
		D3D_FEATURE_LEVEL GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }
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

		void ReportLiveDeviceObjects();

		CoreWindow* GetWindow() const { return m_window; }

	private:
		void CreateDeviceIndependentResources();
		void CreateDeviceResources();
		void CreateWindowSizeDependentResources();
		void UpdateRenderTargetSize();

	private:
		ComPtr<ID3D11Device3> m_d3dDevice;
		ComPtr<ID3D11DeviceContext3> m_d3dContext;
		ComPtr<IDXGISwapChain3> m_swapChain;
        ComPtr<ID3DUserDefinedAnnotation> m_annotation;
		ComPtr<ID3D11Debug> m_debugDevice;
		ComPtr<ID3D11InfoQueue> m_infoQueue;

		ComPtr<ID3D11RenderTargetView1> m_d3dRenderTargetView;
		ComPtr<ID3D11Texture2D1> m_backBuffer;
		ComPtr<ID3D11ShaderResourceView> m_backBufferSRV;
		ComPtr<ID3D11DepthStencilView> m_d3dDepthStencilView;
		ComPtr<ID3D11ShaderResourceView> m_DepthStencilViewSRV;
		ComPtr<ID3D11RasterizerState> m_rasterizerState;
		ComPtr<ID3D11DepthStencilState> m_depthStencilState;
		ComPtr<ID3D11BlendState> m_blendState;
		D3D11_VIEWPORT m_screenViewport;

		CoreWindow* m_window;

		D3D_FEATURE_LEVEL m_d3dFeatureLevel;
		Sizef m_d3dRenderTargetSize;
		Sizef m_outputSize;
		Sizef m_logicalSize;
		float m_dpi;
		float m_effectiveDpi;

		IDeviceNotify* m_deviceNotify;
	};
}
