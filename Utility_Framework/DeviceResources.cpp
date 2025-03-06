#include "DirectXHelper.h"
#include "DeviceResources.h"
#include "CoreWindow.h"
#include "DirectXMath.h"
using namespace DirectX;

namespace DisplayMetrics
{
    // 고해상도 디스플레이는 렌더링하는 데 많은 GPU 및 배터리 전원이 필요할 수 있습니다.
    // 예를 들어 고해상도 휴대폰의 게임에서 고화질로 초당 60프레임을 렌더링하려는
    // 경우 짧은 배터리 수명으로 인해 문제가 발생할 수 있습니다.
    // 모든 플랫폼 및 폼 팩터에서 고화질로 렌더링하는 결정은
    // 신중하게 내려야 합니다.
    static const bool SupportHighResolutions = false;

    // “고해상도” 디스플레이를 정의하는 기본 임계값입니다. 임계값을 초과하거나
    // SupportHighResolutions가 false인 경우 크기가 50%로
    //줄어듭니다.
    static const float DpiThreshold = 192.0f;		// 표준 데스크톱 디스플레이의 200%입니다.
    static const float WidthThreshold = 1920.0f;	// 너비가 1080p입니다.
    static const float HeightThreshold = 1080.0f;	// 높이가 1080p입니다.
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
        m_d2dContext->SetDpi(m_dpi, m_dpi);
        CreateWindowSizeDependentResources();
    }
}

void DirectX11::DeviceResources::ValidateDevice()
{
    // 기본 어댑터가 디바이스가 만들어진 이후에 변경되거나 디바이스가 제거된 경우
    // D3D 디바이스는 더 이상 유효하지 않습니다.
    // 먼저, 디바이스를 만들었을 때의 기본 어댑터에 대한 정보를 가져옵니다.

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

    // 다음으로, 현재 기본 어댑터에 대한 정보를 가져옵니다.
    ComPtr<IDXGIFactory4> currentFactory;
    DirectX11::ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&currentFactory)));

    ComPtr<IDXGIAdapter1> currentDefaultAdapter;
    DirectX11::ThrowIfFailed(currentFactory->EnumAdapters1(0, &currentDefaultAdapter));

    DXGI_ADAPTER_DESC1 currentDesc;
    DirectX11::ThrowIfFailed(currentDefaultAdapter->GetDesc1(&currentDesc));

    // 어댑터 LUID가 일치하지 않거나 디바이스가 제거되었다고 보고하는 경우
    // 새 D3D 디바이스를 만들어야 합니다.

    if (previousDesc.AdapterLuid.LowPart != currentDesc.AdapterLuid.LowPart ||
        previousDesc.AdapterLuid.HighPart != currentDesc.AdapterLuid.HighPart ||
        FAILED(m_d3dDevice->GetDeviceRemovedReason()))
    {
        // 이전 디바이스와 관련된 리소스에 대한 참조를 해제합니다.
        dxgiDevice = nullptr;
        deviceAdapter = nullptr;
        deviceFactory = nullptr;
        previousDefaultAdapter = nullptr;

        // 새 디바이스 및 스왑 체인을 만듭니다.
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
    m_d2dContext->SetDpi(m_dpi, m_dpi);
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
    // 첫 번째 인수는 DXGI에 VSync까지 차단하도록 지시하여 애플리케이션이
    // 다음 VSync까지 대기하도록 합니다. 이를 통해 화면에 표시되지 않는 프레임을
    // 렌더링하는 주기를 낭비하지 않을 수 있습니다.
    DXGI_PRESENT_PARAMETERS parameters = { 0 };
    HRESULT hr = m_swapChain->Present1(1, 0, &parameters);

    // 렌더링 대상의 콘텐츠를 삭제합니다.
    // 이 작업은 기존 콘텐츠를 완전히 덮어쓸 경우에만
    // 올바릅니다. 변경 또는 스크롤 영역이 사용되는 경우에는 이 호출을 제거해야 합니다.
    m_d3dContext->DiscardView1(m_d3dRenderTargetView.Get(), nullptr, 0);

    // 깊이 스텐실의 콘텐츠를 삭제합니다.
    m_d3dContext->DiscardView1(m_d3dDepthStencilView.Get(), nullptr, 0);

    // 연결이 끊기거나 드라이버 업그레이드로 인해 디바이스가 제거되면 
    // 모든 디바이스 리소스를 다시 만들어야 합니다.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        HandleDeviceLost();
    }
    else
    {
        DirectX11::ThrowIfFailed(hr);
    }
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
	// 크기가 변경되지 않았으면 중지합니다.
	if (m_d3dRenderTargetSize.width == outputWidth && m_d3dRenderTargetSize.height == outputHeight)
	{
		return;
	}
	// 크기가 0인 경우 중지합니다.
	if (outputWidth == 0 || outputHeight == 0)
	{
		return;
	}
	// 크기가 변경되면 새 크기를 저장하고 렌더링 대상 및 깊이 스텐실 대상을 다시 만듭니다.
	m_d3dRenderTargetSize = { static_cast<float>(outputWidth), static_cast<float>(outputHeight) };
	CreateWindowSizeDependentResources();
}

