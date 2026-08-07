#ifndef DYNAMICCPP_EXPORTS
#include "DX12DeviceResources.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

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

    // ── 리드백 버퍼(행 정렬 256바이트 규약) ──
    m_rowPitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = static_cast<uint64_t>(m_rowPitch) * height;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // 버퍼는 초기 상태 지정이 무시되고 COMMON으로 만들어진다 — COPY_DEST를 넘기면
    // 검증 레이어가 경고를 쌓는다(업로드 링 검증에서 잡혔다).
    hr = m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_readback));
    if (FAILED(hr)) { outError = "리드백 버퍼 생성 실패 " + HrToString(hr); return false; }

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
    WaitForGpu();

    m_renderTarget.Reset();
    m_readback.Reset();

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

    // 프레임 업로드 링. 첫 잠정치는 8MB였고, 실측이 한도를 알려 줬다 —
    // 엔진 스카이박스 큐브맵(512² x 6면 RGBA16F)이 12.6MB라 텍스처 캐시
    // 운반이 8MB에서 거절됐다. 16MB로 올린다. 이보다 큰 단일 텍스처
    // (예: 2048x1024 초과 HDR equirect ≈ 16.7MB+)는 여전히 거절되며,
    // 그때는 서브리소스 분할 업로드가 필요하다(운반 검증이 스킵으로 알린다).
    constexpr uint64_t kUploadBytesPerFrame = 16ull * 1024 * 1024;
    if (!m_uploadRing.Initialize(m_device.Get(), kUploadBytesPerFrame, kFrameCount, outError))
    {
        return false;
    }

    // 디스크립터 링·샘플러 힙. 크기는 브링업 잠정치이고, 실제 씬을 이식하면
    // peakFrameDescriptors가 필요한 값을 알려 준다(추정하지 말고 재서 정한다).
    constexpr uint32_t kDescriptorsPerFrame = 4096;
    if (!m_descriptorRing.Initialize(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        kDescriptorsPerFrame, kFrameCount, outError))
    {
        return false;
    }

    constexpr uint32_t kSamplerCapacity = 128;
    if (!m_samplerHeap.Initialize(m_device.Get(), kSamplerCapacity, outError))
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

    return true;
}

void DX12DeviceResources::Shutdown()
{
    // 구독을 먼저 끊는다. 해체 중에 리사이즈 콜백이 들어오면 이미 놓은 것을
    // 만진다.
    ScreenResizeBus::Get().Unsubscribe(m_resizeSubscription);
    m_resizeSubscription = ScreenResizeBus::kInvalidHandle;

    if (m_device)
    {
        WaitForGpu();
    }

    // 스왑체인은 백버퍼 참조를 먼저 놓아야 곱게 죽는다(GPU 완주는 위에서 확인).
    for (auto& backBuffer : m_backBuffers) backBuffer.Reset();
    m_backBufferRtvHeap.Reset();
    m_swapChain.Reset();

    // GPU가 다 끝난 뒤에 Unmap한다. 순서가 반대면 아직 읽는 중인 메모리를 푼다.
    m_uploadRing.Shutdown();
    m_descriptorRing.Shutdown();
    m_samplerHeap.Shutdown();

    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    // ComPtr가 역순 해제를 처리한다. 멤버 선언 순서가 곧 해제 역순이다.
}

bool DX12DeviceResources::BeginFrame(std::string& outError)
{
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

    hr = m_commandList->Reset(allocator.Get(), nullptr);
    if (FAILED(hr)) { outError = "커맨드 리스트 Reset 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    // 업로드 링 되감기는 반드시 위 펜스 대기 뒤여야 한다. GPU가 이 슬롯의
    // 프레임을 끝냈다는 사실이 곧 그 구간을 다시 써도 된다는 근거다.
    m_uploadRing.BeginFrame(m_frameIndex);
    m_descriptorRing.BeginFrame(m_frameIndex);

    return true;
}

bool DX12DeviceResources::FlushCommandList(std::string& outError)
{
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr)) { outError = "중간 제출 Close 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    // 얼로케이터는 그대로 둔다. 리스트만 다시 여는 것은 제출 직후에도 된다 —
    // 얼로케이터를 되돌리면 GPU가 읽는 중인 메모리를 재사용하게 된다.
    hr = m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr);
    if (FAILED(hr)) { outError = "중간 제출 Reset 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    return true;
}

