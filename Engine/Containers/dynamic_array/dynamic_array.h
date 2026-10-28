#pragma once
#include "../include/ce/memory.h"
#include "../include/ce/fixed_array.h"
#include <string.h>
#include <initializer_list>
namespace ce
{
    template<class T, class Allocator = heap_allocator, bool DeferredCopy = false>
    class dynamic_array
    {
        static_assert(detail::nothrow_policy<Allocator>);
        static_assert(noexcept(Allocator(static_cast<const Allocator&>(*static_cast<Allocator*>(nullptr)))));
        T* p_ = nullptr;
        size_t n_ = 0, cap_ = 0;
        CE_NO_UNIQUE_ADDRESS Allocator alloc_;
        static constexpr bool scoped_construction = detail::array_scoped_construction<Allocator>;
        static constexpr bool propagate_copy = detail::array_propagate_copy<Allocator>;
        static constexpr bool propagate_move = detail::array_propagate_move<Allocator>;

        CE_FORCEINLINE static Allocator select_allocator(const Allocator& source)
        {
            if constexpr (requires { source.select_on_container_copy_construction(); })
                return source.select_on_container_copy_construction();
            else
                return source;
        }

        static bool same_allocator(const Allocator& left, const Allocator& right)
        {
            if constexpr (requires { left == right; })
                return left == right;
            else
                return false;
        }

        template<class... A>
        CE_FORCEINLINE T* construct(T* target, A&&... args)
        {
            if constexpr (scoped_construction)
            {
                alloc_.construct(target, static_cast<A&&>(args)...);
                return target;
            }
            else
                return ::new (static_cast<void*>(target)) T(static_cast<A&&>(args)...);
        }

        template<class... A>
        CE_FORCEINLINE T make_value(A&&... args)
        {
            if constexpr (scoped_construction &&
                          requires { alloc_.template construct_value<T>(static_cast<A&&>(args)...); })
                return alloc_.template construct_value<T>(static_cast<A&&>(args)...);
            else
                return T(static_cast<A&&>(args)...);
        }

        static void destroy(T* p, size_t n) noexcept
        {
            if constexpr (!detail::trivial<T>)
                for (size_t i = n; i; --i)
                    p[i - 1].~T();
        }

        T* allocate(size_t n)
        {
            return static_cast<T*>(alloc_.allocate(detail::checked_bytes(n, sizeof(T)), alignof(T)));
        }

        void free(T* p, size_t c) noexcept
        {
            if (p)
                alloc_.deallocate(p, c * sizeof(T), alignof(T));
        }

        void relocate(T* q, size_t& built)
        {
            if constexpr (detail::trivial<T> && !scoped_construction)
            {
                if (n_)
                    memcpy(q, p_, n_ * sizeof(T));
                built = n_;
            }
            else
                for (; built < n_; ++built)
                {
                    if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
                        construct(q + built, static_cast<T&&>(p_[built]));
                    else
                        construct(q + built, p_[built]);
                }
        }

        size_t growth(size_t need) const
        {
            if (need > SIZE_MAX / sizeof(T))
                throw std::bad_alloc();
            auto limit = SIZE_MAX / sizeof(T);
            size_t c = cap_ ? cap_ : 8;
            if (c > limit)
                c = limit;
            while (c < need)
            {
                auto delta = c / 2 + 1;
                if (c > limit - delta)
                {
                    c = limit;
                    break;
                }
                c += delta;
            }
            return c;
        }

