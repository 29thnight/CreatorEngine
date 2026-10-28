#pragma once
#include "../include/ce/memory.h"
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <typeinfo>
#include <exception>
#include <concepts>
#include <cstddef>
#include <cstring>
#if defined(_MSC_VER)
#define CE_FUNC_INLINE __forceinline
#else
#define CE_FUNC_INLINE inline __attribute__((always_inline))
#endif
namespace ce
{
    class bad_func_call final : public std::bad_function_call
    {
    public:
        const char* what() const noexcept override { return "empty func invocation"; }
    };

    namespace func_detail
    {
#if defined(_MSC_VER)
        [[noreturn]] __declspec(noinline) inline void throw_empty_func() { throw bad_func_call{}; }
#else
        [[noreturn]] __attribute__((noinline)) inline void throw_empty_func() { throw bad_func_call{}; }
#endif
        template<class R, bool Noexcept, class F, class... Args>
        inline constexpr bool compatible_target = [] {
            if constexpr (!std::is_invocable_r_v<R, F&, Args...>) return false;
            else if constexpr (std::is_reference_v<R>)
            {
                return !std::reference_converts_from_temporary_v<R, std::invoke_result_t<F&, Args...>> &&
                    (!Noexcept || std::is_nothrow_invocable_r_v<R, F&, Args...>);
            }
            else return !Noexcept || std::is_nothrow_invocable_r_v<R, F&, Args...>;
        }();

// The header travels with its target, so a heap move transfers one
        // pointer. Copyable and unique owners share the same erased model.
        template<class R, bool Noexcept, bool InlineOnly, size_t Bytes, size_t Align, class Allocator, class... Args>
        struct operations
        {
            struct header { const operations* ops; };
            template<class T> using call_argument = std::conditional_t<(Noexcept || InlineOnly) && std::is_scalar_v<T>, T, T&&>;
            using invoke_pointer = R (*)(header*, call_argument<Args>...) noexcept(Noexcept);
            static R empty_invoke(header*, call_argument<Args>...) noexcept { std::terminate(); }
            invoke_pointer invoke;
            void (*destroy)(header*, Allocator&) noexcept;
            header* (*move)(void*, header*) noexcept;
            header* (*copy)(void*, const header*, Allocator&);
            const std::type_info& (*type)() noexcept;

            static constexpr size_t storage_alignment = Align < alignof(header) ? alignof(header) : Align;
            static constexpr size_t storage_bytes =
                ((Bytes + storage_alignment + storage_alignment - 1) / storage_alignment) * storage_alignment;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // Header padding preserves the target's requested alignment.
#endif
            template<class F> struct model : header
            {
                F function;
                template<class Source>
                explicit model(Source&& source) noexcept(std::is_nothrow_constructible_v<F, Source>)
                    : header{&table<F>()}, function(std::forward<Source>(source)) {}
            };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            template<class F>
            static constexpr bool local = sizeof(F) <= Bytes && alignof(F) <= Align &&
                std::is_nothrow_move_constructible_v<F>;

            template<class F> static F* target(header* value) noexcept
            { return std::addressof(static_cast<model<F>*>(value)->function); }
            template<class F> static const F* target(const header* value) noexcept
            { return std::addressof(static_cast<const model<F>*>(value)->function); }

            template<class F, class Source>
            static header* construct(void* storage, Allocator& allocator, Source&& source)
            {
                if constexpr (local<F>)
                {
                    static_assert(sizeof(model<F>) <= storage_bytes);
                    return ::new (storage) model<F>(std::forward<Source>(source));
                }
                else
                {
                    auto memory = allocator.allocate(sizeof(model<F>), alignof(model<F>));
                    if constexpr (std::is_nothrow_constructible_v<F, Source>)
                        return ::new (memory) model<F>(std::forward<Source>(source));
                    else try { return ::new (memory) model<F>(std::forward<Source>(source)); }
                    catch (...) { allocator.deallocate(memory, sizeof(model<F>), alignof(model<F>)); throw; }
                }
            }

