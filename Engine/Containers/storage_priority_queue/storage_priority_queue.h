#pragma once
#include "../include/ce/all.h"
#include "../ordered-map/ordered_map.h"
#if defined(_MSC_VER)
#define CE_STORAGE_INLINE __forceinline
#else
#define CE_STORAGE_INLINE inline __attribute__((always_inline))
#endif
namespace ce
{
    template<class T, class Container = dynamic_array<T>, class Compare = std::less<T>>
    class storage_priority_queue
    {
        Container values_;
        [[no_unique_address]] Compare compare_;
    public:
        storage_priority_queue() = default;
        explicit storage_priority_queue(Compare compare, Container values = Container{})
            : values_(std::move(values)), compare_(std::move(compare)) { std::make_heap(values_.begin(), values_.end(), compare_); }
        bool empty() const noexcept { return values_.empty(); }
        size_t size() const noexcept { return values_.size(); }
        const T& top() const { return values_.front(); }
        void push(const T& value) { values_.push_back(value); std::push_heap(values_.begin(), values_.end(), compare_); }
        void push(T&& value) { values_.push_back(std::move(value)); std::push_heap(values_.begin(), values_.end(), compare_); }
        template<class... Args> void emplace(Args&&... args)
        { values_.emplace_back(std::forward<Args>(args)...); std::push_heap(values_.begin(), values_.end(), compare_); }
        void pop() { std::pop_heap(values_.begin(), values_.end(), compare_); values_.pop_back(); }
    };
}
#undef CE_STORAGE_INLINE