        template<class... A>
        T& grow_append(A&&... a)
        {
            auto c = growth(n_ + 1);
            if constexpr (detail::trivial<T> && !scoped_construction && detail::nothrow_move<T> &&
                          detail::byte_reallocating_allocator<Allocator>)
            {
                T pending(static_cast<A&&>(a)...); // preserve references into old storage
                auto q = static_cast<T*>(
                    alloc_.reallocate(p_, cap_ * sizeof(T), detail::checked_bytes(c, sizeof(T)), alignof(T)));
                p_ = q;
                cap_ = c;
                ::new (static_cast<void*>(p_ + n_)) T(static_cast<T&&>(pending));
                return p_[n_++];
            }
            auto q = allocate(c);
            size_t built = 0;
            bool tail = false;
            try
            {
                construct(q + n_, static_cast<A&&>(a)...);
                tail = true;
                relocate(q, built);
            }
            catch (...)
            {
                destroy(q, built);
                if (tail)
                    q[n_].~T();
                free(q, c);
                throw;
            }
            destroy(p_, n_);
            free(p_, cap_);
            p_ = q;
            cap_ = c;
            ++n_;
            return p_[n_ - 1];
        }

      public:
        using value_type = T;
        using allocator_type = Allocator;
        using size_type = size_t;
        using iterator = T*;
        using const_iterator = const T*;
        using difference_type = ptrdiff_t;
        using reference = T&;
        using const_reference = const T&;
        using pointer = T*;
        using const_pointer = const T*;
        using reverse_iterator = reverse_pointer_iterator<T>;
        using const_reverse_iterator = reverse_pointer_iterator<const T>;
        dynamic_array() noexcept(__is_nothrow_constructible(Allocator)) = default;

        explicit dynamic_array(size_t n) : dynamic_array(n, Allocator{})
        {
        }

        dynamic_array(size_t n, const Allocator& a) : dynamic_array(a)
        {
            if constexpr (__is_constructible(T, T&&) || detail::copyable<T>)
                resize(n);
            else
            {
                // Fixed-count storage also supports immovable objects such as atomics.
                // The completed delegating target destroys constructed elements if a
                // default constructor throws; no relocation operation is instantiated.
                if (n)
                {
                    p_ = allocate(n);
                    cap_ = n;
                    for (; n_ < n; ++n_)
                        construct(p_ + n_);
                }
            }
        }

        dynamic_array(size_t n, const T& value, const Allocator& a = Allocator{}) : dynamic_array(a)
        {
            resize(n, value);
        }

        dynamic_array(std::initializer_list<T> values, const Allocator& a = Allocator{}) : dynamic_array(a)
        {
            assign(values.begin(), values.end());
        }

        template<class It>
            requires requires(It i) {
                *i;
                ++i;
                i != i;
            }
        dynamic_array(It first, It last, const Allocator& a = Allocator{}) : dynamic_array(a)
        {
            assign(first, last);
        }

        dynamic_array& operator=(std::initializer_list<T> values)
        {
            assign(values.begin(), values.end());
            return *this;
        }

        void assign(size_t n, const T& value)
        {
            dynamic_array fresh(n, value, alloc_);
            swap(fresh);
        }

        void assign(std::initializer_list<T> values)
        {
            assign(values.begin(), values.end());
        }

        template<class It>
            requires requires(It i) {
                *i;
                ++i;
                i != i;
            }
        void assign(It first, It last)
        {
            if constexpr (detail::trivial<T> && !scoped_construction &&
                          (detail::same_type<It, T*> || detail::same_type<It, const T*>))
            {
                const auto count = first == last ? size_t{} : static_cast<size_t>(last - first);
                // A valid aliased source fits the current storage, so reserve cannot
                // invalidate it. External ranges may allocate before the nonthrowing copy.
                if (count > cap_)
                    reserve(count);
                if (count)
                    memmove(p_, first, count * sizeof(T));
                n_ = count;
            }
            else
            {
                dynamic_array fresh(alloc_);
                if constexpr (requires { last - first; })
                {
                    if (first != last)
                    {
                        auto count = last - first;
                        if (count > 0)
                            fresh.reserve(static_cast<size_t>(count));
                    }
                }
                for (; first != last; ++first)
                    fresh.emplace_back(*first);
                swap(fresh);
            }
        }

        explicit dynamic_array(const Allocator& a) noexcept : alloc_(a)
        {
        }

        inline dynamic_array(const dynamic_array& o)
            requires(DeferredCopy || detail::copyable<T>)
            : dynamic_array(o, select_allocator(o.alloc_))
        {
        }