            template<class F> static void destroy_target(header* value, Allocator& allocator) noexcept
            {
                auto pointer = static_cast<model<F>*>(value);
                std::destroy_at(pointer);
                if constexpr (!local<F>) allocator.deallocate(pointer, sizeof(model<F>), alignof(model<F>));
            }
            template<class F> static header* move_target(void* destination, header* source) noexcept
            {
                if constexpr (local<F>)
                {
                    auto pointer = static_cast<model<F>*>(source);
                    auto result = ::new (destination) model<F>(std::move(pointer->function));
                    std::destroy_at(pointer);
                    return result;
                }
                else return source;
            }
            template<class F> static header* relocate_trivial(void* destination, header* source) noexcept
            {
                static_assert(std::is_trivially_copyable_v<model<F>>);
                std::memcpy(destination, static_cast<model<F>*>(source), sizeof(model<F>));
                return std::launder(static_cast<model<F>*>(destination));
            }
            template<class F> static constexpr auto move_policy() noexcept -> header* (*)(void*, header*) noexcept
            {
                if constexpr (local<F> && std::is_trivially_copyable_v<model<F>>)
                {
                    if constexpr (sizeof(model<F>) <= 2 * sizeof(void*)) return nullptr;
                    else return &relocate_trivial<F>;
                }
                else return &move_target<F>;
            }
            template<class F> static header* copy_target(void* destination, const header* source, Allocator& allocator)
            {
                if constexpr (std::is_copy_constructible_v<F>) return construct<F>(destination, allocator, *target<F>(source));
                else std::terminate();
            }
            template<class F> static const operations& table() noexcept
            {
                static constexpr operations value = {
                    [](header* source, call_argument<Args>... args) noexcept(Noexcept) -> R {
                        return std::invoke_r<R>(*target<F>(source), std::forward<Args>(args)...);
                    },
                    local<F> && std::is_trivially_destructible_v<model<F>> ? nullptr : &destroy_target<F>,
                    move_policy<F>(),
                    std::is_copy_constructible_v<F> ? &copy_target<F> : nullptr,
                    []() noexcept -> const std::type_info& { return typeid(F); }
                };
                return value;
            }
        };

        template<class Pointer, bool Enabled> struct invoke_cache
        {
            constexpr invoke_cache(Pointer) noexcept {}
            void set(Pointer) noexcept {}
        };
        template<class Pointer> struct invoke_cache<Pointer, true>
        {
            Pointer pointer;
            constexpr invoke_cache(Pointer value) noexcept : pointer(value) {}
            void set(Pointer value) noexcept { pointer = value; }
        };

        template<class R, bool Noexcept, bool Copyable, bool InlineOnly,
                 size_t Bytes, size_t Align, class Allocator, class... Args>
        class owning_func
        {
        public:
            using erasure_type = operations<R, Noexcept, InlineOnly, Bytes, Align, Allocator, Args...>;
            static constexpr bool inline_only = InlineOnly;

        private:
            template<class, bool, bool, bool, size_t, size_t, class, class...> friend class owning_func;
            static_assert(Bytes > 0 && Align > 0 && (Align & (Align - 1)) == 0);
            static_assert(InlineOnly || Bytes >= sizeof(void*));
            static_assert(std::is_nothrow_copy_constructible_v<Allocator> &&
                          std::is_nothrow_move_constructible_v<Allocator> &&
                          std::is_nothrow_move_assignable_v<Allocator> &&
                          std::is_nothrow_destructible_v<Allocator>);
            using ops_type = erasure_type;
#if defined(_MSC_VER) && !defined(__clang__)
            [[msvc::no_unique_address]]
#else
            [[no_unique_address]]
#endif
            Allocator allocator_;
            alignas(ops_type::storage_alignment) mutable unsigned char storage_[ops_type::storage_bytes];
            typename ops_type::header* value_ = nullptr;
#if defined(_MSC_VER) && !defined(__clang__)
            [[msvc::no_unique_address]]
#else
            [[no_unique_address]]
#endif
            invoke_cache<typename ops_type::invoke_pointer, Noexcept> invoke_cache_{&ops_type::empty_invoke};

