#include "DirectXHelper.h"
#include "DeviceResources.h"
#include "CoreWindow.h"
#include "DirectXMath.h"
#include "Core.Memory.hpp"
#include "DirectXColors.h"

using namespace DirectX;

namespace DisplayMetrics
{
    // ���ػ� ���÷��̴� �������ϴ� �� ���� GPU �� ���͸� ������ �ʿ��� �� �ֽ��ϴ�.
    // ���� ��� ���ػ� �޴����� ���ӿ��� ��ȭ���� �ʴ� 60�������� �������Ϸ���
    // ��� ª�� ���͸� �������� ���� ������ �߻��� �� �ֽ��ϴ�.
    // ��� �÷��� �� �� ���Ϳ��� ��ȭ���� �������ϴ� ������
    // �����ϰ� ������ �մϴ�.
    static const bool SupportHighResolutions = false;

    // �����ػ󵵡� ���÷��̸� �����ϴ� �⺻ �Ӱ谪�Դϴ�. �Ӱ谪�� �ʰ��ϰų�
    // SupportHighResolutions�� false�� ��� ũ�Ⱑ 50%��
    //�پ��ϴ�.
    static const float DpiThreshold = 192.0f;		// ǥ�� ����ũ�� ���÷����� 200%�Դϴ�.
    static const float WidthThreshold = 1920.0f;	// �ʺ� 1080p�Դϴ�.
    static const float HeightThreshold = 1080.0f;	// ���̰� 1080p�Դϴ�.
};

DirectX11::DeviceResources::DeviceResources() :
    m_screenViewport(),
    m_d3dFeatureLevel(D3D_FEATURE_LEVEL_9_1),
    m_d3dRenderTargetSize(),
    m_outputSize(),
    m_logicalSize(),
    m_dpi(-1.0f),
    m_effectiveDpi(-1.0f),
    m_deviceNotify(nullptr)
{
    CreateDeviceIndependentResources();
    CreateDeviceResources();
}

DirectX11::DeviceResources::~DeviceResources()
{
}

void DirectX11::DeviceResources::SetWindow(CoreWindow& window)
{
    m_window = &window;
    m_logicalSize = { static_cast<float>(window.GetWidth()), static_cast<float>(window.GetHeight()) };
    m_dpi = static_cast<float>(GetDpiForWindow(window.GetHandle()));
    //m_d2dContext->SetDpi(m_dpi, m_dpi);

    CreateWindowSizeDependentResources();
}

void DirectX11::DeviceResources::SetLogicalSize(Sizef logicalSize)
{
    if (m_logicalSize.width != logicalSize.width || m_logicalSize.height != logicalSize.height)
    {
        m_logicalSize = logicalSize;
        CreateWindowSizeDependentResources();
    }
}

void DirectX11::DeviceResources::SetDpi(float dpi)
{
    if (m_dpi != dpi)
    {
        m_dpi = dpi;
        m_effectiveDpi = dpi;
        CreateWindowSizeDependentResources();
    }
}

void DirectX11::DeviceResources::ValidateDevice()
{
    // �⺻ ����Ͱ� ����̽��� ������� ���Ŀ� ����ǰų� ����̽��� ���ŵ� ���
    // D3D ����̽��� �� �̻� ��ȿ���� �ʽ��ϴ�.
    // ����, ����̽��� ������� ���� �⺻ ����Ϳ� ���� ������ �����ɴϴ�.

    ComPtr<IDXGIDevice3> dxgiDevice;
    DirectX11::ThrowIfFailed(m_d3dDevice.As(&dxgiDevice));

    ComPtr<IDXGIAdapter> deviceAdapter;
    DirectX11::ThrowIfFailed(dxgiDevice->GetAdapter(&deviceAdapter));

    ComPtr<IDXGIFactory4> deviceFactory;
    DirectX11::ThrowIfFailed(deviceAdapter->GetParent(IID_PPV_ARGS(&deviceFactory)));

    ComPtr<IDXGIAdapter1> previousDefaultAdapter;
    DirectX11::ThrowIfFailed(deviceFactory->EnumAdapters1(0, &previousDefaultAdapter));

    DXGI_ADAPTER_DESC1 previousDesc;
    DirectX11::ThrowIfFailed(previousDefaultAdapter->GetDesc1(&previousDesc));

    // ��������, ���� �⺻ ����Ϳ� ���� ������ �����ɴϴ�.
    ComPtr<IDXGIFactory4> currentFactory;
    DirectX11::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&currentFactory)));

    ComPtr<IDXGIAdapter1> currentDefaultAdapter;
    DirectX11::ThrowIfFailed(currentFactory->EnumAdapters1(0, &currentDefaultAdapter));

    DXGI_ADAPTER_DESC1 currentDesc;
    DirectX11::ThrowIfFailed(currentDefaultAdapter->GetDesc1(&currentDesc));

    // ����� LUID�� ��ġ���� �ʰų� ����̽��� ���ŵǾ��ٰ� �����ϴ� ���
    // �� D3D ����̽��� ������ �մϴ�.

    if (previousDesc.AdapterLuid.LowPart != currentDesc.AdapterLuid.LowPart ||
        previousDesc.AdapterLuid.HighPart != currentDesc.AdapterLuid.HighPart ||
        FAILED(m_d3dDevice->GetDeviceRemovedReason()))
    {
        // ���� ����̽��� ���õ� ���ҽ��� ���� ������ �����մϴ�.
        dxgiDevice = nullptr;
        deviceAdapter = nullptr;
        deviceFactory = nullptr;
        previousDefaultAdapter = nullptr;

        // �� ����̽� �� ���� ü���� ����ϴ�.
        HandleDeviceLost();
    }
}

