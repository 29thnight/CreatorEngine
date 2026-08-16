#ifndef DYNAMICCPP_EXPORTS
#include "DX12DeviceResources.h"
#include "DX12Encoder.h"   // A-3 — 즉시 인코더의 실물. 헤더는 이름만 안다
#include <vector>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <dxgidebug.h>   // IDXGIDebug — 종료 시 라이브 객체 보고(프로세스 범위)

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace
{
    enum class ValidationMode
    {
        Off,
        Basic,
        Gpu
    };

    std::string ReadEnvironment(const char* name)
    {
        const DWORD length = GetEnvironmentVariableA(name, nullptr, 0);
        if (0 == length) return {};

        std::string value(length, '\0');
        GetEnvironmentVariableA(name, value.data(), length);
        value.resize(length - 1);
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool ReadEnvironmentFlag(const char* name, bool fallback)
    {
        const std::string value = ReadEnvironment(name);
        if (value.empty()) return fallback;
        if (value == "1" || value == "true" || value == "on" || value == "yes") return true;
        if (value == "0" || value == "false" || value == "off" || value == "no") return false;
        return fallback;
    }

    ValidationMode ReadValidationMode()
    {
#if defined(_DEBUG)
        constexpr ValidationMode fallback = ValidationMode::Basic;
#else
        constexpr ValidationMode fallback = ValidationMode::Off;
#endif
        const std::string value = ReadEnvironment("CREATOR_DX12_VALIDATION");
        if (value.empty()) return fallback;
        if (value == "off" || value == "0" || value == "false") return ValidationMode::Off;
        if (value == "gpu" || value == "full" || value == "2") return ValidationMode::Gpu;
        if (value == "basic" || value == "standard" || value == "1" || value == "true")
        {
            return ValidationMode::Basic;
        }
        return fallback;
    }

    std::string WideToUtf8(const wchar_t* value)
    {
        if (nullptr == value || L'\0' == *value) return {};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (bytes <= 1) return {};
        std::string result(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), bytes, nullptr, nullptr);
        result.resize(static_cast<size_t>(bytes - 1));
        return result;
    }

    std::string DebugName(const char* narrow, const wchar_t* wide)
    {
        if (nullptr != narrow && '\0' != *narrow) return narrow;
        const std::string converted = WideToUtf8(wide);
        return converted.empty() ? "<unnamed>" : converted;
    }

    std::string HrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }

    uint64_t DX12UploadSoftBudget(const RHIUploadMemoryBudget& budget,
        uint64_t currentSegmentBytes)
    {
        constexpr uint64_t MiB = 1024ull * 1024ull;
        constexpr uint64_t kMinimum = 64ull * MiB;
        constexpr uint64_t kMaximum = 512ull * MiB;
        constexpr uint64_t kFallback = 256ull * MiB;
        if (!budget.IsValid()) return kFallback;

        const uint64_t headroom = budget.budgetBytes > budget.usageBytes
            ? budget.budgetBytes - budget.usageBytes : 0;
        const uint64_t candidate = currentSegmentBytes + headroom / 8;
        return (std::min)(kMaximum, (std::max)(kMinimum, candidate));
    }

    bool DX12UploadMemoryPressure(const RHIUploadMemoryBudget& budget,
        bool wasPressured)
    {
        if (!budget.IsValid()) return false;
        const uint64_t releaseThreshold = budget.budgetBytes - budget.budgetBytes / 5;
        const uint64_t enterThreshold = budget.budgetBytes - budget.budgetBytes / 10;
        return wasPressured
            ? budget.usageBytes > releaseThreshold
            : budget.usageBytes >= enterThreshold;
    }
}

bool DX12DeviceResources::CreateSizeDependentResources(uint32_t width, uint32_t height,
    std::string& outError)
{
    m_width = width;
    m_height = height;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rtDesc{};
    rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtDesc.Width = width;
    rtDesc.Height = height;
    rtDesc.DepthOrArraySize = 1;
    rtDesc.MipLevels = 1;
    rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtDesc.SampleDesc.Count = 1;
    rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = rtDesc.Format;
    for (int i = 0; i < 4; ++i) clearValue.Color[i] = kClearColor[i];

    HRESULT hr = m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &rtDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&m_renderTarget));
    if (FAILED(hr)) { outError = "렌더 타깃 생성 실패 " + HrToString(hr); return false; }

    m_rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_renderTarget.Get(), nullptr, m_rtvHandle);

    // ── 프레임 리드백(행 정렬은 CreateReadback이 한다, R2c-b2) ──
    //
    // 손으로 만들던 것을 자기 인터페이스로 돌렸다. 행 간격 계산과 힙 설명이
    // 여기서 사라지고, 쓰는 쪽(자가 검증 셋)도 GetRowPitch로 산술을 하지
    // 않는다 — 리드백이 스스로 안다.
    if (!CreateReadback(width, height, RHIFormat::RGBA8Unorm, 1,
        m_frameReadback, outError))
    {
        return false;
    }

    return true;
}

bool DX12DeviceResources::Resize(uint32_t width, uint32_t height, std::string& outError)
{
    if (!IsInitialized())
    {
        outError = "초기화되지 않은 디바이스에 리사이즈를 걸었다";
        return false;
    }
    if (0 == width || 0 == height) return true;
    if (width == m_width && height == m_height) return true;

    // GPU가 옛 타깃을 읽는 중일 수 있다. 놓기 전에 비운다 — DX11 쪽은 스왑체인
    // 제약 때문에 뷰를 먼저 놓아야 했지만, 여기서는 이유가 다르다(수명).
    if (!DrainForLifecycle(RHILifecycleCommand::SwapChainResize, outError))
        return false;

    m_renderTarget.Reset();
    ReleaseReadback(m_frameReadback);
    m_frameReadback = RHIReadback{};

    return CreateSizeDependentResources(width, height, outError);
}

