#pragma once

#include "domain.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace gc
{
template <class T> class root_ref;
template <class T> class trace_ref;
template <class T> class weak_ref;

namespace detail
{
struct ref_access
{
    template <class T> static object_header* header(const root_ref<T>& r) noexcept { return r.node_.header; }
    template <class T> static object_header* header(const trace_ref<T>& r) noexcept { return r.header_; }
    template <class T> static T* raw(const root_ref<T>& r) noexcept { return r.ptr_; }
    template <class T> static T* raw(const trace_ref<T>& r) noexcept { return r.ptr_; }

    template <class T> static root_ref<T> make_root(object_header* h, T* p) { return root_ref<T>(h, p); }
    // For gc::make: the object was just published on the owner thread, so
    // the store checks and the barrier have nothing to do.
    template <class T> static root_ref<T> adopt_new(object_header* h, T* p) noexcept
    {
        root_ref<T> r;
        r.node_.header = h;
        r.ptr_ = p;
        domain_access::link_new_root(&r.node_);
        return r;
    }
    template <class T> static trace_ref<T> make_trace(object_header* h, T* p) { return trace_ref<T>(h, p); }
    template <class T> static weak_ref<T> make_weak(object_header* h, T* p) noexcept
    {
        weak_ref<T> w;
        w.set(h, p);
        return w;
    }
};

template <class From, class To>
concept ref_convertible = std::convertible_to<From*, To*>;
} // namespace detail

// External root. Keeps its target and everything reachable from it alive.
// Owner thread only. Not for members of GC-managed objects: a root inside an
// object would keep the graph alive after the object becomes unreachable.
// Releasing the last root_ref never requests logical destruction.
template <class T>
class root_ref
{
public:
    using element_type = T;

    root_ref() noexcept = default;
    root_ref(std::nullptr_t) noexcept {}

    root_ref(const root_ref& other) { assign(other.node_.header, other.ptr_); }
    // The new root is linked before the source is released, so the target is
    // never unprotected during the move.
    root_ref(root_ref&& other) { assign(other.node_.header, other.ptr_); other.reset(); }

    template <class U>
        requires detail::ref_convertible<U, T>
    root_ref(const root_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    root_ref(root_ref<U>&& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
        other.reset();
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    root_ref(const trace_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
    }

    ~root_ref() { reset(); }

    root_ref& operator=(const root_ref& other)
    {
        if (this != &other)
            assign(other.node_.header, other.ptr_);
        return *this;
    }

    root_ref& operator=(root_ref&& other)
    {
        if (this != &other)
        {
            assign(other.node_.header, other.ptr_);
            other.reset();
        }
        return *this;
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    root_ref& operator=(const root_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
        return *this;
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    root_ref& operator=(const trace_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
        return *this;
    }

    root_ref& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    // Diagnostic name of the holder, reported by domain::for_each_root and
    // retention paths. The string must outlive the root. Not copied or moved
    // with the value.
    void set_label(const char* label) noexcept { node_.label = label; }
    [[nodiscard]] const char* label() const noexcept { return node_.label; }

    void reset() noexcept
    {
        if (node_.header)
        {
            detail::domain_access::unlink_root(&node_);
            node_.header = nullptr;
        }
        ptr_ = nullptr;
    }

    [[nodiscard]] T* get() const noexcept { return node_.header ? ptr_ : nullptr; }
    T& operator*() const noexcept { return *get(); }
    T* operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return node_.header != nullptr; }

    friend bool operator==(const root_ref& r, std::nullptr_t) noexcept { return !r; }

private:
    template <class> friend class root_ref;
    friend struct detail::ref_access;

    root_ref(detail::object_header* h, T* p) { assign(h, p); }

    void assign(detail::object_header* h, T* p)
    {
        if (h == nullptr)
        {
            reset();
            return;
        }
        detail::domain_access::on_store(h);
        if (node_.header && node_.header->owner == h->owner)
        {
            // Retarget in place; the node stays in the same root list.
            node_.header = h;
            ptr_ = p;
            return;
        }
        if (node_.header)
            detail::domain_access::unlink_root(&node_);
        node_.header = h;
        ptr_ = p;
        detail::domain_access::link_root(&node_);
    }

    detail::root_node node_;
    T* ptr_ = nullptr;
};

// Strong edge held by a GC-managed object. Followed while its owner is
// reachable. Every non-null store goes through the insertion barrier; do not
// bypass it with memcpy or raw overwrites. Destruction never touches the
// target, so destructors running during sweep may drop already reclaimed
// targets safely.
template <class T>
class trace_ref
{
public:
    using element_type = T;

    trace_ref() noexcept = default;
    trace_ref(std::nullptr_t) noexcept {}

    trace_ref(const trace_ref& other) { assign(other.header_, other.ptr_); }
    trace_ref(trace_ref&& other)
    {
        assign(other.header_, other.ptr_);
        other.reset();
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    trace_ref(const trace_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    trace_ref(const root_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
    }

    ~trace_ref() = default;

    trace_ref& operator=(const trace_ref& other)
    {
        assign(other.header_, other.ptr_);
        return *this;
    }

    trace_ref& operator=(trace_ref&& other)
    {
        if (this != &other)
        {
            assign(other.header_, other.ptr_);
            other.reset();
        }
        return *this;
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    trace_ref& operator=(const trace_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
        return *this;
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    trace_ref& operator=(const root_ref<U>& other)
    {
        assign(detail::ref_access::header(other), detail::ref_access::raw(other));
        return *this;
    }

    trace_ref& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    void reset() noexcept
    {
        header_ = nullptr;
        ptr_ = nullptr;
    }

    [[nodiscard]] T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return header_ != nullptr; }

    friend bool operator==(const trace_ref& r, std::nullptr_t) noexcept { return !r; }

private:
    template <class> friend class trace_ref;
    friend struct detail::ref_access;

    trace_ref(detail::object_header* h, T* p) { assign(h, p); }

    void assign(detail::object_header* h, T* p)
    {
        if (h)
            detail::domain_access::on_store(h);
        header_ = h;
        ptr_ = p;
    }

    detail::object_header* header_ = nullptr;
    T* ptr_ = nullptr;
};

// Non-owning reference. Promotion with lock() yields a root_ref, which means
// the memory is protected; whether the engine may still use the object is a
// separate lifecycle check. Promotion touches the root registry, so it is
// owner-thread only. Converting between weak_ref types (including adding const)
// promotes before adjusting the pointer and is also owner-thread only; expired
// sources yield empty references. Other threads may copy the same weak_ref type.
// The domain must outlive every weak_ref that is still promoted, cross-type
// converted, or expired-checked; debug checks report use after the domain is gone.
template <class T>
class weak_ref
{
public:
    using element_type = T;

    weak_ref() noexcept = default;
    weak_ref(std::nullptr_t) noexcept {}

    template <class U>
        requires detail::ref_convertible<U, T>
    weak_ref(const root_ref<U>& r) noexcept
    {
        set(detail::ref_access::header(r), detail::ref_access::raw(r));
    }

    template <class U>
        requires detail::ref_convertible<U, T>
    weak_ref(const trace_ref<U>& r) noexcept
    {
        set(detail::ref_access::header(r), detail::ref_access::raw(r));
    }

    template <class U>
        requires detail::ref_convertible<U, T> && (!std::same_as<U, T>)
    weak_ref(const weak_ref<U>& w)
    {
        // A virtual-base adjustment may read the object's vptr. Do not touch
        // the stored pointer until promotion protects the object's memory.
        auto r = w.lock();
        set(detail::ref_access::header(r), detail::ref_access::raw(r));
    }

    [[nodiscard]] root_ref<T> lock() const
    {
        if (!domain_)
            return {};
        detail::object_header* h = detail::domain_access::resolve_weak(domain_, domain_id_, slot_, generation_);
        if (!h)
            return {};
        return detail::ref_access::make_root<T>(h, ptr_);
    }

    // True when promotion would fail. Owner thread only.
    [[nodiscard]] bool expired() const
    {
        return !domain_ || detail::domain_access::resolve_weak(domain_, domain_id_, slot_, generation_) == nullptr;
    }

    void reset() noexcept { *this = weak_ref(); }

private:
    template <class> friend class weak_ref;
    friend struct detail::ref_access;

    void set(detail::object_header* h, T* p) noexcept
    {
        if (!h)
            return;
        domain_ = h->owner;
        domain_id_ = detail::domain_access::id(*h->owner);
        slot_ = h->slot;
        generation_ = h->generation;
        ptr_ = p;
    }

    domain* domain_ = nullptr;
    std::uint64_t domain_id_ = 0; // detects use after the domain is gone (debug checks)
    std::uint32_t slot_ = 0;
    std::uint32_t generation_ = 0;
    T* ptr_ = nullptr; // never dereferenced unless promotion succeeds
};
// ------------------------------------------------------------------ helpers

template <class R>
concept strong_ref = requires(const R& r) { detail::ref_access::header(r); };

// Identity comparison: two strong references are equal when they refer to
// the same GC object, whatever static type each one views it as.
template <strong_ref A, strong_ref B>
[[nodiscard]] bool operator==(const A& a, const B& b) noexcept
{
    return detail::ref_access::header(a) == detail::ref_access::header(b);
}

template <class U, class T>
[[nodiscard]] root_ref<U> static_ref_cast(const root_ref<T>& r)
{
    return detail::ref_access::make_root<U>(detail::ref_access::header(r), static_cast<U*>(r.get()));
}

template <class U, class T>
[[nodiscard]] trace_ref<U> static_ref_cast(const trace_ref<T>& r)
{
    return detail::ref_access::make_trace<U>(detail::ref_access::header(r), static_cast<U*>(r.get()));
}

// Returns an empty reference when the object is not a U.
template <class U, class T>
[[nodiscard]] root_ref<U> dynamic_ref_cast(const root_ref<T>& r)
{
    U* p = dynamic_cast<U*>(r.get());
    return p ? detail::ref_access::make_root<U>(detail::ref_access::header(r), p) : root_ref<U>{};
}

template <class U, class T>
[[nodiscard]] trace_ref<U> dynamic_ref_cast(const trace_ref<T>& r)
{
    U* p = dynamic_cast<U*>(r.get());
    return p ? detail::ref_access::make_trace<U>(detail::ref_access::header(r), p) : trace_ref<U>{};
}

} // namespace gc

// Hashes by object identity, consistent with operator==.
template <class T>
struct std::hash<gc::root_ref<T>>
{
    std::size_t operator()(const gc::root_ref<T>& r) const noexcept
    {
        return std::hash<const void*>{}(gc::detail::ref_access::header(r));
    }
};

template <class T>
struct std::hash<gc::trace_ref<T>>
{
    std::size_t operator()(const gc::trace_ref<T>& r) const noexcept
    {
        return std::hash<const void*>{}(gc::detail::ref_access::header(r));
    }
};
