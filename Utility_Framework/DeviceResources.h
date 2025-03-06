#pragma once
#include "Core.Definition.h"
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
		D3D_FEATURE_LEVEL GetDeviceFeatureLevel() const { return m_d3dFeatureLevel; }
		ID3D11RenderTargetView1* GetBackBufferRenderTargetView() const { return m_d3dRenderTargetView.Get(); }
		ID3D11Texture2D1* GetBackBuffer() const { return m_backBuffer.Get(); }
		ID3D11ShaderResourceView* GetBackBufferSRV() const { return m_backBufferSRV.Get(); }
		ID3D11DepthStencilView* GetDepthStencilView() const { return m_d3dDepthStencilView.Get(); }
		D3D11_VIEWPORT GetScreenViewport() const { return m_screenViewport; }

		ID2D1Factory3* GetD2DFactory() const { return m_d2dFactory.Get(); }
		ID2D1Device2* GetD2DDevice() const { return m_d2dDevice.Get(); }
		ID2D1DeviceContext2* GetD2DDeviceContext() const { return m_d2dContext.Get(); }
		ID2D1RenderTarget* GetD2DTarget() const { return m_d2dTarget.Get(); }
		IDWriteFactory3* GetDWriteFactory() const { return m_dwriteFactory.Get(); }
		IWICImagingFactory2* GetWicImagingFactory() const { return m_wicFactory; }

        void BeginEvent(const std::wstring_view& name);
        void EndEvent();
        void SetMarker(const std::wstring_view& name);

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

		ComPtr<ID3D11RenderTargetView1> m_d3dRenderTargetView;
		ComPtr<ID3D11Texture2D1> m_backBuffer;
		ComPtr<ID3D11ShaderResourceView> m_backBufferSRV;
		ComPtr<ID3D11DepthStencilView> m_d3dDepthStencilView;
		D3D11_VIEWPORT m_screenViewport;

		ComPtr<ID2D1Factory3> m_d2dFactory;
		ComPtr<ID2D1Device2> m_d2dDevice;
		ComPtr<ID2D1DeviceContext2> m_d2dContext;
		ComPtr<ID2D1RenderTarget> m_d2dTarget;
		ComPtr<IDXGISurface2> dxgiBackBuffer;

		ComPtr<IDWriteFactory3> m_dwriteFactory;
		IWICImagingFactory2* m_wicFactory;

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