bool DX12DeviceResources::Initialize(uint32_t width, uint32_t height, std::string& outError,
    LUID matchAdapterLuid, bool followScreenSize)
{
    m_width = width;
    m_height = height;

    const ValidationMode validationMode = ReadValidationMode();
    const bool debugLayerEnabled = ValidationMode::Off != validationMode;
    const bool gpuValidationEnabled = ValidationMode::Gpu == validationMode;
    const bool dredEnabled = ReadEnvironmentFlag("CREATOR_DX12_DRED", debugLayerEnabled);
    const bool breakOnError = ReadEnvironmentFlag("CREATOR_DX12_BREAK_ON_ERROR", false);

    // 검증 레이어는 디바이스 생성 전에 켜야 한다. 브링업 단계에서는 이 레이어가
    // 곧 테스트 하네스다 — 메시지 0건이 통과 조건.
    // CREATOR_DX12_VALIDATION=off|basic|gpu로 선택한다. Debug 기본은 basic이고,
    // Release 기본은 off라 배포 실행의 성능을 바꾸지 않는다.
    if (debugLayerEnabled)
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();

            if (gpuValidationEnabled)
            {
                Microsoft::WRL::ComPtr<ID3D12Debug1> debug1;
                if (SUCCEEDED(debug.As(&debug1)))
                {
                    debug1->SetEnableGPUBasedValidation(TRUE);
                    debug1->SetEnableSynchronizedCommandQueueValidation(TRUE);
                }
            }
        }
        else
        {
            outError = "D3D12 Debug Layer를 열 수 없다. Windows Graphics Tools 설치를 확인한다";
            return false;
        }
    }

    // DRED 설정 역시 디바이스 생성 전에 해야 한다. 장치 제거 뒤에 켜려 하면 이미
    // 필요한 breadcrumb와 page-fault 기록이 사라진 뒤다.
    if (dredEnabled)
    {
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
        {
            dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }

    std::printf("[DX12 검증] DebugLayer=%s GPUValidation=%s DRED=%s BreakOnError=%s\n",
        debugLayerEnabled ? "on" : "off",
        gpuValidationEnabled ? "on" : "off",
        dredEnabled ? "on" : "off",
        breakOnError ? "on" : "off");

    const UINT factoryFlags = debugLayerEnabled ? DXGI_CREATE_FACTORY_DEBUG : 0;
    HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) { outError = "DXGI 팩토리 생성 실패 " + HrToString(hr); return false; }

    // 어댑터 선택.
    //
    // LUID를 받았으면 그 어댑터를 찾는다 — 공유 텍스처는 같은 물리 어댑터에서만
    // 성립하므로, DX11과 짝을 맞추는 것이 성능 정책보다 우선이다.
    // 받지 않았으면(독립 자가 검증 등) 고성능 우선.
    if (0 != matchAdapterLuid.LowPart || 0 != matchAdapterLuid.HighPart)
    {
        hr = m_factory->EnumAdapterByLuid(matchAdapterLuid, IID_PPV_ARGS(&m_adapter));
        if (FAILED(hr))
        {
            outError = "지정 LUID의 어댑터를 찾지 못했다 " + HrToString(hr);
            return false;
        }
    }
    else
    {
        hr = m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&m_adapter));
        if (FAILED(hr)) { outError = "어댑터 열거 실패 " + HrToString(hr); return false; }
    }

    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) { outError = "D3D12 디바이스 생성 실패 " + HrToString(hr); return false; }

    // 알려진 정상 경로 메시지를 억제한다.
    //
    // LOADPIPELINE_NAMENOTFOUND는 파이프라인 라이브러리의 '첫 조회 실패'이고,
    // 캐시 미스는 설계된 동작이다(그다음에 컴파일해서 저장한다). 그런데 검증
    // 레이어가 이것을 WARNING으로 올려서, 심각도 필터만으로는 걸러지지 않는다.
    //
    // 심각도를 통째로 낮추는 대신 이 ID 하나만 막는다 — 억제 목록이 길어지는 것은
    // 그 자체로 신호이므로, 새 항목을 넣을 때는 왜 정상인지를 여기 적을 것.
    {
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue)))
        {
            if (breakOnError)
            {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            }

            D3D12_MESSAGE_ID denied[] = { D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(denied);
            filter.DenyList.pIDList = denied;
            infoQueue->AddStorageFilterEntries(&filter);
        }
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
    if (FAILED(hr)) { outError = "커맨드 큐 생성 실패 " + HrToString(hr); return false; }

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_allocators[i]));
        if (FAILED(hr)) { outError = "커맨드 얼로케이터 생성 실패 " + HrToString(hr); return false; }
    }

    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr)) { outError = "커맨드 리스트 생성 실패 " + HrToString(hr); return false; }
    m_commandList->Close(); // BeginFrame이 여는 것이 규약 — 생성 직후는 닫아 둔다
    m_immediateEncoder.reset();

    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    if (FAILED(hr)) { outError = "펜스 생성 실패 " + HrToString(hr); return false; }

    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (nullptr == m_fenceEvent) { outError = "펜스 이벤트 생성 실패"; return false; }

    // ── 오프스크린 렌더 타깃 ──
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = 1;
    hr = m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap));
    if (FAILED(hr)) { outError = "RTV 힙 생성 실패 " + HrToString(hr); return false; }

    // 크기에 딸린 것은 한 곳에 모은다 — 리사이즈가 같은 코드를 다시 탄다.
    if (!CreateSizeDependentResources(width, height, outError)) return false;

    // 완료점 기반 transient upload segment pool. 대형 요청은 첫 요청에서
    // 즉시 전용 세그먼트를 만들며 다음 프레임 성장을 기다리지 않는다.
    constexpr uint64_t kRegularUploadSegmentBytes = 16ull * 1024 * 1024;
    constexpr uint64_t kLargeUploadThreshold = 8ull * 1024 * 1024;
    constexpr uint32_t kStandbyRegularSegments = 3;
    RHIUploadSegmentPolicy uploadPolicy{};
    uploadPolicy.regularSegmentBytes = kRegularUploadSegmentBytes;
    uploadPolicy.largeThreshold = kLargeUploadThreshold;
    uploadPolicy.standbyRegularSegments = kStandbyRegularSegments;
    uploadPolicy.largeCacheBudgetBytes = 64ull * 1024 * 1024;
    uploadPolicy.softBudgetBytes = DX12UploadSoftBudget(QueryUploadMemoryBudget(), 0);
    if (!m_uploadAllocator.Initialize(m_device.Get(), m_resourceTable,
        uploadPolicy, outError))
    {
        return false;
    }

    // shader-visible descriptor page·샘플러 힙. page는 recording 단위로
    // 제출하고 실제 peakRecordingDescriptors로 용량을 조정한다.
    constexpr uint32_t kDescriptorsPerPage = 4096;
    if (!m_descriptorRecycler.Initialize(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        kDescriptorsPerPage, kFrameCount, outError))
    {
        return false;
    }

    constexpr uint32_t kSamplerCapacity = 128;
    if (!m_samplerHeap.Initialize(m_device.Get(), kSamplerCapacity, outError))
    {
        return false;
    }

    // RTV/DSV 힙(R2b). 프레임 구간을 나누지 않으므로 용량은 '한 프레임에
    // 만드는 최대 개수'다. 지금 상한을 세어 보면 GBuffer 5 × 조각 8 = 40이
    // 가장 크고 나머지 패스는 대개 1~3이라, 여유를 두 자릿수로 잡았다.
    // 정확한 값은 peakFrameDescriptors가 알려 준다 — 추정하지 말고 재서 줄인다.
    constexpr uint32_t kRtvCapacity = 512;
    constexpr uint32_t kDsvCapacity = 256;
    if (!m_rtvViewHeap.Initialize(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        kRtvCapacity, outError))
    {
        return false;
    }
    if (!m_dsvViewHeap.Initialize(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        kDsvCapacity, outError))
    {
        return false;
    }

    // ClearUnorderedAccessViewFloat 전용 비가시 UAV 힙(R3). 쓰는 자리가
    // 지금은 포그 볼륨 셋뿐이라 작게 잡는다 — 그 셋이 자기 힙을 들고 있던
    // 것을 이리로 합쳤다(R3-2).
    constexpr uint32_t kClearViewCapacity = 64;
    if (!m_clearViewHeap.Initialize(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        kClearViewCapacity, outError))
    {
        return false;
    }

    // 창을 따라가겠다고 선언했으면 버스를 구독한다.
    //
    // 해제 단계에서는 아무것도 하지 않는다. 그쪽은 DX11 스왑체인이 백버퍼
    // 참조를 먼저 놓아야 한다는 제약 때문에 있는 단계이고, 오프스크린 타깃만
    // 쓰는 이쪽에는 해당이 없다 — Resize가 스스로 GPU를 기다린 뒤 놓는다.
    if (followScreenSize)
    {
        m_resizeSubscription = ScreenResizeBus::Get().Subscribe(
            nullptr,
            [this](uint32_t newWidth, uint32_t newHeight)
            {
                std::string resizeError;
                if (!Resize(newWidth, newHeight, resizeError))
                {
                    OutputDebugStringA(("[DX12] 리사이즈 실패: " + resizeError + "\n").c_str());
                }
            });
    }

    RefreshPersistentMemoryBudget();
    if (!GetRHISubmissionThread().AcquireClient(this, outError)) return false;
    m_submissionClient = true;
    return true;
}

void DX12DeviceResources::Shutdown()
{
    // 구독을 먼저 끊는다. 해체 중에 리사이즈 콜백이 들어오면 이미 놓은 것을
    // 만진다.
    ScreenResizeBus::Get().Unsubscribe(m_resizeSubscription);
    m_resizeSubscription = ScreenResizeBus::kInvalidHandle;

    if (m_device && m_submissionClient)
    {
        std::string lifecycleError;
        const RHISubmissionOwnerStats owner =
            GetRHISubmissionThread().GetOwnerStats(this);
        const RHILifecycleCommand command = owner.faulted
            ? RHILifecycleCommand::UnrecoverableDeviceError
            : RHILifecycleCommand::BackendShutdown;
        if (!DrainForLifecycle(command, lifecycleError) && !lifecycleError.empty())
        {
            OutputDebugStringA(("[DX12] lifecycle shutdown 실패: " +
                lifecycleError + "\n").c_str());
        }
    }
    if (m_submissionClient)
    {
        GetRHISubmissionThread().ReleaseClient(this);
        m_submissionClient = false;
    }

    // 스왑체인은 백버퍼 참조를 먼저 놓아야 곱게 죽는다(GPU 완주는 위에서 확인).
    for (auto& backBuffer : m_backBuffers) backBuffer.Reset();
    m_backBufferRtvHeap.Reset();
    m_swapChain.Reset();

    // GPU가 다 끝난 뒤에 Unmap한다. 순서가 반대면 아직 읽는 중인 메모리를 푼다.
    m_uploadAllocator.Shutdown();
    m_descriptorRecycler.Shutdown();
    m_samplerHeap.Shutdown();
    m_rtvViewHeap.Shutdown();
    m_dsvViewHeap.Shutdown();
    m_clearViewHeap.Shutdown();
    m_uploadMemoryPressure = false;
    m_persistentMemoryBudget.Reset();
    m_commandList.Reset();
    for (auto& lists : m_retiredCommandLists) lists.clear();
    m_immediateEncoder.reset();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    // ComPtr가 역순 해제를 처리한다. 멤버 선언 순서가 곧 해제 역순이다.
}

// ── 즉시 인코더 (A-3) ──
//
// 소멸자가 여기 있는 이유는 멤버가 불완전 타입(unique_ptr<DX12Encoder>)이기
// 때문이다. 헤더에서 인코더를 물면 방향이 거꾸로가 된다 — 인코더가 이
// 클래스를 알아야지 그 반대가 아니다.
DX12DeviceResources::DX12DeviceResources() = default;
DX12DeviceResources::~DX12DeviceResources()
{
    // 성공 초기화 뒤 호출부가 명시 Shutdown을 놓쳐도 process-wide RHI client와
    // owner 작업이 남지 않게 한다. 정상 명시 해체 뒤에는 flag가 false라 no-op이다.
    if (m_submissionClient) Shutdown();
}

void DX12DeviceResources::RegisterUploadTransactionListener(
    IRHIUploadTransactionListener* listener)
{
    if (nullptr == listener) return;
    if (m_uploadTransactionListeners.end() == std::find(
        m_uploadTransactionListeners.begin(), m_uploadTransactionListeners.end(), listener))
        m_uploadTransactionListeners.push_back(listener);
}

void DX12DeviceResources::UnregisterUploadTransactionListener(
    IRHIUploadTransactionListener* listener)
{
    const auto found = std::remove(m_uploadTransactionListeners.begin(),
        m_uploadTransactionListeners.end(), listener);
    m_uploadTransactionListeners.erase(found, m_uploadTransactionListeners.end());
}

void DX12DeviceResources::ResetImmediateEncoder()
{
    // 리스트를 Reset 할 때마다 새로 만든다. 들고 있으면 인코더가 기억하는
    // 디스크립터 힙 바인딩이 낡는다 — Reset 이 그것을 푸는데 인코더는
    // '이미 걸었다'로 남아, 다음 테이블 바인딩이 힙 없이 나간다.
    m_immediateEncoder = std::make_unique<DX12Encoder>(m_commandList.Get(), this);
}

