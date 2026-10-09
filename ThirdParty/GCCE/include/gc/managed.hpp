#pragma once

#include "config.hpp"
#include "detail/header.hpp"
#include "refs.hpp"

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

namespace gc
{
class managed;

namespace detail
{
[[noreturn]] GC_API void managed_delete_called() noexcept;
// Debug check: reports a gc::managed subobject constructed outside gc::make.
GC_API void on_managed_constructed(const void* self) noexcept;
struct managed_access;
} // namespace detail

// Base of every GC-managed type. Derive from it exactly once.
//
// Creation and destruction: GC objects are created only by gc::make and
// destroyed only by the collector, so the class-scope allocation functions
// shut out every other path at compile time:
//
//     new T, new T[n], new (std::nothrow) T, new (p) T   -> error
//     delete p, std::unique_ptr<T>, std::make_unique<T>  -> error
//
// operator delete cannot be deleted or private: a virtual destructor looks
// it up, so polymorphic GC types would stop compiling. It is protected and
// aborts if reached, which only `delete this` inside the class can do.
// A deliberately global ::new bypasses class scope and is not prevented.
//
// Identity: every object knows its GC identity, so its member functions can
// get references to the object itself. The reference type follows the type
// the call is made through (deducing this), so a base-class member function
// of a multiply inherited object gets a correctly adjusted base reference:
//
//     void widget::attach(panel& p) { p.children.push_back(root_from_this()); }
//
// The identity is set by gc::make after the constructor returns and is never
// copied. Inside the constructor, for copies, and for instances not created
// by gc::make, the result is an empty reference.
class managed
{
public:
    template <class Self>
    [[nodiscard]] root_ref<std::remove_reference_t<Self>> root_from_this(this Self&& self)
    {
        using T = std::remove_reference_t<Self>;
        detail::object_header* h = header_of(self);
        return detail::ref_access::make_root<T>(h, h ? std::addressof(self) : nullptr);
    }

    template <class Self>
    [[nodiscard]] weak_ref<std::remove_reference_t<Self>> weak_from_this(this Self&& self) noexcept
    {
        using T = std::remove_reference_t<Self>;
        return detail::ref_access::make_weak<T>(header_of(self), std::addressof(self));
    }

    static void* operator new(std::size_t) = delete;
    static void* operator new[](std::size_t) = delete;
    static void* operator new(std::size_t, std::align_val_t) = delete;
    static void* operator new[](std::size_t, std::align_val_t) = delete;

protected:
    managed() noexcept { constructed(); }
    managed(const managed&) noexcept { constructed(); }
    managed& operator=(const managed&) noexcept { return *this; }
    ~managed() = default;

    static void operator delete(void*) noexcept { detail::managed_delete_called(); }
    static void operator delete[](void*) noexcept { detail::managed_delete_called(); }
    static void operator delete(void*, std::align_val_t) noexcept { detail::managed_delete_called(); }
    static void operator delete[](void*, std::align_val_t) noexcept { detail::managed_delete_called(); }

private:
    friend struct detail::managed_access;

    static detail::object_header* header_of(const managed& m) noexcept { return m.gc_header_; }

    void constructed() const noexcept
    {
#if GC_DEBUG_CHECKS
        detail::on_managed_constructed(this);
#endif
    }

    detail::object_header* gc_header_ = nullptr;
};

// A type the collector can manage: derives from gc::managed exactly once.
template <class T>
concept managed_type = std::is_base_of_v<managed, std::remove_cv_t<T>> &&
                       requires(T* p) { static_cast<const managed*>(p); };

namespace detail
{
struct managed_access
{
    static object_header* header(const managed& m) noexcept { return m.gc_header_; }
    static void set_header(managed& m, object_header* h) noexcept { m.gc_header_ = h; }
};
} // namespace detail
} // namespace gc
