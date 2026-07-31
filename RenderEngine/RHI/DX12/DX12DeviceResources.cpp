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

bool DX12DeviceResources::Initialize(uint32_t width, uint32_t height, std::string& outError)
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

    // 고성능 어댑터 우선 — DX11 쪽과 다른 어댑터가 잡히면 비교가 무의미해지므로
    // 같은 정책(고성능 우선)을 쓴다.
    hr = m_factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        IID_PPV_ARGS(&m_adapter));
    if (FAILED(hr)) { outError = "어댑터 열거 실패 " + HrToString(hr); return false; }

    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device));
    if (FAILED(hr)) { outError = "D3D12 디바이스 생성 실패 " + HrToString(hr); return false; }

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

    hr = m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &rtDesc,
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

    hr = m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readback));
    if (FAILED(hr)) { outError = "리드백 버퍼 생성 실패 " + HrToString(hr); return false; }

    return true;
}

void DX12DeviceResources::Shutdown()
{
    if (m_device)
    {
        WaitForGpu();
    }
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
    for (uint64_t i = 0; i < count; ++i)
    {
        SIZE_T length = 0;
        infoQueue->GetMessage(i, nullptr, &length);
        std::string storage(length, '\0');
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (SUCCEEDED(infoQueue->GetMessage(i, message, &length)))
        {
            outMessages += message->pDescription;
            outMessages += "\n";
        }
    }
    infoQueue->ClearStoredMessages();
    return static_cast<uint32_t>(count);
#else
    return 0;
#endif
}

#endif