RHIEncoder& DX12DeviceResources::GetImmediateEncoder()
{
    // ★ 부를 때마다 상태 기억을 비운다 (G-2a). A-3 은 "BeginFrame 이 다시
    //   만든다"였는데, 그래프가 **패스마다** 이것을 받게 되면서 계약을 좁혔다 —
    //   기억이 패스 경계를 넘으면 안 되기 때문이다(예전에는 그래프가 패스마다
    //   `DX12Encoder` 를 새로 만들어 수명으로 막던 자리).
    //
    //   커맨드 리스트 포인터도 여기서 다시 받는다. `ID3D12GraphicsCommandList::
    //   Reset()` 은 같은 객체를 재사용하므로 포인터는 안 바뀌지만, 그 사실에
    //   기대지 않는다.
    if (nullptr == m_immediateEncoder) ResetImmediateEncoder();
    else m_immediateEncoder->ResetState(m_commandList.Get());
    return *m_immediateEncoder;
}

bool DX12DeviceResources::BeginFrame(std::string& outError)
{
    RHISubmissionThread& submission = GetRHISubmissionThread();
    if (submission.ConsumeFailure(this, outError)) return false;
    if (m_frameSubmissionTickets[m_frameIndex].IsValid() &&
        !submission.Wait(m_frameSubmissionTickets[m_frameIndex], outError))
    {
        return false;
    }

    auto& allocator = m_allocators[m_frameIndex];

    // 이 얼로케이터로 기록했던 프레임을 GPU가 끝냈는지 — 인플라이트 회전의 핵심.
    const uint64_t completed = m_fence->GetCompletedValue();
    if (completed < m_frameFenceValues[m_frameIndex])
    {
        HRESULT hr = m_fence->SetEventOnCompletion(m_frameFenceValues[m_frameIndex], m_fenceEvent);
        if (FAILED(hr)) { outError = "펜스 대기 설정 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    HRESULT hr = allocator->Reset();
    if (FAILED(hr)) { outError = "얼로케이터 Reset 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    bool created = false;
    if (!m_commandList)
    {
        auto& retired = m_retiredCommandLists[m_frameIndex];
        if (!retired.empty())
        {
            m_commandList = retired.front();
            retired.clear();
        }
        else
        {
            hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList));
            if (FAILED(hr))
            {
                outError = "커맨드 리스트 생성 실패 " + HrToString(hr);
                AppendDeviceRemovedReport(hr, outError);
                return false;
            }
            created = true;
        }
    }
    if (!created)
    {
        hr = m_commandList->Reset(allocator.Get(), nullptr);
        if (FAILED(hr)) { outError = "커맨드 리스트 Reset 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }
    }
    ResetImmediateEncoder();

    // frame index가 아니라 실제 fence 완료값으로 업로드 세그먼트를 회수한다.
    RefreshUploadBudget();
    RefreshPersistentMemoryBudget();
    m_uploadAllocator.Collect(m_fence->GetCompletedValue());
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadCompleted(m_fence->GetCompletedValue());
    m_currentRecordingId = m_nextRecordingId++;
    m_uploadAllocator.BeginRecording(m_currentRecordingId);
    m_descriptorRecycler.Collect(RHICompletionPoint{ m_fence->GetCompletedValue() });
    if (!m_descriptorRecycler.BeginRecording(m_currentRecordingId, outError))
    {
        m_uploadAllocator.AbortRecording(m_currentRecordingId);
        return false;
    }

    // RTV/DSV 힙은 펜스 대기와 무관하게 되감아도 된다 — 이 디스크립터는
    // 기록 시점에 소비되므로 GPU가 지난 프레임을 실행 중이어도 상관없다.
    // 그래도 같은 자리에서 부르는 것은 '프레임 시작에 되감는다'는 규칙을
    // 한 곳에 모아 두기 위해서다.
    m_rtvViewHeap.BeginFrame();
    m_dsvViewHeap.BeginFrame();
    m_clearViewHeap.BeginFrame();

    return true;
}

bool DX12DeviceResources::FlushCommandList(std::string& outError)
{
    if (!m_commandList)
    {
        outError = "중간 제출할 DX12 command list가 없다";
        return false;
    }
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr)) { outError = "중간 제출 Close 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    const uint64_t fenceValue = m_nextFenceValue++;
    m_uploadAllocator.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_descriptorRecycler.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadSubmitted(m_currentRecordingId, RHICompletionPoint{ fenceValue });
    // 이 뒤 현재 recording이 Abort되더라도 같은 command allocator는 앞선
    // 중간 제출이 끝나기 전 Reset할 수 없다.
    m_frameFenceValues[m_frameIndex] = fenceValue;

    const uint32_t frameSlot = m_frameIndex;
    ComPtr<ID3D12GraphicsCommandList> submittedList = m_commandList;
    m_retiredCommandLists[frameSlot].push_back(submittedList);
    m_commandList.Reset();
    m_immediateEncoder.reset();

    RHISubmissionTicket ticket;
    if (!GetRHISubmissionThread().Enqueue(this, "DX12 immediate flush",
        [owner = this, queue = m_queue, fence = m_fence, submittedList, fenceValue](
            std::string& error)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                error = "DX12 ExecuteCommandLists가 RHI thread 밖에서 호출됐다";
                return false;
            }
            ID3D12CommandList* lists[] = { submittedList.Get() };
            queue->ExecuteCommandLists(1, lists);
            const HRESULT signalResult = queue->Signal(fence.Get(), fenceValue);
            if (FAILED(signalResult))
            {
                error = "DX12 중간 제출 Signal 실패 " + HrToString(signalResult);
                if (DXGI_ERROR_DEVICE_REMOVED == signalResult ||
                    DXGI_ERROR_DEVICE_RESET == signalResult ||
                    DXGI_ERROR_DEVICE_HUNG == signalResult ||
                    DXGI_ERROR_DRIVER_INTERNAL_ERROR == signalResult)
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(owner, error);
                return false;
            }
            return true;
        }, ticket, outError))
    {
        return false;
    }
    m_frameSubmissionTickets[frameSlot] = ticket;

    // 같은 allocator는 되감지 않고 새 command-list 객체만 연다. 이전 객체는
    // RHI thread가 아직 Execute하지 않았을 수 있어 여기서 Reset할 수 없다.
    hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_allocators[m_frameIndex].Get(), nullptr, IID_PPV_ARGS(&m_commandList));
    if (FAILED(hr)) { outError = "중간 제출 후 command list 생성 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }
    ResetImmediateEncoder();

    m_currentRecordingId = m_nextRecordingId++;
    m_uploadAllocator.BeginRecording(m_currentRecordingId);
    if (!m_descriptorRecycler.BeginRecording(m_currentRecordingId, outError))
    {
        m_uploadAllocator.AbortRecording(m_currentRecordingId);
        return false;
    }

    return true;
}

bool DX12DeviceResources::PrepareParallelSubmission(
    RHICompletionPoint& outCompletion, std::string& outError)
{
    if (!m_queue || !m_fence)
    {
        outError = "DX12 병렬 제출 준비 coordinator가 초기화되지 않았다";
        return false;
    }

    const uint64_t fenceValue = m_nextFenceValue++;
    m_uploadAllocator.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_descriptorRecycler.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadSubmitted(m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_frameFenceValues[m_frameIndex] = fenceValue;
    outCompletion = RHICompletionPoint{ fenceValue };
    m_currentRecordingId = m_nextRecordingId++;
    m_uploadAllocator.BeginRecording(m_currentRecordingId);
    if (!m_descriptorRecycler.BeginRecording(m_currentRecordingId, outError))
    {
        m_uploadAllocator.AbortRecording(m_currentRecordingId);
        return false;
    }
    ResetImmediateEncoder();
    return true;
}

bool DX12DeviceResources::SubmitCommandLists(
    std::span<ID3D12CommandList* const> lists, RHICompletionPoint completion,
    std::string& outError)
{
    if (!m_queue || !m_fence || !completion.IsValid())
    {
        outError = "DX12 제출 coordinator/completion이 초기화되지 않았다";
        return false;
    }
    if (!GetRHISubmissionThread().IsCurrentThread())
    {
        outError = "DX12 병렬 ExecuteCommandLists가 RHI thread 밖에서 호출됐다";
        return false;
    }
    if (!lists.empty())
        m_queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
    const HRESULT hr = m_queue->Signal(m_fence.Get(), completion.value);
    if (FAILED(hr))
    {
        outError = "DX12 외부 리스트 제출 Signal 실패 " + HrToString(hr);
        if (DXGI_ERROR_DEVICE_REMOVED == hr || DXGI_ERROR_DEVICE_RESET == hr ||
            DXGI_ERROR_DEVICE_HUNG == hr ||
            DXGI_ERROR_DRIVER_INTERNAL_ERROR == hr)
            GetRHISubmissionThread().MarkUnrecoverableDeviceError(this, outError);
        return false;
    }
    return true;
}

void DX12DeviceResources::AbortFrame()
{
    // 닫기만 한다. 실패해도 할 수 있는 일이 없고, 여기까지 온 시점에
    // 이미 상위가 원래 사유를 들고 있다 — 덮지 않는다.
    if (m_commandList) m_commandList->Close();
    m_uploadAllocator.AbortRecording(m_currentRecordingId);
    m_descriptorRecycler.AbortRecording(m_currentRecordingId);
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadAborted(m_currentRecordingId);
    m_currentRecordingId = 0;
}

bool DX12DeviceResources::EndFrame(std::string& outError)
{
    if (!m_commandList)
    {
        outError = "종료할 DX12 command list가 없다";
        return false;
    }
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr)) { outError = "커맨드 리스트 Close 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    const uint64_t fenceValue = m_nextFenceValue++;
    m_uploadAllocator.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_descriptorRecycler.OnSubmitted(
        m_currentRecordingId, RHICompletionPoint{ fenceValue });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadSubmitted(m_currentRecordingId, RHICompletionPoint{ fenceValue });
    m_currentRecordingId = 0;

    const uint32_t frameSlot = m_frameIndex;
    m_frameFenceValues[frameSlot] = fenceValue;
    ComPtr<ID3D12GraphicsCommandList> submittedList = m_commandList;
    m_retiredCommandLists[frameSlot].push_back(submittedList);
    m_commandList.Reset();
    m_immediateEncoder.reset();

    RHISubmissionTicket ticket;
    if (!GetRHISubmissionThread().Enqueue(this, "DX12 EndFrame",
        [owner = this, queue = m_queue, fence = m_fence, submittedList, fenceValue](
            std::string& error)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                error = "DX12 EndFrame queue 호출이 RHI thread 밖에서 실행됐다";
                return false;
            }
            ID3D12CommandList* lists[] = { submittedList.Get() };
            queue->ExecuteCommandLists(1, lists);
            const HRESULT signalResult = queue->Signal(fence.Get(), fenceValue);
            if (FAILED(signalResult))
            {
                error = "DX12 EndFrame Signal 실패 " + HrToString(signalResult);
                if (DXGI_ERROR_DEVICE_REMOVED == signalResult ||
                    DXGI_ERROR_DEVICE_RESET == signalResult ||
                    DXGI_ERROR_DEVICE_HUNG == signalResult ||
                    DXGI_ERROR_DRIVER_INTERNAL_ERROR == signalResult)
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(owner, error);
                return false;
            }
            return true;
        }, ticket, outError))
    {
        return false;
    }
    m_frameSubmissionTickets[frameSlot] = ticket;
    m_frameIndex = (m_frameIndex + 1) % kFrameCount;
    return true;
}

