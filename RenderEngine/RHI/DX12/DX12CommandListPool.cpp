#ifndef DYNAMICCPP_EXPORTS
#include "DX12CommandListPool.h"

#include <algorithm>
#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string CmdPoolHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }
}

bool DX12CommandListPool::Initialize(ID3D12Device* device, uint32_t workerCount,
    uint32_t frameCount, std::string& outError)
{
    if (nullptr == device || 0 == workerCount || 0 == frameCount)
    {
        outError = "커맨드 리스트 풀 인자가 잘못됐다";
        return false;
    }

    workerCount = (std::min)(workerCount, kMaxWorkers);

    m_device = device;
    m_workerCount = workerCount;
    m_frameCount = frameCount;
    m_frameIndex = 0;

    m_slots.assign(frameCount, std::vector<Slot>(workerCount));

    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        for (uint32_t worker = 0; worker < workerCount; ++worker)
        {
            Slot& slot = m_slots[frame][worker];

            HRESULT hr = m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&slot.allocator));
            if (FAILED(hr))
            {
                outError = "커맨드 얼로케이터 생성 실패 " + CmdPoolHrToString(hr);
                return false;
            }

            hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                slot.allocator.Get(), nullptr, IID_PPV_ARGS(&slot.list));
            if (FAILED(hr))
            {
                outError = "커맨드 리스트 생성 실패 " + CmdPoolHrToString(hr);
                return false;
            }

            // 만들자마자 열린 상태다. 닫아 두고 BeginFrame이 열게 한다 —
            // '열려 있는가'를 한 곳에서만 관리해야 어긋나지 않는다.
            slot.list->Close();
            slot.opened = false;

            const std::wstring name = L"DX12CommandListPool[" + std::to_wstring(frame)
                + L"][" + std::to_wstring(worker) + L"]";
            slot.list->SetName(name.c_str());
        }
    }

    return true;
}

void DX12CommandListPool::Shutdown()
{
    m_slots.clear();
    m_device.Reset();
    m_workerCount = 0;
    m_frameCount = 0;
    m_frameIndex = 0;
}

void DX12CommandListPool::BeginFrame(uint32_t frameIndex)
{
    if (m_slots.empty()) return;

    m_frameIndex = frameIndex % m_frameCount;

    // 이 구간의 GPU 작업은 호출부의 펜스 대기로 이미 끝나 있다. 그래서 여기서
    // 얼로케이터를 되돌려도 GPU가 읽는 중인 메모리를 건드리지 않는다 —
    // 업로드 링·디스크립터 링과 같은 계약이다.
    for (Slot& slot : m_slots[m_frameIndex])
    {
        slot.opened = false;
    }
}

ID3D12GraphicsCommandList* DX12CommandListPool::Open(uint32_t worker, std::string& outError)
{
    if (m_slots.empty() || worker >= m_workerCount)
    {
        outError = "워커 번호가 범위를 벗어났다";
        return nullptr;
    }

    Slot& slot = m_slots[m_frameIndex][worker];
    if (slot.opened) return slot.list.Get();

    HRESULT hr = slot.allocator->Reset();
    if (FAILED(hr))
    {
        outError = "얼로케이터 Reset 실패 " + CmdPoolHrToString(hr);
        return nullptr;
    }

    hr = slot.list->Reset(slot.allocator.Get(), nullptr);
    if (FAILED(hr))
    {
        outError = "커맨드 리스트 Reset 실패 " + CmdPoolHrToString(hr);
        return nullptr;
    }

    slot.opened = true;
    return slot.list.Get();
}

bool DX12CommandListPool::CloseAll(std::string& outError)
{
    if (m_slots.empty()) return true;

    for (Slot& slot : m_slots[m_frameIndex])
    {
        if (!slot.opened) continue;

        const HRESULT hr = slot.list->Close();
        if (FAILED(hr))
        {
            outError = "커맨드 리스트 Close 실패 " + CmdPoolHrToString(hr);
            return false;
        }
    }

    return true;
}

bool DX12CommandListPool::HasRecorded(uint32_t worker) const
{
    if (m_slots.empty() || worker >= m_workerCount) return false;
    return m_slots[m_frameIndex][worker].opened;
}

ID3D12GraphicsCommandList* DX12CommandListPool::Get(uint32_t worker) const
{
    if (m_slots.empty() || worker >= m_workerCount) return nullptr;
    return m_slots[m_frameIndex][worker].list.Get();
}

#endif
