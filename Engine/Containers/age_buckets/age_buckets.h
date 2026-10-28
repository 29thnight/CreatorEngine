#pragma once
#include "../include/ce/dynamic_array.h"
namespace ce
{
    // Quantized monotonic deadlines. Only [next_tick, next_tick + Window) can
    // be scheduled. Advancing visits equal-deadline events in insertion order.
    template<class T, size_t Window, class Allocator = heap_allocator>
    class age_buckets
    {
        static_assert(Window > 0);
        dynamic_array<T, Allocator> buckets_[Window];
        uint64_t next_ = 0;
        size_t size_ = 0;
        bool exhausted_ = false;

      public:
        age_buckets() = default;

        explicit age_buckets(const Allocator& a)
        {
            for (auto& b : buckets_)
                b = dynamic_array<T, Allocator>(a);
        }

        age_buckets(const age_buckets&) = delete;
        age_buckets& operator=(const age_buckets&) = delete;

        template<class... A>
        bool emplace(uint64_t deadline, A&&... a)
        {
            if (exhausted_ || deadline < next_ || deadline - next_ >= Window)
                return false;
            buckets_[deadline % Window].emplace_back(static_cast<A&&>(a)...);
            ++size_;
            return true;
        }

        template<class F>
        void expire_through(uint64_t tick, F&& consume) noexcept
        {
            static_assert(noexcept(consume(*static_cast<T*>(nullptr), uint64_t{})),
                          "Expiration callbacks must not throw or mutate this table");
            if (exhausted_ || tick < next_)
                return;
            auto distance = tick - next_;
            auto count = distance >= Window - 1 ? Window : size_t(distance) + 1;
            for (size_t step = 0; step < count; ++step)
            {
                auto deadline = next_ + step;
                auto& bucket = buckets_[deadline % Window];
                for (auto& value : bucket)
                    consume(value, deadline);
                size_ -= bucket.size();
                bucket.clear();
            }
            exhausted_ = tick == UINT64_MAX;
            next_ = exhausted_ ? tick : tick + 1;
        }

        size_t size() const noexcept
        {
            return size_;
        }

        uint64_t next_tick() const noexcept
        {
            return next_;
        }

        void clear() noexcept
        {
            for (auto& b : buckets_)
                b.clear();
            size_ = 0;
        }
    };
} // namespace ce