            CE_FUNC_INLINE void refresh_cache() noexcept
            {
                if constexpr (Noexcept)
                    invoke_cache_.set(value_ ? value_->ops->invoke : &ops_type::empty_invoke);
            }

            template<class F> static consteval bool same_owner()
            {
                using target_type = std::decay_t<F>;
                if constexpr (requires { typename target_type::erasure_type; target_type::inline_only; })
                    return std::is_same_v<typename target_type::erasure_type, ops_type> &&
                        target_type::inline_only == InlineOnly;
                else return false;
            }

            template<class F> static consteval bool accepts_target()
            {
                using target_type = std::decay_t<F>;
                if constexpr (same_owner<F>()) return false;
                else return compatible_target<R, Noexcept, target_type, Args...> &&
                    std::is_constructible_v<target_type, F> &&
                    std::is_nothrow_destructible_v<target_type> &&
                    (!Copyable || std::is_copy_constructible_v<target_type>) &&
                    (!InlineOnly || ops_type::template local<target_type>);
            }
            template<class F> static constexpr bool accepts = accepts_target<F>();

            template<class F> void create(F&& function)
            {
                using target_type = std::decay_t<F>;
                if constexpr (std::is_pointer_v<target_type> || std::is_member_pointer_v<target_type>)
                    if (function == nullptr) return;
                value_ = ops_type::template construct<target_type>(storage_, allocator_, std::forward<F>(function));
                refresh_cache();
            }

            template<bool OtherCopyable> CE_FUNC_INLINE typename ops_type::header* take(
                owning_func<R, Noexcept, OtherCopyable, InlineOnly, Bytes, Align, Allocator, Args...>& other) noexcept
            {
                auto* source = std::exchange(other.value_, nullptr);
                other.refresh_cache();
                if (source)
                {
                    const auto offset = reinterpret_cast<std::uintptr_t>(source) -
                        reinterpret_cast<std::uintptr_t>(other.storage_);
                    if (InlineOnly || offset < ops_type::storage_bytes)
                    {
                        if (source->ops->move) return source->ops->move(storage_, source);
                        else
                        {
                            // A trivial model is an implicit-lifetime type;
                            // memcpy starts its lifetime in the destination.
                            std::memcpy(storage_, other.storage_, 2 * sizeof(void*));
                            return std::launder(reinterpret_cast<typename ops_type::header*>(storage_ + offset));
                        }
                    }
                    else return source;
                }
                return nullptr;
            }

        public:
            using result_type = R;
            using allocator_type = Allocator;
            static constexpr size_t inline_capacity = Bytes;
            static constexpr size_t inline_alignment = Align;

            owning_func() noexcept(std::is_nothrow_default_constructible_v<Allocator>) : allocator_() {}
            owning_func(std::nullptr_t) noexcept(std::is_nothrow_default_constructible_v<Allocator>) : allocator_() {}
            explicit owning_func(const Allocator& allocator) noexcept : allocator_(allocator) {}

            template<class F> requires accepts<F&&>
            owning_func(F&& function) : allocator_() { create(std::forward<F>(function)); }

            template<class F> requires accepts<F&&>
            owning_func(std::allocator_arg_t, const Allocator& allocator, F&& function) : allocator_(allocator)
            { create(std::forward<F>(function)); }

            owning_func(const owning_func& other) requires Copyable : allocator_(other.allocator_)
            {
                if (other.value_) value_ = other.value_->ops->copy(storage_, other.value_, allocator_);
                refresh_cache();
            }
            owning_func(const owning_func&) requires (!Copyable) = delete;

            CE_FUNC_INLINE owning_func(owning_func&& other) noexcept : allocator_(std::move(other.allocator_)), value_(take(other)) { refresh_cache(); }