void DX12DeviceResources::WaitForGpu()
{
    std::string error;
    if (!DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, error) &&
        !error.empty())
    {
        OutputDebugStringA(("[DX12] offline GPU drain 실패: " + error + "\n").c_str());
    }
}

bool DX12DeviceResources::DrainForLifecycle(RHILifecycleCommand command,
    std::string& outError)
{
    if (!m_queue || !m_fence || !m_submissionClient) return true;

    RHISubmissionThread& submission = GetRHISubmissionThread();
    const RHISubmissionOwnerStats before = submission.GetOwnerStats(this);
    if (before.IsIdle() &&
        ((before.lastCommand == command &&
            (RHILifecycleCommand::BackendShutdown == command ||
             RHILifecycleCommand::SwapChainResize == command)) ||
         (before.faulted && RHILifecycleCommand::UnrecoverableDeviceError ==
            before.lastCommand)))
    {
        m_lastLifecycleResult = {};
        m_lastLifecycleResult.command = before.lastCommand;
        m_lastLifecycleResult.previousGeneration = before.generation - 1u;
        m_lastLifecycleResult.generation = before.generation;
        m_lastLifecycleResult.drained = true;
        return true;
    }
    if (RHILifecycleCommand::UnrecoverableDeviceError == command || before.faulted)
    {
        return submission.AbandonForDeviceError(this,
            m_lastLifecycleResult, outError);
    }

    const uint64_t fenceValue = m_nextFenceValue++;
    if (!submission.ExecuteLifecycleDrain(this, command,
        [owner = this, queue = m_queue, fence = m_fence,
            fenceEvent = m_fenceEvent, fenceValue](std::string& taskError)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                taskError = "DX12 lifecycle drain이 RHI thread 밖에서 호출됐다";
                return false;
            }
            const HRESULT result = queue->Signal(fence.Get(), fenceValue);
            if (FAILED(result))
            {
                taskError = "DX12 lifecycle Signal 실패 " + HrToString(result);
                if (DXGI_ERROR_DEVICE_REMOVED == result ||
                    DXGI_ERROR_DEVICE_RESET == result ||
                    DXGI_ERROR_DEVICE_HUNG == result ||
                    DXGI_ERROR_DRIVER_INTERNAL_ERROR == result)
                {
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(
                        owner, taskError);
                }
                return false;
            }
            if (fence->GetCompletedValue() < fenceValue)
            {
                const HRESULT waitResult = fence->SetEventOnCompletion(
                    fenceValue, fenceEvent);
                if (FAILED(waitResult))
                {
                    taskError = "DX12 lifecycle fence wait 설정 실패 " +
                        HrToString(waitResult);
                    return false;
                }
                WaitForSingleObject(fenceEvent, INFINITE);
            }
            return true;
        }, m_lastLifecycleResult, outError)) return false;

    const uint64_t completed = m_fence->GetCompletedValue();
    m_descriptorRecycler.Collect(RHICompletionPoint{ completed });
    for (IRHIUploadTransactionListener* listener : m_uploadTransactionListeners)
        listener->OnUploadCompleted(completed);
    std::printf("[RHI lifecycle][DX12] %s generation %llu->%llu"
        " pending task/batch/retirement %u/%u/%u\n",
        ToString(command),
        static_cast<unsigned long long>(m_lastLifecycleResult.previousGeneration),
        static_cast<unsigned long long>(m_lastLifecycleResult.generation),
        m_lastLifecycleResult.pendingTasks,
        m_lastLifecycleResult.pendingBatches,
        m_lastLifecycleResult.pendingRetirements);
    return m_lastLifecycleResult.IsClean();
}

uint32_t DX12DeviceResources::DrainDebugMessages(std::string& outMessages)
{
    outMessages.clear();
#if defined(_DEBUG)
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(m_device.As(&infoQueue))) return 0;

    const uint64_t count = infoQueue->GetNumStoredMessages();
    uint32_t problems = 0;

    for (uint64_t i = 0; i < count; ++i)
    {
        SIZE_T length = 0;
        infoQueue->GetMessage(i, nullptr, &length);
        std::string storage(length, '\0');
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(infoQueue->GetMessage(i, message, &length))) continue;

        const char* severity = "INFO";
        bool isProblem = false;
        switch (message->Severity)
        {
        case D3D12_MESSAGE_SEVERITY_CORRUPTION: severity = "CORRUPTION"; isProblem = true; break;
        case D3D12_MESSAGE_SEVERITY_ERROR:      severity = "ERROR";      isProblem = true; break;
        case D3D12_MESSAGE_SEVERITY_WARNING:    severity = "WARNING";    isProblem = true; break;
        case D3D12_MESSAGE_SEVERITY_INFO:       severity = "INFO";       break;
        case D3D12_MESSAGE_SEVERITY_MESSAGE:    severity = "MESSAGE";    break;
        }

        if (isProblem) ++problems;

        outMessages += "[";
        outMessages += severity;
        outMessages += "] ";
        outMessages += message->pDescription;
        outMessages += "\n";
    }

    infoQueue->ClearStoredMessages();
    return problems;
#else
    return 0;
#endif
}

// ── GPU 진단 (DX11 DeviceResources에서 이관, 2026-08-10) ──

namespace
{
    // 진단 대상 디바이스. 명시 등록이고, 지금 등록자는 상시 러너 하나다
    // (IRHIDeviceResources.h의 §진단 대상 디바이스 참고).
    IRHIDeviceResources* g_diagnosticsResources = nullptr;

    // 디버그 레이어의 라이브 객체 메시지에서 타입 이름을 뽑는다.
    // 메시지 예: "Live ID3D12Resource at 0x000001F2..., Refcount: 1, IntRef: 0"
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
}

void SetDiagnosticsDeviceResources(IRHIDeviceResources* resources)
{
    g_diagnosticsResources = resources;
}

IRHIDeviceResources* GetDiagnosticsDeviceResources()
{
    return g_diagnosticsResources;
}

RHIUploadMemoryBudget DX12DeviceResources::QueryUploadMemoryBudget() const
{
    RHIUploadMemoryBudget result{};
    if (!m_adapter || !m_device) return result;

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    if (FAILED(m_adapter.As(&adapter3))) return result;

    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{};
    architecture.NodeIndex = 0;
    const bool uma = SUCCEEDED(m_device->CheckFeatureSupport(
        D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture))) &&
        architecture.UMA;

    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    const DXGI_MEMORY_SEGMENT_GROUP preferred = uma
        ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
    HRESULT queried = adapter3->QueryVideoMemoryInfo(0, preferred, &memory);
    if (FAILED(queried) || 0 == memory.Budget)
    {
        queried = adapter3->QueryVideoMemoryInfo(0,
            DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory);
    }
    if (FAILED(queried) || 0 == memory.Budget) return result;

    result.usageBytes = memory.CurrentUsage;
    result.budgetBytes = memory.Budget;
    return result;
}

void DX12DeviceResources::RefreshUploadBudget()
{
    const RHIUploadMemoryBudget budget = QueryUploadMemoryBudget();
    const uint64_t currentBytes = m_uploadAllocator.GetStats().segmentBytes;
    m_uploadMemoryPressure = DX12UploadMemoryPressure(budget,
        m_uploadMemoryPressure);
    m_uploadAllocator.UpdateBudget(DX12UploadSoftBudget(budget, currentBytes),
        m_uploadMemoryPressure);
}

void DX12DeviceResources::RefreshPersistentMemoryBudget()
{
    RHIPersistentHeapBudget budget{};
    if (m_adapter)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
        if (SUCCEEDED(m_adapter.As(&adapter3)))
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,
                DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)) && 0 != memory.Budget)
            {
                budget.usageBytes = memory.CurrentUsage;
                budget.budgetBytes = memory.Budget;
            }
        }
    }
    m_persistentMemoryBudget.UpdateBudget(
        kRHIDeviceLocalMemoryBudgetDomain, budget);
}

