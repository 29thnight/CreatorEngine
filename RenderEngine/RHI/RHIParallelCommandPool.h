#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "RHIRecordedBatch.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

class RHIEncoder;

/// RenderGraph 병렬 command recording의 백엔드 중립 계약(G-3).
///
/// 그래프는 native command list/buffer나 queue를 보지 않는다. 풀은 워커별
/// command allocator/pool과 command target을 소유하고, pass마다 상태가 비어 있는
/// encoder를 제공한다. 기록 결과는 RHIRecordedBatch 값으로 밀봉하고, 별도 제출
/// 호출이 batch에 보존된 worker 순서를 그대로 queue 순서로 넘긴다(3-15A).
class IRHIParallelCommandPool
{
public:
    static constexpr uint32_t kMaxWorkers = 8;

    virtual ~IRHIParallelCommandPool() = default;

    virtual bool IsInitialized() const = 0;
    virtual uint32_t GetWorkerCount() const = 0;

    /// 이 frame slot의 GPU 사용이 끝난 뒤 호출한다.
    virtual void BeginFrame(uint32_t frameIndex) = 0;

    /// immediate command에 기록된 upload/copy를 worker 제출보다 먼저 queue에 넣고
    /// worker recording용 upload/descriptor version을 연다.
    virtual bool Prepare(std::string& outError) = 0;

    /// worker의 command target을 연다. Reset은 owner thread에서 끝낸다.
    virtual bool OpenWorker(uint32_t worker, std::string& outError) = 0;

    /// 열린 worker target에 새 pass 상태로 기록할 encoder를 돌려준다.
    virtual RHIEncoder& AcquireEncoder(uint32_t worker) = 0;

    /// 열린 target을 모두 닫는다. Vulkan은 열린 dynamic rendering도 여기서 닫는다.
    virtual bool CloseAll(std::string& outError) = 0;
    virtual bool HasRecorded(uint32_t worker) const = 0;

    /// 지속 worker thread에서 job을 병렬 실행하고 join 지점까지 기다린다.
    virtual void RunParallel(const std::function<void(uint32_t)>& job,
        uint32_t workerCount) = 0;

    /// 열린 command target을 닫고 workerOrder를 복사해 이동 전용 batch로 만든다.
    /// 이 메서드는 queue에 아무것도 제출하지 않는다.
    bool FinalizeRecordedBatch(std::span<const uint32_t> workerOrder,
        const RHIRecordedBatchDesc& desc, RHIRecordedBatch& outBatch,
        std::string& outError)
    {
        if (!IsInitialized())
        {
            outError = "기록 batch를 만들 command pool이 초기화되지 않았다";
            return false;
        }
        if (outBatch.IsValid())
        {
            outError = "출력 RHIRecordedBatch가 비어 있지 않다";
            return false;
        }

        std::array<bool, kMaxWorkers> seen{};
        for (const uint32_t worker : workerOrder)
        {
            if (worker >= GetWorkerCount() || worker >= kMaxWorkers)
            {
                outError = "기록 batch worker 번호가 범위를 벗어났다";
                return false;
            }
            if (seen[worker])
            {
                outError = "기록 batch에 같은 worker가 두 번 들어갔다";
                return false;
            }
            if (!HasRecorded(worker))
            {
                outError = "기록되지 않은 worker를 batch에 넣으려 했다";
                return false;
            }
            seen[worker] = true;
        }

        if (!CloseAll(outError)) return false;

        RHIRecordedBatch batch;
        batch.m_pool = this;
        batch.m_commandOrder.assign(workerOrder.begin(), workerOrder.end());
        batch.m_frameId = desc.frameId;
        batch.m_backendGeneration = desc.backendGeneration;
        batch.m_displayToken = desc.displayToken;
        batch.m_frameSlot = GetCurrentFrameSlot();
        batch.m_lifetimeToken = desc.lifetimeToken;
        batch.m_state = RHIRecordedBatchState::Recorded;
        outBatch = std::move(batch);
        return true;
    }

    /// producer thread에서 native queue 호출 없이 completion을 예약하고 upload /
    /// descriptor recording을 밀봉한다. 이 뒤 batch 자체를 bounded queue로 옮긴다.
    bool PrepareRecordedBatchSubmission(RHIRecordedBatch& batch,
        std::string& outError)
    {
        if (batch.m_pool != this || !batch.IsReadyForSubmit())
        {
            outError = "제출 준비할 RHIRecordedBatch의 소유/상태가 잘못됐다";
            return false;
        }
        if (batch.m_completion.IsValid())
        {
            outError = "RHIRecordedBatch completion point가 이미 예약됐다";
            return false;
        }

        RHICompletionPoint completion{};
        if (!PrepareRecordedCommands(batch.m_frameSlot, completion, outError))
            return false;
        if (!completion.IsValid())
        {
            outError = "backend가 유효한 batch completion을 예약하지 않았다";
            return false;
        }
        batch.m_completion = completion;
        return true;
    }

    /// PrepareRecordedBatchSubmission을 마친 batch만 RHI thread에서 호출한다.
    /// native queue submit/signal은 이 함수 아래에서만 일어난다.
    bool SubmitRecordedBatch(RHIRecordedBatch& batch, std::string& outError)
    {
        if (batch.m_pool != this)
        {
            outError = "RHIRecordedBatch의 소유 command pool이 다르다";
            return false;
        }
        if (!batch.IsReadyForSubmit())
        {
            outError = batch.IsSubmitted()
                ? "RHIRecordedBatch를 두 번 제출하려 했다"
                : "제출할 RHIRecordedBatch가 비어 있다";
            return false;
        }
        if (!batch.m_completion.IsValid())
        {
            outError = "completion을 예약하지 않은 RHIRecordedBatch를 제출하려 했다";
            return false;
        }

        if (!SubmitRecordedCommands(batch.m_frameSlot, batch.m_commandOrder,
            batch.m_completion, outError))
        {
            return false;
        }
        batch.m_state = RHIRecordedBatchState::Submitted;
        return true;
    }

protected:
    virtual uint32_t GetCurrentFrameSlot() const = 0;

    /// producer-side bookkeeping only. queue API를 호출하면 안 된다.
    virtual bool PrepareRecordedCommands(uint32_t frameSlot,
        RHICompletionPoint& outCompletion, std::string& outError) = 0;

    /// workerOrder 순서가 곧 graph 선언 순서다. frameSlot은 batch가 기록 당시
    /// 캡처했으므로 제출 스레드가 바뀌어도 mutable current slot을 다시 읽지 않는다.
    virtual bool SubmitRecordedCommands(uint32_t frameSlot,
        std::span<const uint32_t> workerOrder, RHICompletionPoint completion,
        std::string& outError) = 0;
};

#endif
