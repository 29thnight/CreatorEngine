#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <functional>
#include <span>
#include <string>

class RHIEncoder;

/// RenderGraph 병렬 command recording의 백엔드 중립 계약(G-3).
///
/// 그래프는 native command list/buffer나 queue를 보지 않는다. 풀은 워커별
/// command allocator/pool과 command target을 소유하고, pass마다 상태가 비어 있는
/// encoder를 제공하며, worker 순서를 그대로 queue 제출 순서로 보존한다.
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

    /// workerOrder 순서가 곧 graph 선언 순서다. native queue/fence/timeline과
    /// recording completion 수명 처리는 backend가 맡는다.
    virtual bool Submit(std::span<const uint32_t> workerOrder,
        std::string& outError) = 0;
};

#endif
