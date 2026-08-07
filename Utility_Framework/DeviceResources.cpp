#ifndef DYNAMICCPP_EXPORTS
#include "DirectXHelper.h"
#include "DeviceResources.h"
#include "CoreWindow.h"
#include "DirectXMath.h"
#include "Core.Memory.hpp"
#include "DirectXColors.h"
#include "DeviceState.h"
#include "LogSystem.h"

#include <cctype>
#include <format>

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

    s_active = this;
}

DirectX11::DeviceResources::~DeviceResources()
{
    if (s_active == this)
    {
        s_active = nullptr;
    }

    // 스왑체인이 없을 수 있다 — HandleDeviceLost가 비운 뒤 재생성이 실패하면
    // 그대로 남는다. 널 역참조로 종료가 크래시했다(덤프로 잡혔다).
    if (m_swapChain)
    {
        m_swapChain->SetFullscreenState(FALSE, NULL); // 창 모드로
    }
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
    // 기본 어댑터가 디바이스가 만들어진 이후에 변경되거나 디바이스가 제거된 경우
    // D3D 디바이스는 더 이상 유효하지 않습니다.
    // 먼저, 디바이스를 만들었을 때의 기본 어댑터에 대한 정보를 가져옵니다.

    DirectX11::ThrowIfFailed(m_d3dDevice.As(&m_dxgiDevice));

    DirectX11::ThrowIfFailed(m_dxgiDevice->GetAdapter(&m_deviceAdapter));

    ComPtr<IDXGIFactory4> deviceFactory;
    DirectX11::ThrowIfFailed(m_deviceAdapter->GetParent(IID_PPV_ARGS(&deviceFactory)));

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
        m_dxgiDevice = nullptr;
        m_deviceAdapter = nullptr;
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
    m_dxgiDevice->Trim();
}

void DirectX11::DeviceResources::Present()
{
    // ★ 프레젠트를 남이 소유하면 여기서 아무것도 하지 않는다 (D2).
    //
    // 이 방어가 없으면 위험하다: 아래 줄이 스왑체인이 없을 때
    // HandleLostSwapChain()으로 새로 만드는데, 셸 모드에서 그것이 돌면
    // 같은 HWND에 스왑체인이 둘이 되어 셸 것을 무효화한다. 호출부
    // (Dx11Main)가 이미 걸러 주지만, '한 곳만 어겨도 깨지는' 부류라
    // 소유자 쪽에서도 막는다.
    if (m_presentOwnedExternally) return;

    if (!m_swapChain) { HandleLostSwapChain(); return; }

    // 첫 번째 인수는 DXGI에 VSync까지 차단하도록 지시하여 애플리케이션이
    // 다음 VSync까지 대기하도록 합니다. 이를 통해 화면에 표시되지 않는 프레임을
    // 렌더링하는 주기를 낭비하지 않을 수 있습니다.
    DXGI_PRESENT_PARAMETERS parameters = { 0 };
    HRESULT hr = m_swapChain->Present1(0, 0, &parameters);

    // 렌더링 대상의 콘텐츠를 삭제합니다.
    // 이 작업은 기존 콘텐츠를 완전히 덮어쓸 경우에만
    // 올바릅니다. 변경 또는 스크롤 영역이 사용되는 경우에는 이 호출을 제거해야 합니다.
    m_d3dContext->DiscardView1(m_d3dRenderTargetView.Get(), nullptr, 0);

    // 깊이 스텐실의 콘텐츠를 삭제합니다.
    m_d3dContext->DiscardView1(m_d3dDepthStencilView.Get(), nullptr, 0);

    // 연결이 끊기거나 드라이버 업그레이드로 인해 디바이스가 제거되면 
    // 모든 디바이스 리소스를 다시 만들어야 합니다.
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG)
    {
        //HandleDeviceLost();

        HandleLostSwapChain();
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

DXGI_QUERY_VIDEO_MEMORY_INFO DirectX11::DeviceResources::GetVideoMemoryInfo() const
{
    DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo = {};
    if (m_deviceAdapter)
    {
        ComPtr<IDXGIAdapter3> dxgiAdapter;
        DirectX11::ThrowIfFailed(m_deviceAdapter.As(&dxgiAdapter));
        DirectX11::ThrowIfFailed(dxgiAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo));
    }
    return memoryInfo;
}