        inline dynamic_array(const dynamic_array& o, const Allocator& a)
            requires(DeferredCopy || detail::copyable<T>)
            : alloc_(a)
        {
            if (!o.n_)
                return;
            p_ = allocate(o.n_);
            cap_ = o.n_;
            try
            {
                if constexpr (detail::trivial<T> && !scoped_construction)
                {
                    memcpy(p_, o.p_, o.n_ * sizeof(T));
                    n_ = o.n_;
                }
                else
                    for (; n_ < o.n_; ++n_)
                        construct(p_ + n_, o.p_[n_]);
            }
            catch (...)
            {
                destroy(p_, n_);
                free(p_, cap_);
                throw;
            }
        }

        dynamic_array(dynamic_array&& o) noexcept : p_(o.p_), n_(o.n_), cap_(o.cap_), alloc_(o.alloc_)
        {
            o.p_ = nullptr;
            o.n_ = o.cap_ = 0;
        }

        dynamic_array(dynamic_array&& other, const Allocator& a) : dynamic_array(a)
        {
            if (same_allocator(alloc_, other.alloc_))
            {
                p_ = other.p_;
                n_ = other.n_;
                cap_ = other.cap_;
                other.p_ = nullptr;
                other.n_ = other.cap_ = 0;
            }
            else
            {
                reserve(other.n_);
                for (auto& value : other)
                    emplace_back(static_cast<T&&>(value));
                other.clear();
            }
        }

        dynamic_array& operator=(const dynamic_array& o)
            requires(DeferredCopy || detail::copyable<T>)
        {
            if (this != &o)
            {
                if constexpr (propagate_copy)
                {
                    dynamic_array copy(o);
                    swap(copy);
                }
                else
                {
                    dynamic_array copy(o, alloc_);
                    swap(copy);
                }
            }
            return *this;
        }

        dynamic_array& operator=(dynamic_array&& o) noexcept(propagate_move)
        {
            if (this != &o)
            {
                if constexpr (!propagate_move)
                {
                    if (!same_allocator(alloc_, o.alloc_))
                    {
                        dynamic_array fresh(alloc_);
                        fresh.reserve(o.n_);
                        for (auto& value : o)
                            fresh.emplace_back(static_cast<T&&>(value));
                        swap(fresh);
                        o.clear();
                        return *this;
                    }
                }
                clear();
                free(p_, cap_);
                if constexpr (propagate_move)
                    alloc_ = o.alloc_;
                p_ = o.p_;
                n_ = o.n_;
                cap_ = o.cap_;
                o.p_ = nullptr;
                o.n_ = o.cap_ = 0;
            }
            return *this;
        }

        ~dynamic_array()
        {
            static_assert(detail::nothrow_destroy<T>);
            destroy(p_, n_);
            free(p_, cap_);
        }

        void swap(dynamic_array& o) noexcept
        {
            auto p = p_;
            p_ = o.p_;
            o.p_ = p;
            auto n = n_;
            n_ = o.n_;
            o.n_ = n;
            auto c = cap_;
            cap_ = o.cap_;
            o.cap_ = c;
            auto a = alloc_;
            alloc_ = o.alloc_;
            o.alloc_ = a;
        }

        Allocator get_allocator() const noexcept
        {
            return alloc_;
        }

        void reserve(size_t c)
        {
            if (c <= cap_)
                return;
            if constexpr (detail::trivial<T> && !scoped_construction && detail::nothrow_move<T> &&
                          detail::byte_reallocating_allocator<Allocator>)
            {
                auto q = static_cast<T*>(
                    alloc_.reallocate(p_, cap_ * sizeof(T), detail::checked_bytes(c, sizeof(T)), alignof(T)));
                p_ = q;
                cap_ = c;
                return;
            }
            auto q = allocate(c);
            size_t built = 0;
            try
            {
                relocate(q, built);
            }
            catch (...)
            {
                destroy(q, built);
                free(q, c);
                throw;
            }
            destroy(p_, n_);
            free(p_, cap_);
            p_ = q;
            cap_ = c;
        }