void DirectX11::DeviceResources::HandleDeviceLost()
{
    m_swapChain = nullptr;

    if (m_deviceNotify != nullptr)
    {
        m_deviceNotify->OnDeviceLost();
    }

    CreateDeviceResources();
    CreateWindowSizeDependentResources();

    if (m_deviceNotify != nullptr)
    {
        m_deviceNotify->OnDeviceRestored();
    }
}

void DirectX11::DeviceResources::RegisterDeviceNotify(IDeviceNotify* deviceNotify)
{
    m_deviceNotify = deviceNotify;
}

void DirectX11::DeviceResources::Trim()
{
    ComPtr<IDXGIDevice3> dxgiDevice;
    m_d3dDevice.As(&dxgiDevice);

    dxgiDevice->Trim();
}

void DirectX11::DeviceResources::Present()
{
    // ù ��° �μ��� DXGI�� VSync���� �����ϵ��� �����Ͽ� ���ø����̼���
    // ���� VSync���� ����ϵ��� �մϴ�. �̸� ���� ȭ�鿡 ǥ�õ��� �ʴ� ��������
    // �������ϴ� �ֱ⸦ �������� ���� �� �ֽ��ϴ�.
    DXGI_PRESENT_PARAMETERS parameters = { 0 };
    HRESULT hr = m_swapChain->Present1(0, 0, &parameters);

    // ������ ����� �������� �����մϴ�.
    // �� �۾��� ���� �������� ������ ��� ��쿡��
    // �ùٸ��ϴ�. ���� �Ǵ� ��ũ�� ������ ���Ǵ� ��쿡�� �� ȣ���� �����ؾ� �մϴ�.
    m_d3dContext->DiscardView1(m_d3dRenderTargetView.Get(), nullptr, 0);

    // ���� ���ٽ��� �������� �����մϴ�.
    m_d3dContext->DiscardView1(m_d3dDepthStencilView.Get(), nullptr, 0);

    // ������ ����ų� ����̹� ���׷��̵�� ���� ����̽��� ���ŵǸ� 
    // ��� ����̽� ���ҽ��� �ٽ� ������ �մϴ�.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        HandleDeviceLost();
    }
    else
    {
        DirectX11::ThrowIfFailed(hr);
    }

	m_d3dContext->ClearRenderTargetView(m_d3dRenderTargetView.Get(), DirectX::Colors::Transparent);
	m_d3dContext->ClearDepthStencilView(m_d3dDepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void DirectX11::DeviceResources::ResizeResources()
{
	if (m_window == nullptr)
	{
		return;
	}
	int outputWidth = std::max<int>(
        static_cast<int>(m_logicalSize.width * m_effectiveDpi / DisplayMetrics::DpiThreshold), 1);
	int outputHeight = std::max<int>(
        static_cast<int>(m_logicalSize.height * m_effectiveDpi / DisplayMetrics::DpiThreshold), 1);
	// ũ�Ⱑ ������� �ʾ����� �����մϴ�.
	if (m_d3dRenderTargetSize.width == outputWidth && m_d3dRenderTargetSize.height == outputHeight)
	{
		return;
	}
	// ũ�Ⱑ 0�� ��� �����մϴ�.
	if (outputWidth == 0 || outputHeight == 0)
	{
		return;
	}
	// ũ�Ⱑ ����Ǹ� �� ũ�⸦ �����ϰ� ������ ��� �� ���� ���ٽ� ����� �ٽ� ����ϴ�.
	m_d3dRenderTargetSize = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	CreateWindowSizeDependentResources();
}

void DirectX11::DeviceResources::ReportLiveDeviceObjects()
{
#if defined(_DEBUG)
	if (m_debugDevice != nullptr)
	{
		m_debugDevice->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
	}
#endif
}

void DirectX11::DeviceResources::CreateDeviceIndependentResources()
{
}

void DirectX11::DeviceResources::CreateDeviceResources()
{
    // �� �÷��״� API �⺻���� �ٸ� �� ä�� ������ ǥ�鿡 ���� ������
    // �߰��մϴ�. Direct2D���� ȣȯ���� ���� �ʿ��մϴ�.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
    if (DirectX11::SdkLayersAvailable())
    {
        // ������Ʈ�� ����� ���� ���� ��쿡�� �� �÷��װ� �ִ� SDK ���̾ ���� ������� ����Ͻʽÿ�.
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif

    // �� �迭�� �� ���� ���α׷����� �����ϴ� DirectX �ϵ���� ��� ���� ������ �����մϴ�.
    // ������ �����ؾ� �մϴ�.
    // ������ ���ø����̼ǿ� �ʿ��� �ּ� ��� ������ �����ؾ� �մϴ�.
    // ������ �������� ���� ��� ��� ���ø����̼��� 9.1�� �����ϴ� ������ ���ֵ˴ϴ�.
    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };

    // Direct3D 11 API ����̽� ��ü�� �ش� ���ؽ�Ʈ�� ����ϴ�.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    HRESULT hr = D3D11CreateDevice(
        nullptr,						// �⺻ ����͸� ����Ϸ��� nullptr�� �����մϴ�.
        D3D_DRIVER_TYPE_HARDWARE,	// �ϵ���� �׷��� ����̹��� ����Ͽ� ����̽��� ����ϴ�.
        0,							// ����̹��� D3D_DRIVER_TYPE_SOFTWARE�� �ƴ� ��� 0�̾�� �մϴ�.
        creationFlags,						// ����� �� Direct2D ȣȯ�� �÷��׸� �����մϴ�.
        featureLevels,			// �� ���� ���α׷��� ������ �� �ִ� ��� ���� ����Դϴ�.
        ARRAYSIZE(featureLevels),			// �� ����� ũ���Դϴ�.
        D3D11_SDK_VERSION,			// Microsoft Store ���� ��� �׻� �� ���� D3D11_SDK_VERSION���� �����մϴ�.
        &device,						// ������� Direct3D ����̽��� ��ȯ�մϴ�.
        &m_d3dFeatureLevel,		// ������� ����̽��� ��� ������ ��ȯ�մϴ�.
        &context				// ����̽� ���� ���ؽ�Ʈ�� ��ȯ�մϴ�.
    );

#if defined(_DEBUG)
	// ����� ���̾ ����Ͽ� ����̽��� ����� ����� ���̾ ���� �����͸� �����ɴϴ�.
    DirectX11::ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_debugDevice)));

    DirectX11::ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_infoQueue)));
    {
        // WARNING �޽����� Breakpoint
        m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);

        // ERROR �޽����� Breakpoint
        m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);

        // CORRUPTION �޽����� Breakpoint (�޸� �ջ�, ġ����)
        m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);

        m_infoQueue->Release();
    }