RHIVideoMemoryInfo DX12DeviceResources::QueryVideoMemory() const
{
    RHIVideoMemoryInfo info{};
    if (!m_adapter)
    {
        return info;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
    if (FAILED(m_adapter.As(&adapter3)))
    {
        return info;
    }

    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
    {
        return info;
    }

    constexpr uint64_t megabyte = 1024ull * 1024ull;
    info.usedMB = memory.CurrentUsage / megabyte;
    info.budgetMB = memory.Budget / megabyte;
    return info;
}

RHIGpuObjectCensus DX12DeviceResources::CaptureLiveObjectCensus(bool allowDeviceEnumeration)
{
    RHIGpuObjectCensus census{};

    // VRAM은 디버그 레이어 유무와 무관하게 수집할 수 있다.
    const RHIVideoMemoryInfo memory = QueryVideoMemory();
    census.vramUsedMB = memory.usedMB;
    census.vramBudgetMB = memory.budgetMB;

#if defined(_DEBUG)
    // 호출자가 "이후 렌더가 없다"고 약속한 경우에만 순회한다(인터페이스 주석 참고).
    if (!allowDeviceEnumeration || !m_device)
    {
        return census;
    }

    Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(m_device.As(&debugDevice)) || FAILED(m_device.As(&infoQueue)))
    {
        return census;  // 디버그 레이어가 꺼져 있다 — VRAM만 유효
    }

    // ReportLiveDeviceObjects는 살아있는 객체 하나당 메시지 한 건을 InfoQueue에
    // 넣는다. 직전 메시지가 섞이지 않도록 비우고 호출한 뒤 결과만 읽는다.
    //
    // ★ 여기서 DrainDebugMessages와 같은 큐를 비운다. 종료 지점에서만 도는
    //   경로라 서로 잡아먹을 일이 없지만, 실행 중에 부르면 그쪽이 셀 메시지를
    //   삼킨다 — allowDeviceEnumeration이 막는 것이 이것이기도 하다.
    infoQueue->ClearStoredMessages();
    debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL);

    const uint64_t messageCount = infoQueue->GetNumStoredMessages();
    for (uint64_t i = 0; i < messageCount; ++i)
    {
        SIZE_T length = 0;
        if (FAILED(infoQueue->GetMessage(i, nullptr, &length)) || 0 == length)
        {
            continue;
        }

        std::string storage(length, '\0');
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (FAILED(infoQueue->GetMessage(i, message, &length)) || nullptr == message->pDescription)
        {
            continue;
        }

        // DescriptionByteLength는 널 종료 문자를 포함한다.
        size_t descriptionLength = message->DescriptionByteLength;
        if (descriptionLength > 0 && '\0' == message->pDescription[descriptionLength - 1])
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

    infoQueue->ClearStoredMessages();
    census.available = true;
#else
    (void)allowDeviceEnumeration;
#endif

    return census;
}

void DX12DeviceResources::ReportLiveObjectsToDebugOutput()
{
#if defined(_DEBUG)
    if (m_device)
    {
        Microsoft::WRL::ComPtr<ID3D12DebugDevice> debugDevice;
        if (SUCCEEDED(m_device.As(&debugDevice)))
        {
            debugDevice->ReportLiveDeviceObjects(D3D12_RLDO_IGNORE_INTERNAL);
        }
    }

    // DXGI 디버그 계층은 프로세스 범위다 — 디바이스가 이미 해체됐어도 남은
    // 객체를 훑는다. DX11 DeviceResources는 이것을 생성 시점에 한 번 얻어
    // 멤버로 들고 있었는데, 쓰는 곳이 이 함수 하나뿐이라 여기서 연다.
    Microsoft::WRL::ComPtr<IDXGIDebug> dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
    {
        dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_IGNORE_INTERNAL);
    }
#endif
}

void DX12DeviceResources::AppendDeviceRemovedReport(HRESULT operationResult,
    std::string& outError) const
{
    if (!m_device) return;

    HRESULT reason = m_device->GetDeviceRemovedReason();
    const bool operationIndicatesRemoval =
        DXGI_ERROR_DEVICE_REMOVED == operationResult ||
        DXGI_ERROR_DEVICE_RESET == operationResult ||
        DXGI_ERROR_DEVICE_HUNG == operationResult ||
        DXGI_ERROR_DRIVER_INTERNAL_ERROR == operationResult;

    // GetDeviceRemovedReason은 살아 있는 장치에서 S_OK다. 일반 API 실패마다 DRED
    // 빈 보고서를 붙이면 최초 오류가 묻히므로 장치 제거일 때만 확장한다.
    if (SUCCEEDED(reason) && !operationIndicatesRemoval) return;
    if (SUCCEEDED(reason)) reason = operationResult;

    std::ostringstream report;
    report << "\n[DRED] deviceRemovedReason=" << HrToString(reason);

    Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    if (FAILED(m_device.As(&dred)))
    {
        report << " (ID3D12DeviceRemovedExtendedData1 unavailable)";
        outError += report.str();
        return;
    }

    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
    {
        uint32_t nodeCount = 0;
        for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode;
            nullptr != node && nodeCount < 64; node = node->pNext, ++nodeCount)
        {
            const UINT completed = (nullptr != node->pLastBreadcrumbValue)
                ? *node->pLastBreadcrumbValue : 0;
            report << "\n[DRED] queue="
                << DebugName(node->pCommandQueueDebugNameA, node->pCommandQueueDebugNameW)
                << " commandList="
                << DebugName(node->pCommandListDebugNameA, node->pCommandListDebugNameW)
                << " completed=" << completed << '/' << node->BreadcrumbCount;

            if (nullptr != node->pCommandHistory && completed < node->BreadcrumbCount)
            {
                report << " nextOp=" << static_cast<unsigned>(node->pCommandHistory[completed]);
            }

            // 마지막으로 통과한 사용자 context를 붙인다. 원시 PIX 이벤트를 직접
            // 기록하지 않아도 SetMarker/BeginEvent를 정상 API로 넣은 경우 위치가 나온다.
            const D3D12_DRED_BREADCRUMB_CONTEXT* lastContext = nullptr;
            for (UINT i = 0; i < node->BreadcrumbContextsCount; ++i)
            {
                const auto& context = node->pBreadcrumbContexts[i];
                if (context.BreadcrumbIndex <= completed &&
                    (nullptr == lastContext || context.BreadcrumbIndex >= lastContext->BreadcrumbIndex))
                {
                    lastContext = &context;
                }
            }
            if (nullptr != lastContext)
            {
                report << " context=" << WideToUtf8(lastContext->pContextString);
            }
        }
        if (nodeCount == 64) report << "\n[DRED] breadcrumb list truncated at 64 nodes";
    }

    D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)) &&
        0 != pageFault.PageFaultVA)
    {
        report << "\n[DRED] pageFaultVA=0x" << std::hex << pageFault.PageFaultVA << std::dec;

        const auto appendAllocations = [&report](const char* label,
            const D3D12_DRED_ALLOCATION_NODE1* head)
        {
            uint32_t count = 0;
            for (const D3D12_DRED_ALLOCATION_NODE1* node = head;
                nullptr != node && count < 32; node = node->pNext, ++count)
            {
                report << "\n[DRED] " << label << " name="
                    << DebugName(node->ObjectNameA, node->ObjectNameW)
                    << " type=" << static_cast<unsigned>(node->AllocationType);
            }
            if (count == 32) report << "\n[DRED] " << label << " list truncated at 32 nodes";
        };

        appendAllocations("existing", pageFault.pHeadExistingAllocationNode);
        appendAllocations("recentlyFreed", pageFault.pHeadRecentFreedAllocationNode);
    }

    outError += report.str();
}

bool DX12DeviceResources::AttachSwapChain(void* windowHandle, uint32_t width, uint32_t height,
    std::string& outError)
{
    // 인터페이스(IRHIDeviceResources)가 플랫폼 핸들을 void*로 받는다 —
    // 그 헤더가 Windows.h를 끌어오지 않기 위해서다. Win32에서는 HWND다.
    HWND hwnd = static_cast<HWND>(windowHandle);
    if (nullptr == hwnd)
    {
        outError = "창 핸들이 없다";
        return false;
    }

    if (nullptr == m_device.Get() || nullptr == m_queue.Get())
    {
        outError = "스왑체인 부착 전에 Initialize가 필요하다";
        return false;
    }
    if (m_swapChain)
    {
        outError = "스왑체인이 이미 있다";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kFrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    ComPtr<IDXGISwapChain1> swapChain1;
    HRESULT hr = m_factory->CreateSwapChainForHwnd(m_queue.Get(), hwnd, &desc,
        nullptr, nullptr, &swapChain1);
    if (FAILED(hr))
    {
        outError = "CreateSwapChainForHwnd 실패 " + HrToString(hr);
        return false;
    }
    if (FAILED(swapChain1.As(&m_swapChain)))
    {
        outError = "IDXGISwapChain3 질의 실패";
        return false;
    }
    // 전체 화면 전환은 셸(창 계층)이 관리한다 — DXGI의 암묵 Alt+Enter를 끈다.
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = kFrameCount;
    if (FAILED(m_device->CreateDescriptorHeap(&heapDesc,
        IID_PPV_ARGS(&m_backBufferRtvHeap))))
    {
        outError = "백버퍼 RTV 힙 생성 실패";
        return false;
    }
    m_backBufferRtvSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]))))
        {
            outError = "백버퍼 " + std::to_string(i) + " 획득 실패";
            return false;
        }
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr,
            GetBackBufferRtv(i));
    }
    return true;
}

bool DX12DeviceResources::ResizeSwapChain(uint32_t width, uint32_t height,
    std::string& outError)
{
    if (!m_swapChain)
    {
        outError = "스왑체인이 없다";
        return false;
    }

    // ResizeBuffers는 백버퍼 참조가 전부 풀린 상태를 요구한다 — GPU 완주
    // 대기 후 RTV만 남기고 놓는다.
    if (!DrainForLifecycle(RHILifecycleCommand::SwapChainResize, outError))
        return false;
    for (auto& backBuffer : m_backBuffers) backBuffer.Reset();

    const HRESULT hr = m_swapChain->ResizeBuffers(kFrameCount, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr))
    {
        outError = "ResizeBuffers 실패 " + HrToString(hr);
        return false;
    }

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]))))
        {
            outError = "리사이즈 후 백버퍼 획득 실패";
            return false;
        }
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr,
            GetBackBufferRtv(i));
    }
    return true;
}