bool DX12DeviceResources::EndFrame(std::string& outError)
{
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr)) { outError = "커맨드 리스트 Close 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    const uint64_t fenceValue = m_nextFenceValue++;
    hr = m_queue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr)) { outError = "펜스 Signal 실패 " + HrToString(hr); AppendDeviceRemovedReport(hr, outError); return false; }

    m_frameFenceValues[m_frameIndex] = fenceValue;
    m_frameIndex = (m_frameIndex + 1) % kFrameCount;
    return true;
}

void DX12DeviceResources::WaitForGpu()
{
    if (!m_queue || !m_fence) return;

    const uint64_t fenceValue = m_nextFenceValue++;
    if (FAILED(m_queue->Signal(m_fence.Get(), fenceValue))) return;

    if (m_fence->GetCompletedValue() < fenceValue)
    {
        if (SUCCEEDED(m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent)))
        {
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
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
    WaitForGpu();
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
    const HRESULT hr = m_swapChain->Present(0, 0);
    if (FAILED(hr))
    {
        outError = "Present 실패 " + HrToString(hr);
        AppendDeviceRemovedReport(hr, outError);
        return false;
    }
    return true;
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
    DXGI_FORMAT DepthToColorFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_D32_FLOAT:             return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D16_UNORM:             return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:     return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        default:                                return format;  // 이미 색 포맷이다
        }
    }

    /// UNKNOWN이면 리소스가 아는 포맷을 쓴다. SRV 설명을 명시하는 경로에서는
    /// UNKNOWN이 그대로 유효한 값이 아니라서, 여기서 반드시 채워야 한다.
    DXGI_FORMAT ResolveSrvFormat(const RHIBindingDesc& desc)
    {
        // 널 디스크립터는 물어볼 리소스가 없다 — 적어 준 포맷을 그대로 쓴다.
        if (nullptr == desc.resource) return desc.format;

        const DXGI_FORMAT resourceFormat = desc.resource->GetDesc().Format;

        if (desc.depthAsColor)  return DepthToColorFormat(resourceFormat);
        if (DXGI_FORMAT_UNKNOWN == desc.format) return resourceFormat;
        return desc.format;
    }
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
        if (nullptr == desc.resource && !desc.allowNull) return {};
    }

    const auto range = m_descriptorRing.Allocate(static_cast<uint32_t>(descs.size()));
    if (!range.IsValid()) return {};

    ID3D12Device* device = m_device.Get();
    for (uint32_t i = 0; i < descs.size(); ++i)
    {
        const RHIBindingDesc& desc = descs[i];
        const D3D12_CPU_DESCRIPTOR_HANDLE handle = range.CpuAt(i);

        if (RHIBindingDesc::Kind::UnorderedAccess == desc.kind)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = desc.format;
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
            device->CreateUnorderedAccessView(desc.resource, nullptr, &uav, handle);
            continue;
        }

        // Default는 리소스가 스스로 아는 대로 본다 — nullptr 설명과 같다.
        if (RHIBindingDesc::Dim::Default == desc.dim)
        {
            device->CreateShaderResourceView(desc.resource, nullptr, handle);
            continue;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = ResolveSrvFormat(desc);
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
        device->CreateShaderResourceView(desc.resource, &srv, handle);
    }

    RHIBindingTable table{};
    table.gpu = range.gpu;
    table.count = static_cast<uint32_t>(descs.size());
    return table;
}

void DX12DeviceResources::BindDescriptorHeaps(ID3D12GraphicsCommandList* commandList,
    bool withSamplers)
{
    if (nullptr == commandList) return;

    if (withSamplers)
    {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorRing.GetHeap(), m_samplerHeap.GetHeap() };
        commandList->SetDescriptorHeaps(2, heaps);
        return;
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorRing.GetHeap() };
    commandList->SetDescriptorHeaps(1, heaps);
}

#endif