        template<class... A>
        T& emplace_back(A&&... a)
        {
            if (n_ == cap_)
                return grow_append(static_cast<A&&>(a)...);
            auto p = construct(p_ + n_, static_cast<A&&>(a)...);
            ++n_;
            return *p;
        }

        void push_back(const T& v)
        {
            emplace_back(v);
        }

        void push_back(T&& v)
        {
            emplace_back(static_cast<T&&>(v));
        }

        void pop_back() noexcept
        {
            p_[--n_].~T();
        }

        void clear() noexcept
        {
            destroy(p_, n_);
            n_ = 0;
        }

        void resize(size_t n)
        {
            if (n < n_)
            {
                destroy(p_ + n, n_ - n);
                n_ = n;
                return;
            }
            if (n > cap_)
                reserve(growth(n));
            auto old = n_;
            if constexpr (__is_trivially_constructible(T) && !scoped_construction)
            {
                auto* const limit = p_ + n;
                for (auto* current = p_ + old; current != limit; ++current)
                    ::new (static_cast<void*>(current)) T();
                n_ = n;
            }
            else
            {
                try
                {
                    for (; n_ < n; ++n_)
                        construct(p_ + n_);
                }
                catch (...)
                {
                    destroy(p_ + old, n_ - old);
                    n_ = old;
                    throw;
                }
            }
        }

        void resize(size_t n, const T& value)
        {
            if (n <= n_)
            {
                resize(n);
                return;
            }
            T copy = make_value(value);
            if (n > cap_)
                reserve(growth(n));
            auto old = n_;
            if constexpr (__is_trivially_constructible(T, const T&) && !scoped_construction)
            {
                auto* const limit = p_ + n;
                for (auto* current = p_ + old; current != limit; ++current)
                    ::new (static_cast<void*>(current)) T(copy);
                n_ = n;
            }
            else
            {
                try
                {
                    for (; n_ < n; ++n_)
                        construct(p_ + n_, copy);
                }
                catch (...)
                {
                    destroy(p_ + old, n_ - old);
                    n_ = old;
                    throw;
                }
            }
        }

        T* erase(size_t pos, size_t count = 1)
        {
            if (pos > n_ || count > n_ - pos)
                throw bounds_error{};
            if (!count)
                return pos ? p_ + pos : p_;
            if constexpr (detail::trivial<T>)
            {
                memmove(p_ + pos, p_ + pos + count, (n_ - pos - count) * sizeof(T));
            }
            else
                for (size_t i = pos; i < n_ - count; ++i)
                    p_[i] = static_cast<T&&>(p_[i + count]);
            destroy(p_ + n_ - count, count);
            n_ -= count;
            return p_ + pos;
        }

        template<class P>
            requires(detail::same_type<P, T*> || detail::same_type<P, const T*>)
        T* erase(P position)
        {
            return erase(static_cast<size_t>(position - p_));
        }

        template<class P, class Q>
            requires((detail::same_type<P, T*> || detail::same_type<P, const T*>) &&
                     (detail::same_type<Q, T*> || detail::same_type<Q, const T*>))
        T* erase(P first, Q last)
        {
            if (first == last)
                return const_cast<T*>(first);
            return erase(static_cast<size_t>(first - p_), static_cast<size_t>(last - first));
        }

        template<class P, class... A>
            requires(detail::same_type<P, T*> || detail::same_type<P, const T*>)
        T* emplace(P position, A&&... a)
        {
            const auto offset = position == p_ ? size_t{} : static_cast<size_t>(position - p_);
            return &emplace(offset, static_cast<A&&>(a)...);
        }

        template<class P>
            requires(detail::same_type<P, T*> || detail::same_type<P, const T*>)
        T* insert(P position, const T& value)
        {
            return emplace(position, value);
        }

        template<class P>
            requires(detail::same_type<P, T*> || detail::same_type<P, const T*>)
        T* insert(P position, T&& value)
        {
            return emplace(position, static_cast<T&&>(value));
        }