void DirectX11::DeviceResources::ReportLiveDeviceObjects()
{
#if defined(_DEBUG)
	if (m_debugDevice != nullptr)
	{
		m_debugDevice->ReportLiveDeviceObjects(D3D11_RLDO_IGNORE_INTERNAL);
	}

    if (m_dxgiDebug != nullptr)
    {
        m_dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
	}
#endif
}

namespace
{
    // 디버그 레이어의 라이브 객체 메시지에서 타입 이름을 뽑는다.
    // 메시지 예: "Live ID3D11Texture2D at 0x000001F2..., Refcount: 1, IntRef: 0"
    std::string ExtractLiveObjectType(std::string_view description)
    {
        constexpr std::string_view marker = "Live ";
        const size_t found = description.find(marker);
        if (found == std::string_view::npos)
        {
            return {};
        }

        size_t begin = found + marker.size();
        size_t end = begin;
        while (end < description.size())
        {
            const unsigned char c = static_cast<unsigned char>(description[end]);
            if (!std::isalnum(c) && c != '_')
            {
                break;
            }
            ++end;
        }

        if (end <= begin)
        {
            return {};
        }
        return std::string(description.substr(begin, end - begin));
    }

    // 집계를 "타입:개수" 목록으로 만든다. 개수가 많은 순으로 상위 항목만 남긴다.
    std::string SummarizeByType(const std::map<std::string, uint32_t>& byType, size_t maxEntries)
    {
        std::vector<std::pair<std::string, uint32_t>> sorted(byType.begin(), byType.end());
        std::sort(sorted.begin(), sorted.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });

        std::string out;
        const size_t limit = (std::min)(maxEntries, sorted.size());
        for (size_t i = 0; i < limit; ++i)
        {
            if (!out.empty()) out += ", ";
            out += sorted[i].first;
            out += ':';
            out += std::to_string(sorted[i].second);
        }
        if (sorted.size() > limit)
        {
            out += " 외 " + std::to_string(sorted.size() - limit) + "종";
        }
        return out;
    }
}

DirectX11::GpuObjectCensus DirectX11::DeviceResources::CaptureLiveObjectCensus(bool allowDeviceEnumeration)
{
    GpuObjectCensus census{};

    // VRAM 사용량은 디버그 레이어 유무와 무관하게 수집할 수 있다.
    const DXGI_QUERY_VIDEO_MEMORY_INFO memory = GetVideoMemoryInfo();
    constexpr uint64_t megabyte = 1024ull * 1024ull;
    census.vramUsedMB = memory.CurrentUsage / megabyte;
    census.vramBudgetMB = memory.Budget / megabyte;

#if defined(_DEBUG)
    // 커맨드 빌드/실행 스레드가 도는 중에 디바이스 자식을 순회하면 순회 도중
    // 목록이 바뀌어 접근 위반으로 죽는다. 호출자가 렌더 스레드 정지를 보장한
    // 경우에만 순회한다.
    if (!allowDeviceEnumeration)
    {
        return census;
    }

    if (m_debugDevice == nullptr || m_infoQueue == nullptr)
    {
        return census;
    }

    // ReportLiveDeviceObjects는 살아있는 객체 하나당 메시지 한 건을 InfoQueue에 넣는다.
    // 직전 메시지가 섞이지 않도록 비우고 호출한 뒤 결과만 읽는다.
    m_infoQueue->ClearStoredMessages();
    m_debugDevice->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);

    const UINT64 messageCount = m_infoQueue->GetNumStoredMessages();
    std::vector<uint8_t> buffer;

    for (UINT64 i = 0; i < messageCount; ++i)
    {
        SIZE_T length = 0;
        if (FAILED(m_infoQueue->GetMessage(i, nullptr, &length)) || length == 0)
        {
            continue;
        }

        buffer.resize(length);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(buffer.data());
        if (FAILED(m_infoQueue->GetMessage(i, message, &length)) || message->pDescription == nullptr)
        {
            continue;
        }

        // DescriptionByteLength는 널 종료 문자를 포함한다.
        size_t descriptionLength = message->DescriptionByteLength;
        if (descriptionLength > 0 && message->pDescription[descriptionLength - 1] == '\0')
        {
            --descriptionLength;
        }

        const std::string type = ExtractLiveObjectType(
            std::string_view(message->pDescription, descriptionLength));
        if (type.empty())
        {
            continue;
        }

        ++census.byType[type];
        ++census.totalObjects;
    }

    m_infoQueue->ClearStoredMessages();
    census.available = true;