bool DX12DeviceResources::Present(std::string& outError)
{
    if (!m_swapChain)
    {
        outError = "스왑체인이 없다";
        return false;
    }
    return GetRHISubmissionThread().ExecuteAndWait(this, "DX12 Present",
        [owner = this, swapChain = m_swapChain](std::string& error)
        {
            if (!GetRHISubmissionThread().IsCurrentThread())
            {
                error = "DX12 Present가 RHI thread 밖에서 호출됐다";
                return false;
            }
            const HRESULT hr = swapChain->Present(0, 0);
            if (FAILED(hr))
            {
                error = "DX12 Present 실패 " + HrToString(hr);
                if (DXGI_ERROR_DEVICE_REMOVED == hr ||
                    DXGI_ERROR_DEVICE_RESET == hr ||
                    DXGI_ERROR_DEVICE_HUNG == hr ||
                    DXGI_ERROR_DRIVER_INTERNAL_ERROR == hr)
                    GetRHISubmissionThread().MarkUnrecoverableDeviceError(owner, error);
                return false;
            }
            return true;
        }, outError);
}

uint32_t DX12DeviceResources::GetBackBufferIndex() const
{
    return m_swapChain ? m_swapChain->GetCurrentBackBufferIndex() : 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12DeviceResources::GetBackBufferRtv(uint32_t index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_backBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_backBufferRtvSize;
    return handle;
}

// ── 바인딩 (PHASE 3-1 재정의, R2) ──
//
// 패스마다 흩어져 있던 "링에서 자르고 → 뷰를 만들고 → 테이블을 건다"를 여기
// 한 곳으로 모은다. 세 가지 실수가 여기서만 일어날 수 있게 되는 것이 요점이다:
// 링 오버런 검사, 리소스 널 검사, ViewDimension·포맷 지정.

namespace
{
    /// 깊이 포맷을 셰이더가 읽을 수 있는 색 포맷으로 바꾼다.
    ///
    /// 깊이는 D32_FLOAT 같은 포맷으로 만들어지는데 SRV는 그 포맷을 받지 않는다.
    /// 예전에는 읽는 패스마다 이 대응을 손으로 적었고(대개 D32만 다뤘다),
    /// 빠뜨리면 뷰 생성이 조용히 실패했다.
    // ★ 깊이 → 셰이더 읽기 포맷 대응표를 DX12Format.h 로 옮겼다(V1).
    //   표가 둘이면 언젠가 갈리고, Vulkan 백엔드에서 이 함수가 항등에 가까워지는 것이
    //   그 자리의 뜻이다.
    DXGI_FORMAT DepthToColorFormat(DXGI_FORMAT format) { return DepthToShaderReadDXGI(format); }

    /// UNKNOWN이면 리소스가 아는 포맷을 쓴다. SRV 설명을 명시하는 경로에서는
    /// UNKNOWN이 그대로 유효한 값이 아니라서, 여기서 반드시 채워야 한다.
    DXGI_FORMAT ResolveSrvFormat(const RHIBindingDesc& desc, ID3D12Resource* resource)
    {
        // 널 디스크립터는 물어볼 리소스가 없다 — 적어 준 포맷을 그대로 쓴다.
        if (nullptr == resource) return ToDXGI(desc.format);

        const DXGI_FORMAT resourceFormat = resource->GetDesc().Format;

        if (desc.depthAsColor)  return DepthToColorFormat(resourceFormat);
        if (RHIFormat::Unknown == desc.format) return resourceFormat;
        return ToDXGI(desc.format);
    }
}
D3D12_RESOURCE_STATES DX12DeviceResources::ToD3D12(RHIResourceState state)
{
    switch (state)
    {
    case RHIResourceState::RenderTarget:    return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RHIResourceState::DepthWrite:      return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RHIResourceState::DepthRead:       return D3D12_RESOURCE_STATE_DEPTH_READ;
    case RHIResourceState::ShaderResource:  return D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    case RHIResourceState::PixelShaderResource:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case RHIResourceState::DepthReadShaderResource:
        return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
    case RHIResourceState::UnorderedAccess: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case RHIResourceState::CopySource:      return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RHIResourceState::CopyDest:        return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHIResourceState::Common:
    default:                                return D3D12_RESOURCE_STATE_COMMON;
    }
}

void DX12DeviceResources::TransitionResources(std::span<const RHITransition> transitions)
{
    if (nullptr == m_commandList.Get() || transitions.empty()) return;

    // 한 번에 모아 넣는다 — 배리어를 흩뿌리면 GPU가 그때마다 파이프라인을 비운다.
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(transitions.size());

    for (const RHITransition& transition : transitions)
    {
        ID3D12Resource* const resource = Resolve(transition.texture);
        if (nullptr == resource) continue;

        const D3D12_RESOURCE_STATES before = ToD3D12(transition.before);
        const D3D12_RESOURCE_STATES after = ToD3D12(transition.after);
        if (before == after) continue;   // 같은 상태로의 전이는 검증 레이어가 거절한다

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barriers.push_back(barrier);
    }

    if (barriers.empty()) return;
    m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void DX12DeviceResources::TransitionBuffers(
    std::span<const RHIBufferTransition> transitions)
{
    if (nullptr == m_commandList.Get() || transitions.empty()) return;

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(transitions.size());
    for (const RHIBufferTransition& transition : transitions)
    {
        ID3D12Resource* const resource = Resolve(transition.buffer);
        if (nullptr == resource) continue;

        const D3D12_RESOURCE_STATES before = ToD3D12(transition.before);
        const D3D12_RESOURCE_STATES after = ToD3D12(transition.after);
        if (before == after) continue;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barriers.push_back(barrier);
    }

    if (!barriers.empty())
        m_commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

ID3D12Resource* DX12DeviceResources::ResolveBinding(const RHIBindingDesc& desc) const
{
    // ★ 어느 칸을 보는지는 dim이 정한다(V2-b). 버퍼 UAV만 bufferResource를
    //   쓰고 나머지는 텍스처 칸이다 — 두 칸을 나눠 둔 이유가 이 분기다.
    if (RHIBindingDesc::Dim::Buffer == desc.dim) return Resolve(desc.bufferResource);
    return Resolve(desc.resource);
}

RHIBindingTable DX12DeviceResources::CreateBindings(std::span<const RHIBindingDesc> descs)
{
    if (descs.empty()) return {};

    // 리소스가 하나라도 비면 테이블을 만들지 않는다.
    //
    // ★ 예전에는 이 검사가 없어서, 널 리소스에 뷰를 만들려다 실패한 자리가
    //   테이블 안에 빈 칸으로 남았다. 검증 레이어가 그것을 잡지만 메시지가
    //   드로우 시점에 나와서 어느 패스인지 짚기 어려웠다. 여기서 미리 끊으면
    //   호출부가 invalid 하나만 보고 돌아설 수 있다.
    //   비어도 되는 자리는 OrNull()로 표시해 예외로 둔다 — 거기에는 널
    //   디스크립터가 깔린다(안 거는 것과 다르다).
    for (const RHIBindingDesc& desc : descs)
    {
        if (nullptr == ResolveBinding(desc) && !desc.allowNull) return {};
    }

    const auto range = m_descriptorRecycler.Allocate(static_cast<uint32_t>(descs.size()));
    if (!range.IsValid()) return {};

    ID3D12Device* device = m_device.Get();
    for (uint32_t i = 0; i < descs.size(); ++i)
    {
        const RHIBindingDesc& desc = descs[i];
        const D3D12_CPU_DESCRIPTOR_HANDLE handle = range.CpuAt(i);
        ID3D12Resource* const resource = ResolveBinding(desc);

        if (RHIBindingDesc::Kind::UnorderedAccess == desc.kind)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = ToDXGI(desc.format);
            switch (desc.dim)
            {
            case RHIBindingDesc::Dim::Texture3D:
                uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
                uav.Texture3D.WSize = desc.sliceCount;
                break;
            case RHIBindingDesc::Dim::Buffer:
                uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uav.Buffer.FirstElement = desc.firstElement;
                uav.Buffer.NumElements = desc.numElements;
                uav.Buffer.StructureByteStride = desc.structureByteStride;
                break;
            default:
                uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uav.Texture2D.MipSlice = desc.mostDetailedMip;
                break;
            }
            device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
            continue;
        }

        // Default는 리소스가 스스로 아는 대로 본다 — nullptr 설명과 같다.
        if (RHIBindingDesc::Dim::Default == desc.dim)
        {
            device->CreateShaderResourceView(resource, nullptr, handle);
            continue;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = ResolveSrvFormat(desc, resource);
        switch (desc.dim)
        {
        case RHIBindingDesc::Dim::Texture2DArray:
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            srv.Texture2DArray.MostDetailedMip = desc.mostDetailedMip;
            srv.Texture2DArray.MipLevels = desc.mipLevels;
            srv.Texture2DArray.FirstArraySlice = desc.firstSlice;
            srv.Texture2DArray.ArraySize = desc.sliceCount;
            break;
        case RHIBindingDesc::Dim::TextureCube:
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srv.TextureCube.MostDetailedMip = desc.mostDetailedMip;
            srv.TextureCube.MipLevels = desc.mipLevels;
            break;
        case RHIBindingDesc::Dim::Texture3D:
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv.Texture3D.MostDetailedMip = desc.mostDetailedMip;
            srv.Texture3D.MipLevels = desc.mipLevels;
            break;
        default:
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MostDetailedMip = desc.mostDetailedMip;
            srv.Texture2D.MipLevels = desc.mipLevels;
            break;
        }
        device->CreateShaderResourceView(resource, &srv, handle);
    }

    RHIBindingTable table{};
    table.backend = range.gpu.ptr;
    table.count = static_cast<uint32_t>(descs.size());
    table.version = range.version;
    return table;
}

void DX12DeviceResources::BindDescriptorHeaps(ID3D12GraphicsCommandList* commandList,
    bool withSamplers)
{
    if (nullptr == commandList) return;

    if (withSamplers)
    {
        ID3D12DescriptorHeap* heaps[] = {
            m_descriptorRecycler.GetHeap(), m_samplerHeap.GetHeap() };
        commandList->SetDescriptorHeaps(2, heaps);
        return;
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorRecycler.GetHeap() };
    commandList->SetDescriptorHeaps(1, heaps);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12DeviceResources::CreateClearDescriptor(
    const RHIBindingDesc& desc)
{
    ID3D12Resource* const resource = ResolveBinding(desc);
    if (nullptr == m_device || nullptr == resource) return {};

    const uint32_t index = m_clearViewHeap.Allocate(1);
    if (DX12TargetViewHeap::kInvalidIndex == index) return {};

    const D3D12_CPU_DESCRIPTOR_HANDLE handle = m_clearViewHeap.CpuAt(index);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = ToDXGI(desc.format);
    switch (desc.dim)
    {
    case RHIBindingDesc::Dim::Texture3D:
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        uav.Texture3D.WSize = desc.sliceCount;
        break;
    case RHIBindingDesc::Dim::Buffer:
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.FirstElement = desc.firstElement;
        uav.Buffer.NumElements = desc.numElements;
        uav.Buffer.StructureByteStride = desc.structureByteStride;
        break;
    default:
        uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uav.Texture2D.MipSlice = desc.mostDetailedMip;
        break;
    }

    m_device->CreateUnorderedAccessView(resource, nullptr, &uav, handle);
    return handle;
}

// ── 렌더 타깃 (R2b) ──

RHIRenderTargetBinding DX12DeviceResources::CreateRenderTargets(
    std::span<const RHITextureHandle> colors, const RHIDepthTargetDesc* depth)
{
    if (colors.size() > 8) return {};
    RHIColorTargetDesc descriptions[8]{};
    for (uint32_t i = 0; i < colors.size(); ++i)
        descriptions[i] = RHIColorTargetDesc::Texture(colors[i]);
    return CreateRenderTargets(
        std::span<const RHIColorTargetDesc>{ descriptions, colors.size() }, depth);
}

RHIRenderTargetBinding DX12DeviceResources::CreateRenderTargets(
    std::span<const RHIColorTargetDesc> colors, const RHIDepthTargetDesc* depth)
{
    const bool wantsDepth = (nullptr != depth && depth->resource.IsValid());
    if (colors.empty() && !wantsDepth) return {};

    // CreateBindings와 같은 계약 — 하나라도 널이면 통째로 거절한다.
    // 부분적으로 만들어 주면 호출부가 '몇 개가 만들어졌는가'를 다시 세야 하고,
    // 그 셈이 틀리면 OMSetRenderTargets가 초기화되지 않은 칸을 묶는다.
    for (const RHIColorTargetDesc& color : colors)
    {
        if (nullptr == Resolve(color.resource)) return {};
    }

    // 깊이는 포맷을 반드시 받는다. UNKNOWN으로 DSV를 만들면 리소스가
    // TYPELESS일 때 조용히 실패하고, 화면에는 '깊이가 안 걸린다'로만 나온다.
    if (wantsDepth && RHIFormat::Unknown == depth->format) return {};

    RHIRenderTargetBinding binding{};
    // 5b. 두 인덱스를 모아 마지막에 한 값으로 접는다 — 계약이 인덱스를
    // 말하지 않으므로 여기가 그 뜻을 주는 유일한 자리다.
    uint32_t rtvIndex = DX12TargetViewHeap::kInvalidIndex;
    uint32_t dsvIndex = DX12TargetViewHeap::kInvalidIndex;
    ID3D12Device* device = m_device.Get();

    if (!colors.empty())
    {
        const uint32_t index = m_rtvViewHeap.Allocate(static_cast<uint32_t>(colors.size()));
        if (DX12TargetViewHeap::kInvalidIndex == index) return {};

        for (uint32_t i = 0; i < colors.size(); ++i)
        {
            const RHIColorTargetDesc& color = colors[i];
            ID3D12Resource* const resource = Resolve(color.resource);
            const bool defaultView = RHIFormat::Unknown == color.format &&
                0 == color.mipSlice && 0 == color.sliceCount;
            if (defaultView)
            {
                device->CreateRenderTargetView(resource, nullptr,
                    m_rtvViewHeap.CpuAt(index + i));
                continue;
            }

            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format = (RHIFormat::Unknown != color.format)
                ? ToDXGI(color.format) : resource->GetDesc().Format;
            if (0 == color.sliceCount)
            {
                rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtv.Texture2D.MipSlice = color.mipSlice;
            }
            else
            {
                rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.MipSlice = color.mipSlice;
                rtv.Texture2DArray.FirstArraySlice = color.firstSlice;
                rtv.Texture2DArray.ArraySize = color.sliceCount;
            }
            device->CreateRenderTargetView(resource, &rtv,
                m_rtvViewHeap.CpuAt(index + i));
        }

        rtvIndex = index;
        binding.colorCount = static_cast<uint32_t>(colors.size());
    }

    if (wantsDepth)
    {
        const uint32_t index = m_dsvViewHeap.Allocate(1);
        if (DX12TargetViewHeap::kInvalidIndex == index) return {};

        D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = ToDXGI(depth->format);
        dsv.Flags = depth->readOnly ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : D3D12_DSV_FLAG_NONE;

        if (0 == depth->sliceCount)
        {
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        }
        else
        {
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dsv.Texture2DArray.FirstArraySlice = depth->firstSlice;
            dsv.Texture2DArray.ArraySize = depth->sliceCount;
        }

        device->CreateDepthStencilView(Resolve(depth->resource), &dsv, m_dsvViewHeap.CpuAt(index));
        dsvIndex = index;
        binding.hasDepth = true;
    }

    binding.backend = DX12PackTargets(rtvIndex, dsvIndex);
    return binding;
}

void DX12DeviceResources::BindRenderTargets(ID3D12GraphicsCommandList* commandList,
    const RHIRenderTargetBinding& binding)
{
    if (nullptr == commandList || !binding.IsValid()) return;

    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    const bool hasDepth = binding.HasDepth();
    if (hasDepth) dsv = m_dsvViewHeap.CpuAt(DX12DsvIndexOf(binding.backend));

    if (!binding.HasColor())
    {
        commandList->OMSetRenderTargets(0, nullptr, FALSE, hasDepth ? &dsv : nullptr);
        return;
    }

    // 색 뷰는 연속으로 잘라 뒀으므로 시작 핸들 하나와 TRUE로 묶는다.
    // 낱개 배열을 만들어 넘기는 것과 결과는 같고, 배열을 세는 자리가 없다.
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvViewHeap.CpuAt(DX12RtvIndexOf(binding.backend));
    commandList->OMSetRenderTargets(binding.colorCount, &rtv, TRUE, hasDepth ? &dsv : nullptr);
}

void DX12DeviceResources::ClearRenderTargets(ID3D12GraphicsCommandList* commandList,
    const RHIRenderTargetBinding& binding, const float rgba[4])
{
    ClearRenderTargetsRect(commandList, binding, rgba, nullptr);
}

void DX12DeviceResources::ClearRenderTargetsRect(ID3D12GraphicsCommandList* commandList,
    const RHIRenderTargetBinding& binding, const float rgba[4], const D3D12_RECT* rect)
{
    if (nullptr == commandList || nullptr == rgba || !binding.HasColor()) return;

    for (uint32_t i = 0; i < binding.colorCount; ++i)
    {
        commandList->ClearRenderTargetView(
            m_rtvViewHeap.CpuAt(DX12RtvIndexOf(binding.backend) + i),
            rgba, (nullptr != rect) ? 1u : 0u, rect);
    }
}

void DX12DeviceResources::ClearDepthTarget(ID3D12GraphicsCommandList* commandList,
    const RHIRenderTargetBinding& binding, float depth)
{
    if (nullptr == commandList || !binding.HasDepth()) return;

    commandList->ClearDepthStencilView(m_dsvViewHeap.CpuAt(DX12DsvIndexOf(binding.backend)),
        D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
}

void DX12DeviceResources::ClearUnorderedAccess(ID3D12GraphicsCommandList* commandList,
    const RHIBindingDesc& view, const float rgba[4])
{
    ID3D12Resource* const viewResource = ResolveBinding(view);
    if (nullptr == commandList || nullptr == rgba || nullptr == viewResource) return;

    // ★ 같은 UAV를 두 벌 만든다. DX12가 셰이더 가시 GPU 핸들과 비가시 CPU
    //   핸들을 짝으로 요구하기 때문이고, 호출부가 그것을 알 이유가 없다.
    //   VolumetricFog가 이것 하나 때문에 비가시 힙을 따로 들고 같은 뷰를
    //   두 번 만들고 있었다.
    const RHIBindingTable shaderVisible = CreateBindings({ &view, 1 });
    if (!shaderVisible.IsValid()) return;

    const D3D12_CPU_DESCRIPTOR_HANDLE cpuOnly = CreateClearDescriptor(view);
    if (0 == cpuOnly.ptr) return;

    // 셰이더 가시 힙이 걸려 있어야 GPU 핸들이 뜻을 갖는다.
    BindDescriptorHeaps(commandList);

    commandList->ClearUnorderedAccessViewFloat(DX12ToGpuHandle(shaderVisible.backend), cpuOnly,
        viewResource, rgba, 0, nullptr);
}

// ── 패스 소유 리소스 (R2c) ──

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    void DevResApplyDebugName(ID3D12Resource* resource, const wchar_t* name)
    {
        if (nullptr != resource && nullptr != name) resource->SetName(name);
    }
}

bool DX12DeviceResources::CreateBuffer(const RHIBufferDesc& desc,
    RHIBufferHandle& outHandle, std::string& outError)
{
    if (nullptr == m_device) { outError = "디바이스가 없다"; return false; }
    if (0 == desc.bytes)     { outError = "버퍼 크기가 0이다"; return false; }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    // ★ 호출부가 매번 손으로 채우던 값들이 여기 모인다. 버퍼는 Height·
    //   DepthOrArraySize·MipLevels·SampleDesc.Count가 전부 1이어야 하고
    //   Layout이 ROW_MAJOR여야 한다 — 하나라도 빠지면 생성이 실패하는데,
    //   실패 지점이 '리소스가 널이다'로만 드러나 원인이 멀다.
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = desc.bytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = desc.allowUnorderedAccess
        ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        : D3D12_RESOURCE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, ToD3D12(desc.initialState), nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr))
    {
        outError = "버퍼 생성 실패 " + HrToString(hr);
        AppendDeviceRemovedReport(hr, outError);
        return false;
    }

    DevResApplyDebugName(resource.Get(), desc.debugName);

    // 표가 소유를 가져간다 — 호출부에는 핸들만 남는다(V2-a).
    outHandle = m_resourceTable.AddBuffer(std::move(resource));
    return true;
}

RHITextureInfo DX12DeviceResources::DescribeTexture(RHITextureHandle handle) const
{
    RHITextureInfo info{};

    ID3D12Resource* const native = m_resourceTable.Resolve(handle);
    if (nullptr == native) return info;

    const D3D12_RESOURCE_DESC desc = native->GetDesc();
    info.width = static_cast<uint32_t>(desc.Width);
    info.height = desc.Height;
    info.depthOrArraySize = desc.DepthOrArraySize;
    info.mipLevels = desc.MipLevels;
    info.format = FromDXGI(desc.Format);
    return info;
}

bool DX12DeviceResources::CreateTexture(const RHITextureDesc& desc,
    RHITextureHandle& outHandle, std::string& outError)
{
    if (nullptr == m_device) { outError = "디바이스가 없다"; return false; }
    if (0 == desc.width || 0 == desc.height)
    {
        outError = "텍스처 크기가 0이다";
        return false;
    }
    if (RHIFormat::Unknown == desc.format)
    {
        outError = "텍스처 포맷이 UNKNOWN이다";
        return false;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = (RHITextureDesc::Dim::Texture3D == desc.dim)
        ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
        : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.depthOrArraySize);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resourceDesc.Format = ToDXGI(desc.format);
    resourceDesc.SampleDesc.Count = 1;

    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (desc.allowUnorderedAccess) resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc.allowRenderTarget)    resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (desc.allowDepthStencil)    resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // ★ 클리어 힌트는 타깃일 때만 넘긴다 (G-1). 타깃이 아닌 리소스에 주면
    //   생성이 E_INVALIDARG 로 실패하고, 타깃인데 안 주면 검증 레이어가
    //   경고를 남긴다 — 둘 다 실패로 나타나므로 조건을 여기 한 곳에 둔다.
    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resourceDesc.Format;
    if (desc.allowDepthStencil)
    {
        clearValue.DepthStencil.Depth = desc.clearDepth;
    }
    else
    {
        for (int i = 0; i < 4; ++i) clearValue.Color[i] = desc.clearColor[i];
    }
    const bool wantsClearValue = desc.allowRenderTarget || desc.allowDepthStencil;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    const HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &resourceDesc, ToD3D12(desc.initialState),
        wantsClearValue ? &clearValue : nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr))
    {
        outError = "텍스처 생성 실패 " + HrToString(hr);
        AppendDeviceRemovedReport(hr, outError);
        return false;
    }

    DevResApplyDebugName(resource.Get(), desc.debugName);

    // 표가 소유를 가져간다 — 호출부에는 핸들만 남는다(V2-a).
    outHandle = m_resourceTable.AddTexture(std::move(resource));
    return true;
}