        template<class P, class It>
            requires((detail::same_type<P, T*> || detail::same_type<P, const T*>) &&
                     requires(It i) {
                         *i;
                         ++i;
                         i != i;
                     })
        T* insert(P position, It first, It last)
        {
            const auto offset = position == p_ ? size_t{} : static_cast<size_t>(position - p_);
            if (offset > n_)
                throw bounds_error{};
            if constexpr (detail::trivial<T> && !scoped_construction &&
                          (detail::same_type<It, T*> || detail::same_type<It, const T*>))
            {
                if (offset == n_)
                {
                    if (first == last)
                        return offset ? p_ + offset : p_;
                    const auto count = static_cast<size_t>(last - first);
                    if (count > max_size() - n_)
                        throw std::bad_alloc{};
                    const auto address = reinterpret_cast<uintptr_t>(first);
                    const auto start = reinterpret_cast<uintptr_t>(p_);
                    const bool alias = p_ && address >= start && address - start < n_ * sizeof(T);
                    const auto sourceOffset = alias ? (address - start) / sizeof(T) : 0;
                    if (n_ + count > cap_)
                        reserve(growth(n_ + count));
                    const auto source = alias ? p_ + sourceOffset : first;
                    memmove(p_ + n_, source, count * sizeof(T));
                    n_ += count;
                    return p_ + offset;
                }
            }
            // Materialize input before relocation: permits aliases and input iterators.
            dynamic_array incoming(first, last, alloc_);
            if (incoming.empty())
                return offset ? p_ + offset : p_;
            if (incoming.size() > max_size() - n_)
                throw std::bad_alloc{};
            dynamic_array fresh(alloc_);
            fresh.reserve(growth(n_ + incoming.size()));
            auto append_old = [&](size_t i)
            {
                if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
                    fresh.emplace_back(static_cast<T&&>(p_[i]));
                else
                    fresh.emplace_back(p_[i]);
            };
            for (size_t i = 0; i < offset; ++i)
                append_old(i);
            for (auto& value : incoming)
            {
                if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
                    fresh.emplace_back(static_cast<T&&>(value));
                else
                    fresh.emplace_back(value);
            }
            for (size_t i = offset; i < n_; ++i)
                append_old(i);
            swap(fresh);
            return p_ + offset;
        }

        template<class P>
            requires(detail::same_type<P, T*> || detail::same_type<P, const T*>)
        T* insert(P position, size_t count, const T& value)
        {
            dynamic_array incoming(count, value, alloc_);
            return insert(position, incoming.begin(), incoming.end());
        }

        template<class P>
            requires(detail::same_type<P, T*> || detail::same_type<P, const T*>)
        T* insert(P position, std::initializer_list<T> values)
        {
            return insert(position, values.begin(), values.end());
        }

        template<class... A>
        T& emplace(size_t pos, A&&... a)
        {
            if (pos > n_)
                throw bounds_error{};
            if (pos == n_)
                return emplace_back(static_cast<A&&>(a)...);
            T value = make_value(static_cast<A&&>(a)...);
            dynamic_array fresh(alloc_);
            fresh.reserve(growth(n_ + 1));
            for (size_t i = 0; i < pos; ++i)
            {
                if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
                    fresh.emplace_back(static_cast<T&&>(p_[i]));
                else
                    fresh.emplace_back(p_[i]);
            }
            fresh.emplace_back(static_cast<T&&>(value));
            for (size_t i = pos; i < n_; ++i)
            {
                if constexpr (detail::nothrow_move<T> || !detail::copyable<T>)
                    fresh.emplace_back(static_cast<T&&>(p_[i]));
                else
                    fresh.emplace_back(p_[i]);
            }
            swap(fresh);
            return p_[pos];
        }

        T& insert(size_t pos, const T& v)
        {
            return emplace(pos, v);
        }

        T& insert(size_t pos, T&& v)
        {
            return emplace(pos, static_cast<T&&>(v));
        }

        bool empty() const noexcept
        {
            return !n_;
        }

        size_t size() const noexcept
        {
            return n_;
        }