            // Copy/transfer the erased target itself, rather than nesting a
            // complete owned_func object and accidentally allocating for it.
            template<bool OtherCopyable> requires (!Copyable && OtherCopyable)
            owning_func(const owning_func<R, Noexcept, OtherCopyable, InlineOnly, Bytes, Align, Allocator, Args...>& other)
                : allocator_(other.allocator_)
            {
                if (other.value_) value_ = other.value_->ops->copy(storage_, other.value_, allocator_);
                refresh_cache();
            }
            template<bool OtherCopyable> requires (!Copyable && OtherCopyable)
            owning_func(owning_func<R, Noexcept, OtherCopyable, InlineOnly, Bytes, Align, Allocator, Args...>&& other) noexcept
                : allocator_(std::move(other.allocator_)), value_(take(other)) { refresh_cache(); }

            ~owning_func() { reset(); }

            owning_func& operator=(const owning_func& other) requires Copyable
            {
                if (this != &other) { owning_func copy(other); *this = std::move(copy); }
                return *this;
            }
            owning_func& operator=(const owning_func&) requires (!Copyable) = delete;

            CE_FUNC_INLINE owning_func& operator=(owning_func&& other) noexcept
            {
                if (this == &other) return *this;
                const auto local = [](const owning_func& owner) noexcept {
                    return reinterpret_cast<std::uintptr_t>(owner.value_) -
                        reinterpret_cast<std::uintptr_t>(owner.storage_) < ops_type::storage_bytes;
                };
                if constexpr (!InlineOnly) if (!local(*this) && !local(other))
                {
                    auto old_allocator = allocator_;
                    auto* old = value_;
                    value_ = std::exchange(other.value_, nullptr);
                    allocator_ = std::move(other.allocator_);
                    refresh_cache(); other.refresh_cache();
                    if (old && old->ops->destroy) old->ops->destroy(old, old_allocator);
                    return *this;
                }
                if (!value_)
                {
                    allocator_ = std::move(other.allocator_); value_ = take(other);
                }
                else
                {
                    owning_func incoming(std::move(other));
                    owning_func old(std::move(*this));
                    allocator_ = std::move(incoming.allocator_); value_ = take(incoming);
                    refresh_cache();
                }
                refresh_cache();
                return *this;
            }
            owning_func& operator=(std::nullptr_t) noexcept { reset(); return *this; }

            template<class F> requires accepts<F&&>
            owning_func& operator=(F&& function)
            {
                owning_func replacement(std::allocator_arg, allocator_, std::forward<F>(function));
                *this = std::move(replacement);
                return *this;
            }

            CE_FUNC_INLINE void reset() noexcept
            {
                if (value_)
                {
                    // Empty first: a target destructor may observe its owner.
                    auto* old = std::exchange(value_, nullptr);
                    refresh_cache();
                    if (old->ops->destroy) old->ops->destroy(old, allocator_);
                }
            }
            void swap(owning_func& other) noexcept
            {
                if (this == &other) return;
                owning_func temporary(std::move(*this));
                allocator_ = std::move(other.allocator_); value_ = take(other);
                other.allocator_ = std::move(temporary.allocator_); other.value_ = other.take(temporary);
                refresh_cache(); other.refresh_cache();
            }
            friend void swap(owning_func& a, owning_func& b) noexcept { a.swap(b); }
            explicit operator bool() const noexcept { return value_ != nullptr; }
            friend bool operator==(const owning_func& function, std::nullptr_t) noexcept { return !function; }
            Allocator get_allocator() const noexcept { return allocator_; }
            const std::type_info& target_type() const noexcept { return value_ ? value_->ops->type() : typeid(void); }

            template<class F> F* target() noexcept
            {
                if constexpr (!std::is_object_v<F>) return nullptr;
                else
                {
                    if (!value_ || value_->ops->type() != typeid(F)) return nullptr;
                    return ops_type::template target<std::remove_cv_t<F>>(value_);
                }
            }
            template<class F> const F* target() const noexcept
            {
                if constexpr (!std::is_object_v<F>) return nullptr;
                else
                {
                    if (!value_ || value_->ops->type() != typeid(F)) return nullptr;
                    return ops_type::template target<std::remove_cv_t<F>>(static_cast<const typename ops_type::header*>(value_));
                }
            }

