#ifndef DYNAMICCPP_EXPORTS
#include "DX12CommandListPool.h"
#include "DX12DeviceResources.h"
#include "DX12Encoder.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

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

DX12CommandListPool::DX12CommandListPool() = default;

DX12CommandListPool::~DX12CommandListPool()
{
    Shutdown();
}

bool DX12CommandListPool::Initialize(DX12DeviceResources& resources, uint32_t workerCount,
    uint32_t frameCount, std::string& outError)
{
    ID3D12Device* const device = resources.GetDevice();
    if (nullptr == device || 0 == workerCount || 0 == frameCount)
    {
        outError = "커맨드 리스트 풀 인자가 잘못됐다";
        return false;
    }

    workerCount = (std::min)(workerCount, kMaxWorkers);

    m_device = device;
    m_resources = &resources;
    m_workerCount = workerCount;
    m_frameCount = frameCount;
    m_frameIndex = 0;

    m_slots.assign(frameCount, std::vector<Slot>(workerCount));
    m_encoders.resize(workerCount);

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

    // 워커 스레드를 띄운다. 워커 0은 호출 스레드가 맡으므로 만들지 않는다.
    m_stopping = false;
    m_threads.reserve(workerCount - 1);
    for (uint32_t worker = 1; worker < workerCount; ++worker)
    {
        m_threads.emplace_back(&DX12CommandListPool::WorkerLoop, this, worker);
    }

    return true;
}

void DX12CommandListPool::WorkerLoop(uint32_t worker)
{
    uint64_t seen = 0;

    for (;;)
    {
        const std::function<void(uint32_t)>* job = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wakeSignal.wait(lock, [&]
            {
                return m_stopping || (m_generation != seen && worker < m_jobWorkerCount);
            });

            if (m_stopping) return;

            seen = m_generation;
            job = m_job;
        }

        if (nullptr != job) (*job)(worker);

        {
            std::lock_guard<std::mutex> guard(m_mutex);
            if (0 != m_pending && 0 == --m_pending)
            {
                m_doneSignal.notify_one();
            }
        }
    }
}

void DX12CommandListPool::RunParallel(const std::function<void(uint32_t)>& job,
    uint32_t workerCount)
{
    if (0 == workerCount) return;

    if (1 == workerCount || m_threads.empty())
    {
        // 워커가 하나면 깨우지 않는다. 비교 기준을 잴 때 동기화 비용이 섞이면
        // '병렬이 느리다'가 무엇 때문인지 흐려진다.
        job(0);
        return;
    }

    workerCount = (std::min)(workerCount, m_workerCount);

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        m_job = &job;
        m_jobWorkerCount = workerCount;
        m_pending = workerCount - 1;   // 워커 0은 호출 스레드
        ++m_generation;
    }
    m_wakeSignal.notify_all();

    job(0);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_doneSignal.wait(lock, [&] { return 0 == m_pending; });
        m_job = nullptr;
        m_jobWorkerCount = 0;
    }
}

void DX12CommandListPool::Shutdown()
{
    // 스레드를 먼저 세운다. 슬롯을 놓은 뒤에 세우면 워커가 사라진 리스트를 만진다.
    if (!m_threads.empty())
    {
        {
            std::lock_guard<std::mutex> guard(m_mutex);
            m_stopping = true;
            ++m_generation;
        }
        m_wakeSignal.notify_all();

        for (auto& thread : m_threads)
        {
            if (thread.joinable()) thread.join();
        }
        m_threads.clear();
    }

    m_slots.clear();
    m_encoders.clear();
    m_device.Reset();
    m_resources = nullptr;
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

bool DX12CommandListPool::Prepare(std::string& outError)
{
    if (nullptr == m_resources)
    {
        outError = "DX12 병렬 풀이 디바이스 서비스와 연결되지 않았다";
        return false;
    }
    return m_resources->FlushCommandList(outError);
}

bool DX12CommandListPool::OpenWorker(uint32_t worker, std::string& outError)
{
    return nullptr != Open(worker, outError);
}

RHIEncoder& DX12CommandListPool::AcquireEncoder(uint32_t worker)
{
    if (worker >= m_workerCount)
        throw std::out_of_range("DX12 병렬 encoder worker 범위 초과");

    ID3D12GraphicsCommandList* const list = Get(worker);
    std::unique_ptr<DX12Encoder>& encoder = m_encoders[worker];
    if (!encoder) encoder = std::make_unique<DX12Encoder>(list, m_resources);
    else encoder->ResetState(list);
    return *encoder;
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

bool DX12CommandListPool::PrepareRecordedCommands(uint32_t frameSlot,
    RHICompletionPoint& outCompletion, std::string& outError)
{
    if (nullptr == m_resources || frameSlot >= m_slots.size())
    {
        outError = "DX12 기록 batch 제출 준비 대상이 잘못됐다";
        return false;
    }
    return m_resources->PrepareParallelSubmission(outCompletion, outError);
}

bool DX12CommandListPool::SubmitRecordedCommands(uint32_t frameSlot,
    std::span<const uint32_t> workerOrder, RHICompletionPoint completion,
    std::string& outError)
{
    if (nullptr == m_resources)
    {
        outError = "DX12 병렬 제출 coordinator가 없다";
        return false;
    }

    if (frameSlot >= m_slots.size())
    {
        outError = "DX12 기록 batch frame slot이 범위를 벗어났다";
        return false;
    }

    std::vector<ID3D12CommandList*> submission;
    submission.reserve(workerOrder.size());
    for (uint32_t worker : workerOrder)
    {
        if (worker >= m_workerCount) continue;
        const Slot& slot = m_slots[frameSlot][worker];
        if (!slot.opened) continue;
        ID3D12GraphicsCommandList* const list = slot.list.Get();
        if (nullptr != list) submission.push_back(list);
    }
    return m_resources->SubmitCommandLists(submission, completion, outError);
}

#endif
