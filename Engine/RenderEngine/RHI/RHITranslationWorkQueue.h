#pragma once
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

template<std::size_t Capacity>
class translation_work_queue final
{
    static_assert(Capacity <= UINT32_MAX);

  public:
    explicit translation_work_queue(std::span<const std::uint64_t> costs) : count_(costs.size())
    {
        if (count_ > Capacity)
        {
            throw std::invalid_argument("translation work exceeds the fixed target capacity");
        }
        for (std::size_t index = 0; index < count_; ++index)
        {
            auto position = index;
            // 큰 target을 먼저 시작하고, 비용이 같으면 원래 target 순서를 유지한다.
            while (position > 0 && costs[order_[position - 1]] < costs[index])
            {
                order_[position] = order_[position - 1];
                --position;
            }
            order_[position] = static_cast<std::uint32_t>(index);
        }
    }

    std::size_t claim() noexcept
    {
        // 정렬 결과는 제출 전에 완성된다. 이 원자는 target의 독점 소유만 배분한다.
        const auto position = next_.fetch_add(1, std::memory_order_relaxed);
        return position < count_ ? order_[position] : count_;
    }

  private:
    std::array<std::uint32_t, Capacity> order_{};
    const std::size_t count_{};
    // 공유 claim 갱신이 읽기 전용 순서를 무효화하지 않도록 캐시 라인을 분리한다.
    alignas(64) std::atomic<std::size_t> next_{};
};