void DirectX11::DeviceResources::CreateDeviceIndependentResources()
{
    // Direct2D 리소스를 초기화합니다.
    D2D1_FACTORY_OPTIONS options;
    ZeroMemory(&options, sizeof(D2D1_FACTORY_OPTIONS));

#if defined(_DEBUG)
    // 프로젝트가 디버그 빌드 중인 경우 SDK 레이어를 통해 Direct2D 디버깅을 사용합니다.
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    // Direct2D 팩터리를 초기화합니다.
    DirectX11::ThrowIfFailed(
        D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            __uuidof(ID2D1Factory3),
            &options,
            &m_d2dFactory
        )
    );

    // DirectWrite 팩터리를 초기화합니다.
    DirectX11::ThrowIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory3),
            &m_dwriteFactory
        )
    );

    // WIC(Windows Imaging Component) 팩터리를 초기화합니다. //이거 왜 터지냐? ㅋㅋㅋ
    DirectX11::ThrowIfFailed(
    	CoCreateInstance(
    		CLSID_WICImagingFactory2,
    		nullptr,
    		CLSCTX_INPROC_SERVER,
    		IID_PPV_ARGS(&m_wicFactory)
    	)
    );
}

void DirectX11::DeviceResources::CreateDeviceResources()
{
    // 이 플래그는 API 기본값과 다른 색 채널 순서의 표면에 대한 지원을
    // 추가합니다. Direct2D와의 호환성을 위해 필요합니다.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
    if (DirectX11::SdkLayersAvailable())
    {
        // 프로젝트가 디버그 빌드 중인 경우에는 이 플래그가 있는 SDK 레이어를 통해 디버깅을 사용하십시오.
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif

    // 이 배열은 이 응용 프로그램에서 지원하는 DirectX 하드웨어 기능 수준 집합을 정의합니다.
    // 순서를 유지해야 합니다.
    // 설명에서 애플리케이션에 필요한 최소 기능 수준을 선언해야 합니다.
    // 별도로 지정하지 않은 경우 모든 애플리케이션은 9.1을 지원하는 것으로 간주됩니다.
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

    // Direct3D 11 API 디바이스 개체와 해당 컨텍스트를 만듭니다.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    HRESULT hr = D3D11CreateDevice(
        nullptr,						// 기본 어댑터를 사용하려면 nullptr을 지정합니다.
        D3D_DRIVER_TYPE_HARDWARE,	// 하드웨어 그래픽 드라이버를 사용하여 디바이스를 만듭니다.
        0,							// 드라이버가 D3D_DRIVER_TYPE_SOFTWARE가 아닌 경우 0이어야 합니다.
        creationFlags,						// 디버그 및 Direct2D 호환성 플래그를 설정합니다.
        featureLevels,			// 이 응용 프로그램이 지원할 수 있는 기능 수준 목록입니다.
        ARRAYSIZE(featureLevels),			// 위 목록의 크기입니다.
        D3D11_SDK_VERSION,			// Microsoft Store 앱의 경우 항상 이 값을 D3D11_SDK_VERSION으로 설정합니다.
        &device,						// 만들어진 Direct3D 디바이스를 반환합니다.
        &m_d3dFeatureLevel,		// 만들어진 디바이스의 기능 수준을 반환합니다.
        &context				// 디바이스 직접 컨텍스트를 반환합니다.
    );

    if (FAILED(hr))
    {
        // 초기화에 실패하면 WARP 디바이스로 대체됩니다.
        // WARP에 대한 자세한 내용은 다음을 참조하세요. 
        // https://go.microsoft.com/fwlink/?LinkId=286690
        DirectX11::ThrowIfFailed(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP, // 하드웨어 디바이스 대신 WARP 디바이스를 만듭니다.
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

    // Direct3D 11.3 API 디바이스 및 직접 컨텍스트에 대한 포인터를 저장합니다.
    DirectX11::ThrowIfFailed(
        device.As(&m_d3dDevice)
    );

    DirectX11::ThrowIfFailed(
        context.As(&m_d3dContext)
    );

    // Direct2D 디바이스 개체 및 해당 컨텍스트를 만듭니다.
    ComPtr<IDXGIDevice3> dxgiDevice;
    DirectX11::ThrowIfFailed(
        m_d3dDevice.As(&dxgiDevice)
    );

    DirectX11::ThrowIfFailed(
        m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice)
    );

    DirectX11::ThrowIfFailed(
        m_d2dDevice->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
            &m_d2dContext
        )
    );
}