#endif

    if (FAILED(hr))
    {
        // �ʱ�ȭ�� �����ϸ� WARP ����̽��� ��ü�˴ϴ�.
        // WARP�� ���� �ڼ��� ������ ������ �����ϼ���. 
        // https://go.microsoft.com/fwlink/?LinkId=286690
        DirectX11::ThrowIfFailed(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP, // �ϵ���� ����̽� ��� WARP ����̽��� ����ϴ�.
                0,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &device,
                &m_d3dFeatureLevel,
                &context
            )
        );
    }

    // Direct3D 11.3 API ����̽� �� ���� ���ؽ�Ʈ�� ���� �����͸� �����մϴ�.
    DirectX11::ThrowIfFailed(
        device.As(&m_d3dDevice)
    );

    DirectX11::ThrowIfFailed(
        context.As(&m_d3dContext)
    );
    // ����׿� �̺�Ʈ �߰��� m_annotation ��ü ����
    DirectX11::ThrowIfFailed(
		m_d3dContext->QueryInterface(IID_PPV_ARGS(&m_annotation))
    );
}

void DirectX11::DeviceResources::CreateWindowSizeDependentResources()
{
    // ���� â ũ��� ���õ� ���ؽ�Ʈ�� ����ϴ�.
    ID3D11RenderTargetView* nullViews[] = { nullptr };
    m_d3dContext->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
    m_d3dRenderTargetView = nullptr;
    //m_d2dContext->SetTarget(nullptr);
    //m_d2dTargetBitmap = nullptr;
    m_d3dDepthStencilView = nullptr;
    m_d3dContext->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);
    m_d3dContext->ClearState();

    UpdateRenderTargetSize();

    if (nullptr != m_swapChain)
    {
        HRESULT hr = m_swapChain->ResizeBuffers(
            2,
            lround(m_d3dRenderTargetSize.width),
            lround(m_d3dRenderTargetSize.height),
            DXGI_FORMAT_R8G8B8A8_UNORM,
            0
        );

        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            HandleDeviceLost();
            return;
        }
        else
        {
            DirectX11::ThrowIfFailed(hr);
        }
    }
    else
    {
        DXGI_SCALING scaling = DisplayMetrics::SupportHighResolutions ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };

        m_d3dRenderTargetSize.width = m_logicalSize.width;
        m_d3dRenderTargetSize.height = m_logicalSize.height;

        swapChainDesc.Width = lround(m_d3dRenderTargetSize.width);		// â�� ũ�⸦ ����ϴ�.
        swapChainDesc.Height = lround(m_d3dRenderTargetSize.height);
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;				// ���� �Ϲ����� ���� ü�� �����Դϴ�.
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1;								// ���� ���ø��� ������� ���ʽÿ�.
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
        swapChainDesc.BufferCount = 2;									// ���� ���۸��� ����Ͽ� ��� �ð��� �ּ�ȭ�մϴ�.
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;	// ��� Microsoft Store ���� �� SwapEffect�� ����ؾ� �մϴ�.
        swapChainDesc.Flags = 0;
        swapChainDesc.Scaling = scaling;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

		DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDesc = { 0 };

		swapChainFullscreenDesc.RefreshRate.Numerator = 60;
		swapChainFullscreenDesc.RefreshRate.Denominator = 1;
		swapChainFullscreenDesc.Scaling = DXGI_MODE_SCALING_CENTERED;
		swapChainFullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapChainFullscreenDesc.Windowed = TRUE;

        ComPtr<IDXGIDevice3> dxgiDevice;
        DirectX11::ThrowIfFailed(
            m_d3dDevice.As(&dxgiDevice)
        );

        ComPtr<IDXGIAdapter> dxgiAdapter;
        DirectX11::ThrowIfFailed(
            dxgiDevice->GetAdapter(&dxgiAdapter)
        );

        ComPtr<IDXGIFactory2> dxgiFactory;
        DirectX11::ThrowIfFailed(
            dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory))
        );

        ComPtr<IDXGISwapChain1> swapChain;
        DirectX11::ThrowIfFailed(
            dxgiFactory->CreateSwapChainForHwnd(
                m_d3dDevice.Get(),
                m_window->GetHandle(),
                &swapChainDesc,
                &swapChainFullscreenDesc,
                nullptr,
                &swapChain
            )
        );

        DirectX11::ThrowIfFailed(
            swapChain.As(&m_swapChain)
        );

		DirectX::SetName(m_swapChain.Get(), "IDXGISwapChain1");

        DirectX11::ThrowIfFailed(
            dxgiDevice->SetMaximumFrameLatency(1)
        );

        ComPtr<ID3D11Texture2D1> backBuffer;
        DirectX11::ThrowIfFailed(
            m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))
        );

		D3D11_RENDER_TARGET_VIEW_DESC1 renderTargetViewDesc = {};
		renderTargetViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		renderTargetViewDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
		renderTargetViewDesc.Texture2D.MipSlice = 0;

        DirectX11::ThrowIfFailed(
            m_d3dDevice->CreateRenderTargetView1(
                backBuffer.Get(),
                &renderTargetViewDesc,
                &m_d3dRenderTargetView
            )
        );

        m_d3dContext->ClearRenderTargetView(m_d3dRenderTargetView.Get(), Colors::SlateGray);

		DirectX::SetName(backBuffer.Get(), "BackBuffer");
        //����� �ؽ��� �޾ƿ���
        ID3D11Resource* pResource = nullptr;
		m_d3dRenderTargetView->GetResource(&pResource);
		DirectX11::ThrowIfFailed(
            pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&m_backBuffer)
        );

        // �ʿ��� ��� 3D �������� ����� ���� ���ٽ� �並 ����ϴ�.
        CD3D11_TEXTURE2D_DESC1 depthStencilDesc(
            DXGI_FORMAT_R24G8_TYPELESS,
            lround(m_d3dRenderTargetSize.width),
            lround(m_d3dRenderTargetSize.height),
            1, // �� ���� ���ٽ� ��� �ϳ��� ������ ������ �ֽ��ϴ�.
            1, // ���� MIP �� ������ ����մϴ�.
            D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
        );

        ComPtr<ID3D11Texture2D1> depthStencil;
        DirectX11::ThrowIfFailed(
            m_d3dDevice->CreateTexture2D1(
                &depthStencilDesc,
                nullptr,
                &depthStencil
            )
        );

        CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
        depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        DirectX11::ThrowIfFailed(
            m_d3dDevice->CreateDepthStencilView(
                depthStencil.Get(),
                &depthStencilViewDesc,
                &m_d3dDepthStencilView
            )
        );
		DirectX::SetName(depthStencil.Get(), "DepthStencil");

		CD3D11_DEPTH_STENCIL_DESC depthStencilDesc2(D3D11_DEFAULT);

		DirectX11::ThrowIfFailed(
			m_d3dDevice->CreateDepthStencilState(
				&depthStencilDesc2,
				&m_depthStencilState
			)
		);

		CD3D11_SHADER_RESOURCE_VIEW_DESC depthStencilSRVDesc(
            D3D11_SRV_DIMENSION_TEXTURE2D, 
            DXGI_FORMAT_R24_UNORM_X8_TYPELESS
        );

		DirectX11::ThrowIfFailed(
			m_d3dDevice->CreateShaderResourceView(
				depthStencil.Get(),
				&depthStencilSRVDesc,
				&m_DepthStencilViewSRV
			)
		);

		DirectX::SetName(m_DepthStencilViewSRV.Get(), "RenderTargetViewSRV");

        CD3D11_RASTERIZER_DESC rasterizerDesc = CD3D11_RASTERIZER_DESC( D3D11_DEFAULT );
		DirectX11::ThrowIfFailed(
			m_d3dDevice->CreateRasterizerState(
				&rasterizerDesc,
				&m_rasterizerState
			)
		);

		CD3D11_BLEND_DESC blendDesc = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		DirectX11::ThrowIfFailed(
			m_d3dDevice->CreateBlendState(
				&blendDesc,
				&m_blendState
			)
		);

        m_screenViewport = CD3D11_VIEWPORT(
            0.0f,
            0.0f,
            m_d3dRenderTargetSize.width,
            m_d3dRenderTargetSize.height
        );

        m_d3dContext->RSSetViewports(1, &m_screenViewport);
    }
}

void DirectX11::DeviceResources::UpdateRenderTargetSize()
{
    m_effectiveDpi = m_dpi;

    if (!DisplayMetrics::SupportHighResolutions && m_dpi > DisplayMetrics::DpiThreshold)
    {
        float width = DirectX11::ConvertDipsToPixels(m_logicalSize.width, m_dpi);
        float height = DirectX11::ConvertDipsToPixels(m_logicalSize.height, m_dpi);
        if (std::max(height, width) > DisplayMetrics::WidthThreshold && std::min(height, width) > DisplayMetrics::HeightThreshold)
        {
            m_effectiveDpi /= 2.0f;
        }
    }

    m_outputSize.width = DirectX11::ConvertDipsToPixels(m_logicalSize.width, m_effectiveDpi);
    m_outputSize.height = DirectX11::ConvertDipsToPixels(m_logicalSize.height, m_effectiveDpi);

    m_outputSize.width = std::max(1.f, m_outputSize.width);
    m_outputSize.height = std::max(1.f, m_outputSize.height);

	m_d3dRenderTargetSize.width = m_outputSize.width;
	m_d3dRenderTargetSize.height = m_outputSize.height;
}
