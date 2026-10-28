#pragma once
#include "../include/ce/all.h"
#include "../ordered-map/ordered_map.h"
#if defined(_MSC_VER)
#define CE_STORAGE_INLINE __forceinline
#else
#define CE_STORAGE_INLINE inline __attribute__((always_inline))
#endif
#include "../segmented_deque/segmented_deque.h"
namespace ce
{
    template<class T, class Container = segmented_deque<T>>
    class storage_queue
    {
        Container values_;
    public:
        using value_type = T;
        using container_type = Container;
        storage_queue() = default;
        explicit storage_queue(Container values) : values_(std::move(values)) {}
        bool empty() const noexcept { return values_.empty(); }
        size_t size() const noexcept { return values_.size(); }
        auto& front() { return values_.front(); }
        const auto& front() const { return values_.front(); }
        auto& back() { return values_.back(); }
        const auto& back() const { return values_.back(); }
        void push(const T& value) { values_.push_back(value); }
        void push(T&& value) { values_.push_back(std::move(value)); }
        template<class... Args> decltype(auto) emplace(Args&&... args) { return values_.emplace_back(std::forward<Args>(args)...); }
        void pop() { values_.pop_front(); }
        void swap(storage_queue& other) { values_.swap(other.values_); }
    };


}
#undef CE_STORAGE_INLINE