#endif

    return census;
}

void DirectX11::DeviceResources::LogLiveObjectCensus(std::string_view label, bool allowDeviceEnumeration)
{
    LogCensus(CaptureLiveObjectCensus(allowDeviceEnumeration), label);
}

void DirectX11::DeviceResources::LogCensus(const GpuObjectCensus& census, std::string_view label)
{
    if (!census.available)
    {
        // 디버그 레이어를 못 쓰는 실행 중에는 엔진이 직접 센 에셋 수로 대신한다.
        Debug->Log(std::format("[GPU 진단] {} · VRAM {}MB / {}MB · 엔진 에셋 {}",
            label, census.vramUsedMB, census.vramBudgetMB,
            Diagnostics::FormatSnapshot(Diagnostics::CaptureResourceSnapshot())));
        return;
    }

    Debug->Log(std::format("[GPU 진단] {} · 라이브 객체 {}개 · VRAM {}MB / {}MB",
        label, census.totalObjects, census.vramUsedMB, census.vramBudgetMB));
    Debug->Log(std::format("[GPU 진단]   타입별: {}", SummarizeByType(census.byType, 12)));
}

void DirectX11::DeviceResources::ResetLiveObjectBaseline()
{
    m_baselineCensus = CaptureLiveObjectCensus();
    m_baselineResources = Diagnostics::CaptureResourceSnapshot();
    m_hasBaseline = true;
}

void DirectX11::DeviceResources::LogLiveObjectDelta(std::string_view label, bool allowDeviceEnumeration)
{
    LogLiveObjectDeltaFrom(CaptureLiveObjectCensus(allowDeviceEnumeration), label);
}

void DirectX11::DeviceResources::LogLiveObjectDeltaFrom(const GpuObjectCensus& current, std::string_view label)
{
    const Diagnostics::ResourceSnapshot resources = Diagnostics::CaptureResourceSnapshot();

    if (!m_hasBaseline)
    {
        m_baselineCensus = current;
        m_baselineResources = resources;
        m_hasBaseline = true;
        LogCensus(current, label);
        return;
    }

    const int64_t objectDelta =
        static_cast<int64_t>(current.totalObjects) - static_cast<int64_t>(m_baselineCensus.totalObjects);
    const int64_t vramDelta =
        static_cast<int64_t>(current.vramUsedMB) - static_cast<int64_t>(m_baselineCensus.vramUsedMB);

    if (!current.available)
    {
        // 실행 중 경로. 씬을 오갔을 때 이 에셋 수가 제자리로 돌아오는지가 핵심 지표다.
        Debug->Log(std::format("[GPU 진단] {} · VRAM {}MB ({:+}MB) · 엔진 에셋 {}",
            label, current.vramUsedMB, vramDelta,
            Diagnostics::FormatDelta(resources, m_baselineResources)));

        m_baselineCensus = current;
        m_baselineResources = resources;
        return;
    }

    // 증가한 타입만 추린다. 회수되지 않은 리소스가 여기에 그대로 드러난다.
    std::map<std::string, uint32_t> increased;
    for (const auto& [type, count] : current.byType)
    {
        const auto previous = m_baselineCensus.byType.find(type);
        const uint32_t before = (previous == m_baselineCensus.byType.end()) ? 0u : previous->second;
        if (count > before)
        {
            increased[type] = count - before;
        }
    }

    Debug->Log(std::format("[GPU 진단] {} · 라이브 객체 {}개 ({:+}) · VRAM {}MB ({:+}MB) · 엔진 에셋 {}",
        label, current.totalObjects, objectDelta, current.vramUsedMB, vramDelta,
        Diagnostics::FormatDelta(resources, m_baselineResources)));

    if (increased.empty())
    {
        Debug->Log("[GPU 진단]   증가한 객체 타입 없음");
    }
    else
    {
        // 누수 후보이므로 경고 레벨로 남긴다(경고 이상은 즉시 디스크에 기록된다).
        Debug->LogWarning(std::format("[GPU 진단]   증가: {}", SummarizeByType(increased, 12)));
    }

    m_baselineCensus = current;
    m_baselineResources = resources;
}