        size_t capacity() const noexcept
        {
            return cap_;
        }

        static constexpr size_t max_size() noexcept
        {
            return SIZE_MAX / sizeof(T);
        }

        void shrink_to_fit()
        {
            if (n_ == cap_)
                return;
            dynamic_array fresh(alloc_);
            fresh.reserve(n_);
            size_t built = 0;
            try
            {
                relocate(fresh.p_, built);
            }
            catch (...)
            {
                destroy(fresh.p_, built);
                throw;
            }
            fresh.n_ = n_;
            swap(fresh);
        }

        T* data() noexcept
        {
            return p_;
        }

        const T* data() const noexcept
        {
            return p_;
        }

        T* begin() noexcept
        {
            return p_;
        }

        const T* begin() const noexcept
        {
            return p_;
        }

        const T* cbegin() const noexcept
        {
            return p_;
        }

        T* end() noexcept
        {
            return n_ ? p_ + n_ : p_;
        }

        const T* end() const noexcept
        {
            return n_ ? p_ + n_ : p_;
        }

        const T* cend() const noexcept
        {
            return end();
        }

        reverse_iterator rbegin() noexcept
        {
            return reverse_iterator(end());
        }

        const_reverse_iterator rbegin() const noexcept
        {
            return const_reverse_iterator(end());
        }

        const_reverse_iterator crbegin() const noexcept
        {
            return rbegin();
        }

        reverse_iterator rend() noexcept
        {
            return reverse_iterator(begin());
        }

        const_reverse_iterator rend() const noexcept
        {
            return const_reverse_iterator(begin());
        }

        const_reverse_iterator crend() const noexcept
        {
            return rend();
        }

        bool operator==(const dynamic_array& other) const
        {
            if (n_ != other.n_)
                return false;
            for (size_t i = 0; i < n_; ++i)
                if (!(p_[i] == other.p_[i]))
                    return false;
            return true;
        }

        auto operator<=>(const dynamic_array& other) const
            requires requires(const T& value) { value < value; }
        {
            using order = decltype(detail::array_compare(p_[0], other.p_[0]));
            const auto count = n_ < other.n_ ? n_ : other.n_;
            for (size_t i = 0; i < count; ++i)
            {
                auto compared = detail::array_compare(p_[i], other.p_[i]);
                if (compared != 0)
                    return compared;
            }
            return n_ < other.n_ ? order::less : n_ > other.n_ ? order::greater : order::equivalent;
        }

        T& operator[](size_t i) noexcept
        {
            return p_[i];
        }

        const T& operator[](size_t i) const noexcept
        {
            return p_[i];
        }

        T& at(size_t i)
        {
            if (i >= n_)
                throw bounds_error{};
            return p_[i];
        }

        const T& at(size_t i) const
        {
            if (i >= n_)
                throw bounds_error{};
            return p_[i];
        }

        T& front() noexcept
        {
            return p_[0];
        }

        const T& front() const noexcept
        {
            return p_[0];
        }

        T& back() noexcept
        {
            return p_[n_ - 1];
        }

        const T& back() const noexcept
        {
            return p_[n_ - 1];
        }
    };
    // Recursive trees need a copy declaration before their element is complete.
    // The ordinary array keeps its stricter copyability trait; this opt-in variant
    // defers element copy validation until the copy operation is instantiated.
    template<class T, class Allocator = heap_allocator>
    using recursive_array = dynamic_array<T, Allocator, true>;

    template<class T, class A, class Predicate>
    size_t erase_if(dynamic_array<T, A>& values, Predicate predicate)
    {
        const auto before = values.size();
        size_t output = 0;
        for (size_t i = 0; i < before; ++i)
            if (!predicate(values[i]))
            {
                if (output != i)
                    values[output] = static_cast<T&&>(values[i]);
                ++output;
            }
        values.erase(output, before - output);
        return before - output;
    }

    template<class T, class A, class U>
    size_t erase(dynamic_array<T, A>& values, const U& value)
    {
        return erase_if(values, [&](const T& item) { return item == value; });
    }
} // namespace ce
