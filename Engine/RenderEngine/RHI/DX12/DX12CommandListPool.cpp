#include "DX12CommandListPool.h"
#include "DX12DeviceResources.h"
#include "DX12Encoder.h"

#include "../../../Utility_Framework/StandardLibrary.h"

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string CmdPoolHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }
} // namespace

DX12CommandListPool::DX12CommandListPool(job_scheduler& scheduler) : IRHIParallelCommandPool(scheduler) {}

DX12CommandListPool::~DX12CommandListPool()
{
    Shutdown();
}

bool DX12CommandListPool::Initialize(DX12DeviceResources& resources, uint32_t workerCount, uint32_t frameCount,
                                     std::string& outError)
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

            HRESULT hr =
                m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator));
            if (FAILED(hr))
            {
                outError = "커맨드 얼로케이터 생성 실패 " + CmdPoolHrToString(hr);
                Shutdown();
                return false;
            }

            hr = m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&slot.list));
            if (FAILED(hr))
            {
                outError = "커맨드 리스트 생성 실패 " + CmdPoolHrToString(hr);
                Shutdown();
                return false;
            }

            // 만들자마자 열린 상태다. 닫아 두고 BeginFrame이 열게 한다 —
            // '열려 있는가'를 한 곳에서만 관리해야 어긋나지 않는다.
            slot.list->Close();
            slot.opened = false;

            const std::wstring name =
                L"DX12CommandListPool[" + std::to_wstring(frame) + L"][" + std::to_wstring(worker) + L"]";
            slot.list->SetName(name.c_str());
        }
    }

    return true;
}

void DX12CommandListPool::Shutdown()
{
    // owner의 RunParallel 반환으로 CPU 기록은 끝났다. GPU/제출 drain은 호출부가 맡는다.
    m_slots.clear();
    m_encoders.clear();
    m_translationPools.clear();
    m_device.Reset();
    m_recordingResources.reset();
    m_resources = nullptr;
    m_workerCount = 0;
    m_frameCount = 0;
    m_frameIndex = 0;
}

void DX12CommandListPool::BeginFrame(uint32_t frameIndex)
{
    if (m_slots.empty())
    {
        return;
    }

    m_frameIndex = frameIndex % m_frameCount;

    // 이 구간의 GPU 작업은 호출부의 펜스 대기로 이미 끝나 있다. 그래서 여기서
    // 얼로케이터를 되돌려도 GPU가 읽는 중인 메모리를 건드리지 않는다 —
    // 업로드 링·디스크립터 링과 같은 계약이다.
    for (Slot& slot : std::span(m_slots[m_frameIndex].data(), m_slots[m_frameIndex].size()))
    {
        slot.opened = false;
    }
}

bool DX12CommandListPool::CreateTranslationPool(std::shared_ptr<DX12CommandListPool>& result, std::string& outError)
{
    if (result || !m_resources || m_slots.empty() || m_recordingResources)
    {
        outError = "Translation pool requires an initialized producer pool and empty output.";
        return false;
    }
    for (const auto& slot : std::span(m_slots[m_frameIndex].data(), m_slots[m_frameIndex].size()))
    {
        if (slot.opened)
        {
            outError = "Cannot fork a native target already opened by the producer.";
            return false;
        }
    }
    m_translationPools.resize(m_frameCount);
    auto& cached = m_translationPools[m_frameIndex];
    auto frozen = cached && cached.use_count() == 1 ? cached : std::make_shared<DX12CommandListPool>();
    // Only an exclusively cached fork reaches this point. Its previous CPU
    // translation and GPU retirement owners have gone away.
    auto previousResources = std::move(frozen->m_recordingResources);
    for (auto& encoder : std::span(frozen->m_encoders.data(), frozen->m_encoders.size()))
    {
        if (encoder)
        {
            encoder->SetRecordingResources({});
        }
    }
    if (!m_resources->CaptureRecordingResources(frozen->m_recordingResources, outError, previousResources.get()))
    {
        return false;
    }
    frozen->m_device = m_device;
    frozen->m_resources = m_resources;
    frozen->m_workerCount = m_workerCount;
    frozen->m_frameCount = 1;
    frozen->m_frameIndex = 0;
    frozen->m_slots.resize(1);
    frozen->m_slots[0].resize(m_workerCount);
    std::copy_n(m_slots[m_frameIndex].data(), m_workerCount, frozen->m_slots[0].data());
    frozen->m_encoders.resize(m_workerCount);
    cached = frozen;
    result = std::move(frozen);
    return true;
}

bool DX12CommandListPool::GetTranslationRecording(uint64_t& uploadRecordingId, uint64_t& descriptorVersion) const
{
    uploadRecordingId = m_recordingResources ? m_recordingResources->uploadRecordingId : 0;
    descriptorVersion = m_recordingResources ? m_recordingResources->descriptors.version : 0;
    return m_recordingResources != nullptr;
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
    {
        throw std::out_of_range("DX12 병렬 encoder worker 범위 초과");
    }

    ID3D12GraphicsCommandList* const list = Get(worker);
    std::unique_ptr<DX12Encoder>& encoder = m_encoders[worker];
    if (!encoder)
    {
        encoder = std::make_unique<DX12Encoder>(list, m_resources);
    }
    else
    {
        encoder->ResetState(list);
    }
    encoder->SetRecordingResources(m_recordingResources);
    return *encoder;
}