            CE_FUNC_INLINE R operator()(Args... args) const noexcept(Noexcept)
            {
                if constexpr (Noexcept) return invoke_cache_.pointer(value_, std::forward<Args>(args)...);
                else
                {
                    if (!value_) throw_empty_func();
                    return value_->ops->invoke(value_, std::forward<Args>(args)...);
                }
            }
        };

        template<class R, bool Noexcept, class... Args>
        class borrowed_func
        {
            union payload
            {
                const void* object;
                void (*function)();
                payload() noexcept : object(nullptr) {}
            } payload_;
            using invoke_pointer = R (*)(payload, Args&&...) noexcept(Noexcept);
            invoke_pointer invoke_;

        public:
            borrowed_func() = delete;
            template<class F>
                requires (std::is_pointer_v<F> && std::is_function_v<std::remove_pointer_t<F>> &&
                          compatible_target<R, Noexcept, F, Args...>)
            borrowed_func(F function)
            {
                if (!function) throw bad_func_call{};
                payload_.function = reinterpret_cast<void (*)()>(function);
                invoke_ = [](payload value, Args&&... args) noexcept(Noexcept) -> R {
                    return std::invoke_r<R>(reinterpret_cast<F>(value.function), std::forward<Args>(args)...);
                };
            }

            template<class F>
                requires (!std::is_same_v<std::remove_cv_t<F>, borrowed_func> && !std::is_function_v<F> &&
                          !std::is_pointer_v<F> && compatible_target<R, Noexcept, F, Args...>)
            borrowed_func(F& function) noexcept
            {
                payload_.object = std::addressof(function);
                invoke_ = [](payload value, Args&&... args) noexcept(Noexcept) -> R {
                    return std::invoke_r<R>(*static_cast<F*>(const_cast<void*>(value.object)), std::forward<Args>(args)...);
                };
            }

            template<class F>
                requires (!std::is_lvalue_reference_v<F> &&
                          !(std::is_pointer_v<std::decay_t<F>> && std::is_function_v<std::remove_pointer_t<std::decay_t<F>>>))
            borrowed_func(F&&) = delete;

            CE_FUNC_INLINE R operator()(Args... args) const noexcept(Noexcept)
            {
                 return invoke_(payload_, std::forward<Args>(args)...); }
        };

        template<class Signature> struct signature;
        template<class R, class... Args> struct signature<R(Args...)>
        {
            template<bool Copyable, bool InlineOnly, size_t Bytes, size_t Align, class Allocator>
            using owner = owning_func<R, false, Copyable, InlineOnly, Bytes, Align, Allocator, Args...>;
            using reference = borrowed_func<R, false, Args...>;
        };
        template<class R, class... Args> struct signature<R(Args...) noexcept>
        {
            template<bool Copyable, bool InlineOnly, size_t Bytes, size_t Align, class Allocator>
            using owner = owning_func<R, true, Copyable, InlineOnly, Bytes, Align, Allocator, Args...>;
            using reference = borrowed_func<R, true, Args...>;
        };
    }

    template<class Signature, class Allocator = heap_allocator>
    using owned_func = typename func_detail::signature<Signature>::template owner<true, false, 48, alignof(void*), Allocator>;
    template<class Signature, class Allocator = heap_allocator>
    using unique_func = typename func_detail::signature<Signature>::template owner<false, false, 48, alignof(void*), Allocator>;
    template<class Signature>
    using func_ref = typename func_detail::signature<Signature>::reference;
    template<class Signature, size_t Capacity, size_t Alignment = alignof(void*)>
    using inline_owned_func = typename func_detail::signature<Signature>::template owner<true, true, Capacity, Alignment, heap_allocator>;
    template<class Signature, size_t Capacity, size_t Alignment = alignof(void*)>
    using inline_unique_func = typename func_detail::signature<Signature>::template owner<false, true, Capacity, Alignment, heap_allocator>;
}
#undef CE_FUNC_INLINE
