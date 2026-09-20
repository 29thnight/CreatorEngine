#pragma once
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

#include "../RHIParallelCommandPool.h"

class DX12DeviceResources;
class DX12Encoder;

// [프레임][기록 구간]별 command allocator/list 소유자. 실행 스레드는 공용
// job_scheduler가 소유한다. 구간 번호는 물리 워커 번호와 독립적이다.
// owner는 GPU frame fence 완료 후 Reset하고, RunParallel 완료 뒤 Close/제출한다.
// 각 구간에 연속 패스를 배정하고 구간 순서로 제출하여 그래프 순서를 보존한다.
class DX12CommandListPool : public IRHIParallelCommandPool
{
public:
    static constexpr uint32_t kMaxWorkers = IRHIParallelCommandPool::kMaxWorkers;

    explicit DX12CommandListPool(job_scheduler& scheduler = ce::get_job_scheduler());
    ~DX12CommandListPool() override;

    bool Initialize(DX12DeviceResources& resources, uint32_t workerCount, uint32_t frameCount,
        std::string& outError);
    void Shutdown();

    bool IsInitialized() const override { return !m_slots.empty(); }

    uint32_t GetWorkerCount() const override { return m_workerCount; }

    /// 프레임 구간을 바꾼다. 이 시점에는 그 구간의 GPU 작업이 끝나 있어야 한다
    /// (DX12DeviceResources::BeginFrame의 펜스 대기가 그것을 보장한다).
    void BeginFrame(uint32_t frameIndex) override;

    bool Prepare(std::string& outError) override;
    bool OpenWorker(uint32_t worker, std::string& outError) override;
    RHIEncoder& AcquireEncoder(uint32_t worker) override;

    /// 워커의 리스트를 열어 돌려준다. 이미 열려 있으면 그대로 준다.
    /// 실패하면 nullptr — 호출부는 그 패스를 건너뛰지 말고 알려야 한다.
    ID3D12GraphicsCommandList* Open(uint32_t worker, std::string& outError);

    /// 이번 프레임에 연 리스트를 전부 닫는다. 닫힌 순서가 아니라 워커 번호
    /// 순서로 돌려주지 않는다 — 제출 순서는 호출부(그래프)가 정한다.
    bool CloseAll(std::string& outError) override;
    uint32_t DrainEncoderDrops(std::string& outLast) override;

    /// 워커가 이번 프레임에 실제로 기록했는가.
    bool HasRecorded(uint32_t worker) const override;

    ID3D12GraphicsCommandList* Get(uint32_t worker) const;


private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Slot
    {
        ComPtr<ID3D12CommandAllocator>    allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        bool opened{ false };
    };

    // [프레임][기록 구간]. OpenWorker에서 해당 allocator/list를 Reset한다.
    std::vector<std::vector<Slot>> m_slots;

    uint32_t GetCurrentFrameSlot() const override { return m_frameIndex; }
    bool PrepareRecordedCommands(uint32_t frameSlot,
        RHICompletionPoint& outCompletion, std::string& outError) override;
    bool SubmitRecordedCommands(uint32_t frameSlot,
        std::span<const uint32_t> workerOrder, RHICompletionPoint completion,
        std::string& outError) override;

    ComPtr<ID3D12Device> m_device;
    DX12DeviceResources* m_resources{ nullptr };
    std::vector<std::unique_ptr<DX12Encoder>> m_encoders;
    uint32_t m_workerCount{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };
};