RHIEncoder& DX12CommandListPool::AcquireTranslationEncoder(uint32_t worker)
{
    if (!m_recordingResources)
    {
        return AcquireEncoder(worker);
    }
    if (worker >= m_workerCount || m_slots.empty() || !m_slots[m_frameIndex][worker].opened)
    {
        throw std::out_of_range("Translation encoder requires an open frozen target.");
    }
    auto& slot = m_slots[m_frameIndex][worker];
    if (!slot.translationBegun)
    {
        auto& encoder = AcquireEncoder(worker);
        slot.translationBegun = true;
        return encoder;
    }
    auto& encoder = *m_encoders[worker];
    encoder.ResetSoftwareSegment();
    return encoder;
}

ID3D12GraphicsCommandList* DX12CommandListPool::Open(uint32_t worker, std::string& outError)
{
    if (m_slots.empty() || worker >= m_workerCount)
    {
        outError = "워커 번호가 범위를 벗어났다";
        return nullptr;
    }

    Slot& slot = m_slots[m_frameIndex][worker];
    if (slot.opened)
    {
        return slot.list.Get();
    }

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

    slot.translationBegun = false;
    slot.opened = true;
    return slot.list.Get();
}

uint32_t DX12CommandListPool::DrainEncoderDrops(std::string& outLast)
{
    // DX12는 인코더가 워커별로 살아 있으므로 여기서 훑어 모은다. 읽으면서
    // 비운다 — 누적을 남기면 프레임별 판정이 성립하지 않는다.
    uint32_t total = 0;
    const char* last = nullptr;
    for (auto& encoder : std::span(m_encoders.data(), m_encoders.size()))
    {
        if (!encoder)
        {
            continue;
        }
        const uint32_t count = encoder->GetDroppedCount();
        if (0 == count)
        {
            continue;
        }
        total += count;
        last = encoder->GetLastDropped();
        encoder->ClearDropped();
    }
    outLast = nullptr != last ? last : std::string{};
    return total;
}

bool DX12CommandListPool::CloseAll(std::string& outError)
{
    if (m_slots.empty())
    {
        return true;
    }

    for (Slot& slot : std::span(m_slots[m_frameIndex].data(), m_slots[m_frameIndex].size()))
    {
        if (!slot.opened)
        {
            continue;
        }

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
    if (m_slots.empty() || worker >= m_workerCount)
    {
        return false;
    }
    return m_slots[m_frameIndex][worker].opened;
}

ID3D12GraphicsCommandList* DX12CommandListPool::Get(uint32_t worker) const
{
    if (m_slots.empty() || worker >= m_workerCount)
    {
        return nullptr;
    }
    return m_slots[m_frameIndex][worker].list.Get();
}

uint64_t DX12CommandListPool::GetCurrentRecordingId() const
{
    return m_recordingResources ? m_recordingResources->uploadRecordingId
                                : (m_resources ? m_resources->GetCurrentUploadRecordingId() : 0);
}

void DX12CommandListPool::RejectPreparedCommands(uint64_t recordingId, RHICompletionPoint completion)
{
    if (m_resources)
    {
        m_resources->RejectNeverEnqueuedRecording(recordingId, completion);
    }
}

void DX12CommandListPool::AcceptPreparedCommands(RHICompletionPoint completion, const RHISubmissionTicket& ticket)
{
    if (m_resources)
    {
        m_resources->AcceptParallelSubmission(completion, ticket);
    }
}

bool DX12CommandListPool::PrepareRecordedCommands(uint32_t frameSlot, RHICompletionPoint& outCompletion,
                                                  std::string& outError)
{
    if (nullptr == m_resources || frameSlot >= m_slots.size())
    {
        outError = "DX12 기록 batch 제출 준비 대상이 잘못됐다";
        return false;
    }
    if (m_recordingResources &&
        (m_resources->GetCurrentUploadRecordingId() != m_recordingResources->uploadRecordingId ||
         m_resources->GetDescriptorVersionToken() != m_recordingResources->descriptors.version))
    {
        outError = "Frozen translation pool admission no longer owns the producer recording.";
        return false;
    }
    return m_resources->PrepareParallelSubmission(outCompletion, outError);
}

bool DX12CommandListPool::SubmitRecordedCommands(uint32_t frameSlot, std::span<const uint32_t> workerOrder,
                                                 RHICompletionPoint completion, std::string& outError)
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

    std::array<ID3D12CommandList*, kMaxWorkers> submission{};
    size_t submissionCount = 0;
    for (uint32_t worker : workerOrder)
    {
        if (worker >= m_workerCount)
        {
            continue;
        }
        const Slot& slot = m_slots[frameSlot][worker];
        if (!slot.opened)
        {
            continue;
        }
        ID3D12GraphicsCommandList* const list = slot.list.Get();
        if (nullptr != list)
        {
            submission[submissionCount++] = list;
        }
    }
    return m_resources->SubmitCommandLists(std::span(submission.data(), submissionCount), completion, outError);
}
