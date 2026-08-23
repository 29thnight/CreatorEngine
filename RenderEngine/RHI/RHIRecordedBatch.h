#pragma once
#include "RHIResourceTypes.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

class IRHIParallelCommandPool;
class RHISubmissionThread;

/// 기록이 끝났을 때 호출부가 붙이는 backend 중립 식별 정보.
/// completion은 제출과 signal 뒤에 정해지므로 RHIRecordedBatch에 별도로 채운다.
struct RHIRecordedBatchDesc
{
    uint64_t frameId{ 0 };
    uint64_t backendGeneration{ 0 };
    uint64_t displayToken{ 0 };

    // 그래프 transient처럼 GPU completion까지 붙잡아야 하는 소유자를 한 덩어리로
    // 넘기는 자리다. 3-15B의 retire queue가 batch와 함께 보관한다.
    std::shared_ptr<const void> lifetimeToken;
};

enum class RHIRecordedBatchState : uint8_t
{
    Empty,
    Recorded,
    Submitted
};

/// RenderThread의 기록 결과. native command list/buffer는 pool 내부에만 있고,
/// 이 값은 제출 순서와 그 command target이 속한 frame slot만 불투명하게 가리킨다.
///
/// 복사를 막는 이유는 같은 native command를 두 번 제출하는 실수를 값 복사만으로
/// 만들 수 없게 하기 위해서다. 이동은 3-15B bounded queue handoff에 사용한다.
class RHIRecordedBatch final
{
public:
    static constexpr uint32_t kInvalidFrameSlot =
        (std::numeric_limits<uint32_t>::max)();

    RHIRecordedBatch() = default;
    ~RHIRecordedBatch() = default;
    RHIRecordedBatch(const RHIRecordedBatch&) = delete;
    RHIRecordedBatch& operator=(const RHIRecordedBatch&) = delete;

    RHIRecordedBatch(RHIRecordedBatch&& other) noexcept
    {
        MoveFrom(std::move(other));
    }

    RHIRecordedBatch& operator=(RHIRecordedBatch&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    bool IsValid() const { return RHIRecordedBatchState::Empty != m_state; }
    bool IsReadyForSubmit() const { return RHIRecordedBatchState::Recorded == m_state; }
    bool IsSubmitted() const { return RHIRecordedBatchState::Submitted == m_state; }
    RHIRecordedBatchState GetState() const { return m_state; }

    uint64_t GetFrameId() const { return m_frameId; }
    uint64_t GetBackendGeneration() const { return m_backendGeneration; }
    uint64_t GetDisplayToken() const { return m_displayToken; }
    uint32_t GetFrameSlot() const { return m_frameSlot; }
    uint32_t GetCommandCount() const
    {
        return static_cast<uint32_t>(m_commandOrder.size());
    }
    RHICompletionPoint GetCompletionPoint() const { return m_completion; }
    bool HasLifetimeToken() const { return nullptr != m_lifetimeToken; }
    std::shared_ptr<const void> GetLifetimeToken() const { return m_lifetimeToken; }

private:
    friend class IRHIParallelCommandPool;
    friend class RHISubmissionThread;

    void Reset()
    {
        m_pool = nullptr;
        m_commandOrder.clear();
        m_frameId = 0;
        m_backendGeneration = 0;
        m_displayToken = 0;
        m_frameSlot = kInvalidFrameSlot;
        m_completion = {};
        m_lifetimeToken.reset();
        m_state = RHIRecordedBatchState::Empty;
    }

    void MoveFrom(RHIRecordedBatch&& other)
    {
        m_pool = other.m_pool;
        m_commandOrder = std::move(other.m_commandOrder);
        m_frameId = other.m_frameId;
        m_backendGeneration = other.m_backendGeneration;
        m_displayToken = other.m_displayToken;
        m_frameSlot = other.m_frameSlot;
        m_completion = other.m_completion;
        m_lifetimeToken = std::move(other.m_lifetimeToken);
        m_state = other.m_state;
        other.Reset();
    }

    IRHIParallelCommandPool* m_pool{ nullptr };
    std::vector<uint32_t> m_commandOrder;
    uint64_t m_frameId{ 0 };
    uint64_t m_backendGeneration{ 0 };
    uint64_t m_displayToken{ 0 };
    uint32_t m_frameSlot{ kInvalidFrameSlot };
    RHICompletionPoint m_completion{};
    std::shared_ptr<const void> m_lifetimeToken;
    RHIRecordedBatchState m_state{ RHIRecordedBatchState::Empty };
};