bool DirectX11::DeviceResources::CheckHDRSupport(ComPtr<IDXGIAdapter> adapter)
{
	bool isHDRSupported = false;
    UINT outputIndex = 0;
    ComPtr<IDXGIOutput> output;

    while (adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND)
    {
        // IDXGIOutput → IDXGIOutput6으로 업캐스팅
        ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6)))
        {
            DXGI_OUTPUT_DESC1 desc1;
            if (SUCCEEDED(output6->GetDesc1(&desc1)))
            {
                if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
                {
					isHDRSupported = true;
                }
            }
        }

        output.Reset();
        outputIndex++;
    }

	return isHDRSupported;
}

void DirectX11::DeviceResources::CreateDeviceIndependentResources()
{

}

void DirectX11::DeviceResources::CreateDeviceResources()
{
    // 이 플래그는 API 기본값과 다른 색 채널 순서의 표면에 대한 지원을
    // 추가합니다. Direct2D와의 호환성을 위해 필요합니다.
    UINT creationFlags = D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT;

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

#if defined(_DEBUG)
	// 디버그 레이어를 사용하여 디바이스를 만들면 디버그 레이어에 대한 포인터를 가져옵니다.
    DirectX11::ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_debugDevice)));

    DirectX11::ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&m_infoQueue)));
    {
        // WARNING 메시지에 Breakpoint
        m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);

        // ERROR 메시지에 Breakpoint
        m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);

        // CORRUPTION 메시지에 Breakpoint (메모리 손상, 치명적)
        m_infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);

        // 라이브 객체 조사(CaptureLiveObjectCensus)는 객체 1개당 메시지 1건을 만든다.
        // 기본 저장 한도(1024)로는 리소스가 많을 때 잘려 집계가 왜곡되므로 한도를 푼다.
        m_infoQueue->SetMessageCountLimit(0xFFFFFFFFFFFFFFFFull);

        // Release()를 호출하지 않는다.
        // QueryInterface가 올린 참조는 ComPtr 소멸자가 내리므로,
        // 여기서 수동 Release하면 참조가 하나 모자라 디바이스가 조기 파괴된다.
    }

    DirectX11::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiDebug)));
    {
        DirectX11::ThrowIfFailed(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&m_dxgiInfoQueue)));

        // WARNING 메시지에 Breakpoint
		m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, FALSE);

        // ERROR 메시지에 Breakpoint
        m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);

        // CORRUPTION 메시지에 Breakpoint (메모리 손상, 치명적)
        m_dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);

        // 위와 같은 이유로 수동 Release를 하지 않는다.
    }
#endif

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
    // 디버그용 이벤트 추가용 m_annotation 객체 생성
    DirectX11::ThrowIfFailed(
		m_d3dContext->QueryInterface(IID_PPV_ARGS(&m_annotation))
    );
}