// ── 리드백 (R2c-b) ──

// ★ 바이트 표를 여기서 걷었다(V1). RHIFormatBytes가 답한다 — 표가 둘이면
//   언젠가 갈리고, 갈린 결과는 '행 간격이 어긋나 그림이 비스듬해진다'로만
//   드러나 원인이 멀다. 모르는 포맷에 0을 주고 호출부가 거절하는 규약은 그대로다.

bool DX12DeviceResources::CreateReadback(uint32_t width, uint32_t height,
    RHIFormat format, uint32_t sliceCount, RHIReadback& outReadback, std::string& outError)
{
    if (nullptr == m_device) { outError = "디바이스가 없다"; return false; }
    if (0 == width || 0 == height || 0 == sliceCount)
    {
        outError = "리드백 크기가 0이다";
        return false;
    }

    const uint32_t bytesPerPixel = RHIFormatBytes(format);
    if (0 == bytesPerPixel)
    {
        outError = "리드백이 모르는 포맷이다";
        return false;
    }

    // ★ 행 간격 정렬을 여기서 한 번만 한다. 검사 열여덟 곳이 각자 상수로
    //   들고 있던 계산이고, 하나만 어긋나도 읽은 그림이 한 행씩 밀린다.
    const uint32_t rowPitch =
        (width * bytesPerPixel + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

    const size_t sliceBytes = static_cast<size_t>(rowPitch) * height;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sliceBytes * sliceCount;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    const HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer));
    if (FAILED(hr))
    {
        outError = "리드백 버퍼 생성 실패 " + HrToString(hr);
        AppendDeviceRemovedReport(hr, outError);
        return false;
    }

    outReadback = RHIReadback{};
    // 표가 소유를 가져간다 (5a). 놓는 것은 ReleaseReadback 이다.
    outReadback.buffer = m_resourceTable.AddBuffer(std::move(buffer));
    if (!outReadback.buffer.IsValid())
    {
        outError = "리드백 등록 실패 — 표가 가득 찼다";
        return false;
    }
    outReadback.width = width;
    outReadback.height = height;
    outReadback.rowPitch = rowPitch;
    outReadback.format = format;
    outReadback.sliceCount = sliceCount;
    outReadback.sliceBytes = sliceBytes;
    return true;
}

