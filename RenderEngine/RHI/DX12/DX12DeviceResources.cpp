#ifndef DYNAMICCPP_EXPORTS
#include "DX12DeviceResources.h"

#include <sstream>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
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

#if defined(_DEBUG)
    // 검증 레이어는 디바이스 생성 전에 켜야 한다. 브링업 단계에서는 이 레이어가
    // 곧 테스트 하네스다 — 메시지 0건이 통과 조건.
    {
        Microsoft::WRL::ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
        }
    }
#endif

    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory));
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

#if defined(_DEBUG)
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
            D3D12_MESSAGE_ID denied[] = { D3D12_MESSAGE_ID_LOADPIPELINE_NAMENOTFOUND };
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = _countof(denied);
            filter.DenyList.pIDList = denied;
            infoQueue->AddStorageFilterEntries(&filter);
        }
    }
#endif

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
        if (FAILED(hr)) { outError = "펜스 대기 설정 실패 " + HrToString(hr); return false; }
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }

    HRESULT hr = allocator->Reset();
    if (FAILED(hr)) { outError = "얼로케이터 Reset 실패 " + HrToString(hr); return false; }

    hr = m_commandList->Reset(allocator.Get(), nullptr);
    if (FAILED(hr)) { outError = "커맨드 리스트 Reset 실패 " + HrToString(hr); return false; }

    // 업로드 링 되감기는 반드시 위 펜스 대기 뒤여야 한다. GPU가 이 슬롯의
    // 프레임을 끝냈다는 사실이 곧 그 구간을 다시 써도 된다는 근거다.
    m_uploadRing.BeginFrame(m_frameIndex);
    m_descriptorRing.BeginFrame(m_frameIndex);

    return true;
}

bool DX12DeviceResources::FlushCommandList(std::string& outError)
{
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr)) { outError = "중간 제출 Close 실패 " + HrToString(hr); return false; }

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    // 얼로케이터는 그대로 둔다. 리스트만 다시 여는 것은 제출 직후에도 된다 —
    // 얼로케이터를 되돌리면 GPU가 읽는 중인 메모리를 재사용하게 된다.
    hr = m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr);
    if (FAILED(hr)) { outError = "중간 제출 Reset 실패 " + HrToString(hr); return false; }

    return true;
}

bool DX12DeviceResources::EndFrame(std::string& outError)
{
    HRESULT hr = m_commandList->Close();
    if (FAILED(hr)) { outError = "커맨드 리스트 Close 실패 " + HrToString(hr); return false; }

    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    const uint64_t fenceValue = m_nextFenceValue++;
    hr = m_queue->Signal(m_fence.Get(), fenceValue);
    if (FAILED(hr)) { outError = "펜스 Signal 실패 " + HrToString(hr); return false; }

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

#endif