void DirectX11::DeviceResources::CreateWindowSizeDependentResources()
{
    // 이전 창 크기와 관련된 컨텍스트를 지웁니다.
    ID3D11RenderTargetView* nullViews[] = { nullptr };
    m_d3dContext->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
    m_d3dRenderTargetView = nullptr;
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
            m_supportHDR ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM,
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
        DirectX11::ThrowIfFailed(
            m_d3dDevice.As(&m_dxgiDevice)
        );

        DirectX11::ThrowIfFailed(
            m_dxgiDevice->GetAdapter(&m_deviceAdapter)
        );

        m_supportHDR = CheckHDRSupport(m_deviceAdapter);

        ComPtr<IDXGIFactory2> dxgiFactory;
        DirectX11::ThrowIfFailed(
            m_deviceAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory))
        );

        DXGI_SCALING scaling = DisplayMetrics::SupportHighResolutions ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };

        m_d3dRenderTargetSize.width = m_logicalSize.width;
        m_d3dRenderTargetSize.height = m_logicalSize.height;

        if (m_window)
        {
            m_window->EnsureSwapChainCompatibleStyle();
        }

        swapChainDesc.Width = lround(m_d3dRenderTargetSize.width);		// 창의 크기를 맞춥니다.
        swapChainDesc.Height = lround(m_d3dRenderTargetSize.height);
        swapChainDesc.Format = m_supportHDR ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;				// 가장 일반적인 스왑 체인 형식입니다.
        swapChainDesc.Stereo = false;
        swapChainDesc.SampleDesc.Count = 1;								// 다중 샘플링을 사용하지 마십시오.
        swapChainDesc.SampleDesc.Quality = 0;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
        swapChainDesc.BufferCount = 3;									// 이중 버퍼링을 사용하여 대기 시간을 최소화합니다.
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;//DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        swapChainDesc.Scaling = scaling;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDesc = { 0 };

        swapChainFullscreenDesc.RefreshRate.Numerator = 0;
        swapChainFullscreenDesc.RefreshRate.Denominator = 1;
        swapChainFullscreenDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
        swapChainFullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        swapChainFullscreenDesc.Windowed = FALSE;


        // ── 스왑체인·백버퍼는 프레젠트를 소유할 때만 만든다 (D2) ──
        //
        // DXGI는 한 HWND에 스왑체인 둘을 허용하지 않는다. 셸(ImGui DX12)이
        // 창을 가져가기로 했으면 여기서 만들지 않아야 하고, 그러지 않으면
        // 나중에 붙는 쪽이 이쪽을 무효화해 종료가 크래시한다.
        //
        // ★ 건너뛰는 것은 스왑체인·백버퍼·RTV 셋뿐이다. 그 아래의 깊이
        //   스텐실·상태 객체·뷰포트는 창 크기에서 나오는 것이라 프레젠트
        //   소유와 무관하고, DX12 라이브가 g_Viewport·g_ClientRect를 읽으므로
        //   함께 건너뛰면 씬 렌더링이 멈춘다.
        if (!m_presentOwnedExternally)
        {
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

            //dxgiFactory->MakeWindowAssociation(
            //    m_window->GetHandle(),
            //    DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER
            //);

            DirectX11::ThrowIfFailed(
                swapChain.As(&m_swapChain)
            );

            DirectX::SetName(m_swapChain.Get(), "IDXGISwapChain1");

            m_swapChain->SetColorSpace1(
                m_supportHDR
                ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020  // HDR10
                : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709     // SDR
            );

            ComPtr<ID3D11Texture2D1> backBuffer;
            DirectX11::ThrowIfFailed(
                m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))
            );

            D3D11_RENDER_TARGET_VIEW_DESC1 renderTargetViewDesc = {};
            renderTargetViewDesc.Format = swapChainDesc.Format;
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
            //백버퍼 텍스쳐 받아오기
            ID3D11Resource* pResource = nullptr;
            m_d3dRenderTargetView->GetResource(&pResource);
            DirectX11::ThrowIfFailed(
                pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&m_backBuffer)
            );
        }

        // 프레임 지연은 스왑체인과 무관한 디바이스 설정이라 양쪽 모두 건다.
        DirectX11::ThrowIfFailed(
            m_dxgiDevice->SetMaximumFrameLatency(3)
        );

        // 필요한 경우 3D 렌더링에 사용할 깊이 스텐실 뷰를 만듭니다.
        CD3D11_TEXTURE2D_DESC1 depthStencilDesc(
            DXGI_FORMAT_R24G8_TYPELESS,
            lround(m_d3dRenderTargetSize.width),
            lround(m_d3dRenderTargetSize.height),
            1, // 이 깊이 스텐실 뷰는 하나의 질감만 가지고 있습니다.
            1, // 단일 MIP 맵 수준을 사용합니다.
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

		DirectX::SetName(m_DepthStencilViewSRV.Get(), "DepthStencilViewSRV");

        CD3D11_RASTERIZER_DESC rasterizerDesc = CD3D11_RASTERIZER_DESC( D3D11_DEFAULT );
		DirectX11::ThrowIfFailed(
			m_d3dDevice->CreateRasterizerState(
				&rasterizerDesc,
				&m_rasterizerState
			)
		);

		CD3D11_BLEND_DESC blendDesc = CD3D11_BLEND_DESC(CD3D11_DEFAULT());
		blendDesc.RenderTarget[0].BlendEnable           = TRUE;
		blendDesc.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
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

void DirectX11::DeviceResources::HandleLostSwapChain()
{
    // 소유하지 않으면 되살리지 않는다 (D2). 이 함수는 스왑체인을 새로
    // 만드는 것이 전부라, 셸 모드에서 돌면 창에 두 번째 스왑체인이 생긴다.
    if (m_presentOwnedExternally) return;

    ComPtr<IDXGIFactory2> dxgiFactory;
    DirectX11::ThrowIfFailed(
        m_deviceAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory))
    );

    DXGI_SCALING scaling = DisplayMetrics::SupportHighResolutions ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };

    m_d3dRenderTargetSize.width = m_logicalSize.width;
    m_d3dRenderTargetSize.height = m_logicalSize.height;

    if (m_window)
    {
        m_window->EnsureSwapChainCompatibleStyle();
    }

    swapChainDesc.Width = lround(m_d3dRenderTargetSize.width);		// 창의 크기를 맞춥니다.
    swapChainDesc.Height = lround(m_d3dRenderTargetSize.height);
    swapChainDesc.Format = m_supportHDR ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;				// 가장 일반적인 스왑 체인 형식입니다.
    swapChainDesc.Stereo = false;
    swapChainDesc.SampleDesc.Count = 1;								// 다중 샘플링을 사용하지 마십시오.
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    swapChainDesc.BufferCount = 3;									// 이중 버퍼링을 사용하여 대기 시간을 최소화합니다.
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;//DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = 0;
    swapChainDesc.Scaling = scaling;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC swapChainFullscreenDesc = { 0 };

    swapChainFullscreenDesc.RefreshRate.Numerator = 0;
    swapChainFullscreenDesc.RefreshRate.Denominator = 1;
    swapChainFullscreenDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
    swapChainFullscreenDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swapChainFullscreenDesc.Windowed = FALSE;

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

    m_swapChain->SetColorSpace1(
        m_supportHDR
        ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020  // HDR10
        : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709     // SDR
    );

    DirectX::SetName(m_swapChain.Get(), "IDXGISwapChain1");

    DirectX11::ThrowIfFailed(
        m_dxgiDevice->SetMaximumFrameLatency(3)
    );

    ComPtr<ID3D11Texture2D1> backBuffer;
    DirectX11::ThrowIfFailed(
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))
    );

    D3D11_RENDER_TARGET_VIEW_DESC1 renderTargetViewDesc = {};
    renderTargetViewDesc.Format = swapChainDesc.Format;
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
    //백버퍼 텍스쳐 받아오기
    ID3D11Resource* pResource = nullptr;
    m_d3dRenderTargetView->GetResource(&pResource);
    DirectX11::ThrowIfFailed(
        pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&m_backBuffer)
    );
    DirectX11::DeviceStates->g_backBufferRTV = m_d3dRenderTargetView.Get();
}
#endif // !DYNAMICCPP_EXPORTS