bool DX12DeviceResources::CreateBufferReadback(uint64_t bytes,
    RHIReadback& outReadback, std::string& outError)
{
    if (nullptr == m_device) { outError = "디바이스가 없다"; return false; }
    if (0 == bytes) { outError = "리드백 크기가 0이다"; return false; }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    const HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
        &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer));
    if (FAILED(hr))
    {
        outError = "버퍼 리드백 생성 실패 " + HrToString(hr);
        AppendDeviceRemovedReport(hr, outError);
        return false;
    }

    // ★ 포맷은 UNKNOWN으로 둔다. 픽셀이 아니므로 At()이 답할 것이 없고,
    //   읽는 쪽은 Elements<T>()를 쓴다. width에 바이트 수를 담아 두면
    //   MapReadback이 그대로 크기를 얻는다.
    outReadback = RHIReadback{};
    // 표가 소유를 가져간다 (5a). 놓는 것은 ReleaseReadback 이다.
    outReadback.buffer = m_resourceTable.AddBuffer(std::move(buffer));
    if (!outReadback.buffer.IsValid())
    {
        outError = "리드백 등록 실패 — 표가 가득 찼다";
        return false;
    }
    outReadback.width = static_cast<uint32_t>(bytes);
    outReadback.height = 1;
    outReadback.rowPitch = static_cast<uint32_t>(bytes);
    outReadback.format = RHIFormat::Unknown;
    outReadback.sliceCount = 1;
    outReadback.sliceBytes = static_cast<size_t>(bytes);
    return true;
}

bool DX12DeviceResources::MapReadback(const RHIReadback& readback,
    RHIReadbackImage& outImage, std::string& outError)
{
    if (!readback.IsValid()) { outError = "리드백이 비어 있다"; return false; }

    const size_t totalBytes = readback.sliceBytes * readback.sliceCount;

    // 읽을 범위를 정확히 준다. 0,0을 주면 드라이버가 '아무것도 안 읽는다'로
    // 보고 최적화할 수 있다.
    const D3D12_RANGE range{ 0, totalBytes };
    void* mapped = nullptr;
    ID3D12Resource* const native = m_resourceTable.Resolve(readback.buffer);
    if (nullptr == native) { outError = "리드백 버퍼가 이미 놓였다"; return false; }

    const HRESULT hr = native->Map(0, &range, &mapped);
    if (FAILED(hr) || nullptr == mapped)
    {
        outError = "리드백 Map 실패 " + HrToString(hr);
        return false;
    }

    outImage.data.assign(static_cast<const uint8_t*>(mapped),
        static_cast<const uint8_t*>(mapped) + totalBytes);

    // ★ 쓰지 않았으므로 빈 범위를 준다. 여기에 전체 범위를 주면 드라이버가
    //   CPU가 쓴 것으로 보고 되돌려 쓸 수 있다.
    const D3D12_RANGE written{ 0, 0 };
    native->Unmap(0, &written);

    outImage.width = readback.width;
    outImage.height = readback.height;
    outImage.rowPitch = readback.rowPitch;
    outImage.format = readback.format;
    outImage.sliceCount = readback.sliceCount;
    outImage.sliceBytes = readback.sliceBytes;
    return true;
}

#endif