void DirectX11::DeviceResources::CreateWindowSizeDependentResources()
{
    // 이전 창 크기와 관련된 컨텍스트를 지웁니다.
    ID3D11RenderTargetView* nullViews[] = { nullptr };
    m_d3dContext->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
    m_d3dRenderTargetView = nullptr;
    //m_d2dContext->SetTarget(nullptr);
    //m_d2dTargetBitmap = nullptr;
    m_d3dDepthStencilView = nullptr;
    m_d3dContext->Flush1(D3D11_CONTEXT_TYPE_ALL, nullptr);

    UpdateRenderTargetSize();

    if (nullptr != m_swapChain)
    {
        HRESULT hr = m_swapChain->ResizeBuffers(
            2,
            lround(m_d3dRenderTargetSize.width),
            lround(m_d3dRenderTargetSize.height),
            DXGI_FORMAT_B8G8R8A8_UNORM,
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

        swapChainDesc.Width = lround(m_d3dRenderTargetSize.width);		// 창의 크기를 맞춥니다.
        swapChainDesc.Height = lround(m_d3dRenderTargetSize.height);
        swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;				// 가장 일반적인 스왑 체인 형식입니다.
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1;								// 다중 샘플링을 사용하지 마십시오.
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;									// 이중 버퍼링을 사용하여 대기 시간을 최소화합니다.
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;	// 모든 Microsoft Store 앱은 이 SwapEffect를 사용해야 합니다.
        swapChainDesc.Flags = 0;
        swapChainDesc.Scaling = scaling;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

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
                nullptr,
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

        DirectX11::ThrowIfFailed(
            m_d3dDevice->CreateRenderTargetView1(
                backBuffer.Get(),
                nullptr,
                &m_d3dRenderTargetView
            )
        );

		DirectX::SetName(backBuffer.Get(), "BackBuffer");
        //백버퍼 텍스쳐 받아오기
        ID3D11Resource* pResource = nullptr;
		m_d3dRenderTargetView->GetResource(&pResource);
		pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&m_backBuffer);

        // 필요한 경우 3D 렌더링에 사용할 깊이 스텐실 뷰를 만듭니다.
        CD3D11_TEXTURE2D_DESC1 depthStencilDesc(
            DXGI_FORMAT_D24_UNORM_S8_UINT,
            lround(m_d3dRenderTargetSize.width),
            lround(m_d3dRenderTargetSize.height),
            1, // 이 깊이 스텐실 뷰는 하나의 질감만 가지고 있습니다.
            1, // 단일 MIP 맵 수준을 사용합니다.
            D3D11_BIND_DEPTH_STENCIL
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
        DirectX11::ThrowIfFailed(
            m_d3dDevice->CreateDepthStencilView(
                depthStencil.Get(),
                &depthStencilViewDesc,
                &m_d3dDepthStencilView
            )
        );

		DirectX::SetName(depthStencil.Get(), "DepthStencil");

        m_screenViewport = CD3D11_VIEWPORT(
            0.0f,
            0.0f,
            m_d3dRenderTargetSize.width,
            m_d3dRenderTargetSize.height
        );

        m_d3dContext->RSSetViewports(1, &m_screenViewport);

        // 스왑 체인 백 버퍼에 연결된 Direct2D 대상
        // 비트맵을 만들고 이를 현재 대상으로 설정합니다.
        D2D1_BITMAP_PROPERTIES1 bitmapProperties =
            D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                m_dpi,
                m_dpi
            );

        DirectX11::ThrowIfFailed(
            m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer))
        );

		D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			0,
			0,
			D2D1_RENDER_TARGET_USAGE_NONE,
			D2D1_FEATURE_LEVEL_DEFAULT
		);

		m_d2dFactory->CreateDxgiSurfaceRenderTarget(
			dxgiBackBuffer.Get(),
            &props,
			&m_d2dTarget
		);
        m_d2dContext->SetDpi(m_effectiveDpi, m_effectiveDpi);
        m_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
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
}
