#pragma once

#include "refs.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_IX86))
#    include <xmmintrin.h>
#endif

namespace gc
{
namespace detail
{
template <class T> struct is_trace_ref : std::false_type {};
template <class T> struct is_trace_ref<trace_ref<T>> : std::true_type {};

template <class T> struct is_optional : std::false_type {};
template <class T> struct is_optional<std::optional<T>> : std::true_type {};

template <class T> struct is_pair : std::false_type {};
template <class A, class B> struct is_pair<std::pair<A, B>> : std::true_type {};

template <class T> struct is_variant : std::false_type {};
template <class... Ts> struct is_variant<std::variant<Ts...>> : std::true_type {};

// Exclusively owned, non-GC sub-objects (pimpl and similar). shared_ptr is
// deliberately not traced: shared ownership would need its own convention.
template <class T> struct is_unique_ptr : std::false_type {};
template <class T, class D> struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};

template <class T> consteval bool traceable();

template <class... Ts>
consteval bool any_traceable(std::variant<Ts...>*)
{
    return (traceable<Ts>() || ...);
}

inline void prefetch(const void* p) noexcept
{
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0);
#else
    (void)p;
#endif
}

template <class T>
concept has_gc_trace = requires(const T& value, tracer& t) { value.gc_trace(t); };

// A type is traceable when it is a trace_ref, declares gc_trace, or is an
// optional, unique_ptr, variant, pair or range that contains a traceable type.
template <class T>
consteval bool traceable()
{
    using U = std::remove_cvref_t<T>;
    if constexpr (is_trace_ref<U>::value || has_gc_trace<U>)
        return true;
    else if constexpr (is_optional<U>::value)
        return traceable<typename U::value_type>();
    else if constexpr (is_unique_ptr<U>::value)
        return !std::is_array_v<typename U::element_type> && traceable<typename U::element_type>();
    else if constexpr (is_variant<U>::value)
        return any_traceable(static_cast<U*>(nullptr));
    else if constexpr (is_pair<U>::value)
        return traceable<typename U::first_type>() || traceable<typename U::second_type>();
    else if constexpr (std::ranges::range<U>)
        return traceable<std::ranges::range_value_t<U>>();
    else
        return false;
}
} // namespace detail

// Passed to gc_trace. A traced type lists every strong reference it holds,
// including references in base classes, nested values and containers, and
// whether or not they are serialized:
//
//     void gc_trace(gc::tracer& t) const
//     {
//         Base::gc_trace(t);
//         t.visit(m_owner);
//         t.visit(m_children);
//     }
//
// gc_trace runs inside a collection. It must not allocate, collect, change
// references or call into game code.
class tracer
{
public:
    tracer(const tracer&) = delete;
    tracer& operator=(const tracer&) = delete;

    template <class T>
    void visit(const T& value)
    {
        using U = std::remove_cvref_t<T>;
        static_assert(detail::traceable<U>(),
                      "gc::tracer::visit: the type holds no trace_ref. Visit trace_ref, types with gc_trace, "
                      "or optionals, unique_ptrs, variants, pairs and containers of them. "
                      "weak_ref is never traced.");

        if constexpr (detail::is_trace_ref<U>::value)
            mark(detail::ref_access::header(value));
        else if constexpr (detail::has_gc_trace<U>)
            value.gc_trace(*this);
        else if constexpr (detail::is_optional<U>::value || detail::is_unique_ptr<U>::value)
        {
            if (value)
                visit(*value);
        }
        else if constexpr (detail::is_variant<U>::value)
        {
            if (value.valueless_by_exception())
                return;
            std::visit(
                [&](const auto& alternative) {
                    if constexpr (detail::traceable<decltype(alternative)>())
                        visit(alternative);
                },
                value);
        }
        else if constexpr (detail::is_pair<U>::value)
        {
            if constexpr (detail::traceable<typename U::first_type>())
                visit(value.first);
            if constexpr (detail::traceable<typename U::second_type>())
                visit(value.second);
        }
        else
        {
            for (const auto& element : value)
                visit(element);
        }
    }

private:
    friend class domain;

    explicit tracer(domain& d) noexcept : domain_(d) {}
    // Diagnostic mode: records edges instead of marking.
    tracer(domain& d, std::vector<detail::object_header*>& edges) noexcept : domain_(d), collect_(&edges) {}

    // Edges are marked in small batches: each target header is prefetched
    // when it is found and marked when the batch is flushed, so the cache
    // misses of one object's edges overlap.
    void mark(detail::object_header* header)
    {
        if (!header)
            return;
        ++edges_;
        if (collect_)
        {
            collect_->push_back(header);
            return;
        }
        detail::prefetch(header);
        pending_[pending_count_++] = header;
        if (pending_count_ == pending_.size())
            flush();
    }

    void flush()
    {
        for (std::size_t i = 0; i < pending_count_; ++i)
            domain_.shade(pending_[i]);
        pending_count_ = 0;
    }

    domain& domain_;
    std::array<detail::object_header*, 16> pending_{};
    std::size_t pending_count_ = 0;
    std::vector<detail::object_header*>* collect_ = nullptr;
    std::size_t edges_ = 0;
};
} // namespace gc
