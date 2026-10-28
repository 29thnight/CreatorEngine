#pragma once
#include "../include/ce/memory.h"
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // intentional payload alignment
#endif
namespace ce
{
    namespace detail
    {
        template<class T>
        inline constexpr bool is_void = same_type<T, void>;
        template<class T>
        inline constexpr bool is_pointer = false;
        template<class T>
        inline constexpr bool is_pointer<T*> = true;

        template<class T>
        struct remove_reference
        {
            using type = T;
        };

        template<class T>
        struct remove_reference<T&>
        {
            using type = T;
        };

        template<class T>
        struct remove_reference<T&&>
        {
            using type = T;
        };

        template<class T>
        struct remove_cv
        {
            using type = T;
        };

        template<class T>
        struct remove_cv<const T>
        {
            using type = T;
        };

        template<class T>
        struct remove_cv<volatile T>
        {
            using type = T;
        };

        template<class T>
        struct remove_cv<const volatile T>
        {
            using type = T;
        };

        template<class T>
        struct callable_decay
        {
            using type = typename remove_cv<T>::type;
        };

        template<class R, class... A>
        struct callable_decay<R(A...)>
        {
            using type = R (*)(A...);
        };

        template<class R, class... A>
        struct callable_decay<R(A...) noexcept>
        {
            using type = R (*)(A...) noexcept;
        };
        template<class T>
        using decay_callable = typename callable_decay<typename remove_reference<T>::type>::type;
    } // namespace detail

    struct bad_function_call
    {
    };
    template<class Signature, size_t Bytes = 64, size_t Align = alignof(max_align_t)>
    class inplace_function;

    template<class R, class... Args, size_t Bytes, size_t Align>
    class inplace_function<R(Args...), Bytes, Align>
    {
        static_assert(Bytes > 0 && Align > 0 && (Align & (Align - 1)) == 0);

        struct operations
        {
            R (*call)(void*, Args&&...);
            void (*destroy)(void*) noexcept;
            void (*move)(void*, void*) noexcept;
        };

        alignas(Align) mutable unsigned char storage_[Bytes];
        const operations* ops_ = nullptr;

        template<class F>
        static const operations& table()
        {
            static constexpr operations ops = {
                [](void* p, Args&&... a) -> R
                {
                    if constexpr (detail::is_void<R>)
                    {
                        (*std::launder(static_cast<F*>(p)))(static_cast<Args&&>(a)...);
                    }
                    else
                        return (*std::launder(static_cast<F*>(p)))(static_cast<Args&&>(a)...);
                },
                [](void* p) noexcept { detail::destroy_at(std::launder(static_cast<F*>(p))); },
                [](void* dst, void* src) noexcept
                {
                    auto p = std::launder(static_cast<F*>(src));
                    ::new (dst) F(static_cast<F&&>(*p));
                    detail::destroy_at(p);
                }};
            return ops;
        }

      public:
        inplace_function() noexcept = default;

        template<class F>
        explicit inplace_function(F&& f)
        {
            using U = detail::decay_callable<F>;
            static_assert(sizeof(U) <= Bytes && alignof(U) <= Align, "Callable exceeds inline storage");
            static_assert(detail::nothrow_move<U>);
            static_assert(detail::nothrow_destroy<U>);
            if constexpr (detail::is_pointer<U>)
            {
                if (f == nullptr)
                    return;
            }
            ::new (static_cast<void*>(storage_)) U(static_cast<F&&>(f));
            ops_ = &table<U>();
        }

        inplace_function(const inplace_function&) = delete;
        inplace_function& operator=(const inplace_function&) = delete;

        inplace_function(inplace_function&& o) noexcept : ops_(o.ops_)
        {
            if (ops_)
            {
                ops_->move(storage_, o.storage_);
                o.ops_ = nullptr;
            }
        }

        inplace_function& operator=(inplace_function&& o) noexcept
        {
            if (this != &o)
            {
                reset();
                ops_ = o.ops_;
                if (ops_)
                {
                    ops_->move(storage_, o.storage_);
                    o.ops_ = nullptr;
                }
            }
            return *this;
        }

        ~inplace_function()
        {
            reset();
        }

        void reset() noexcept
        {
            if (ops_)
            {
                ops_->destroy(storage_);
                ops_ = nullptr;
            }
        }

        explicit operator bool() const noexcept
        {
            return ops_ != nullptr;
        }

        R operator()(Args... a) const
        {
            if (!ops_)
                throw bad_function_call{};
            return ops_->call(storage_, static_cast<Args&&>(a)...);
        }
    };
} // namespace ce

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
