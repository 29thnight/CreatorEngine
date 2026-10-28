module;
#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#include "module_config.h"
export module Creator.Containers:string;
export import :memory;
export import :utf8_copy;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "memory.h"
// CE_HEADER_INCLUDE #include "utf8_copy.h"
// CE_HEADER_INCLUDE #include <string.h>
    namespace ce
    {
        class utf8_string_view
        {
            const char* p_ = nullptr;
            size_t n_ = 0;

          public:
            static constexpr size_t npos = SIZE_MAX;
            constexpr utf8_string_view() noexcept = default;
            constexpr utf8_string_view(const char* p, size_t n) noexcept : p_(p), n_(n) {}
            inline utf8_string_view(const char* p) noexcept : p_(p), n_(strlen(p)) {}
            constexpr const char* data() const noexcept { return p_; }
            constexpr size_t size() const noexcept { return n_; }
            constexpr bool empty() const noexcept { return !n_; }
            char operator[](size_t i) const noexcept { return p_[i]; }
            utf8_string_view substr(size_t pos, size_t count = npos) const
            {
                if (pos > n_)
                {
                    throw bounds_error{};
                }
                auto n = n_ - pos;
                if (count < n)
                {
                    n = count;
                }
                return {pos ? p_ + pos : p_, n};
            }
            inline size_t find(utf8_string_view v, size_t pos = 0) const noexcept
            {
                if (pos > n_ || v.n_ > n_ - pos)
                {
                    return npos;
                }
                if (!v.n_)
                {
                    return pos;
                }
                auto cur = p_ + pos;
                auto stop = p_ + n_ - v.n_ + 1;
                while (cur < stop)
                {
                    auto hit =
                        static_cast<const char*>(memchr(cur, static_cast<unsigned char>(v.p_[0]), size_t(stop - cur)));
                    if (!hit)
                    {
                        return npos;
                    }
                    if (memcmp(hit, v.p_, v.n_) == 0)
                    {
                        return size_t(hit - p_);
                    }
                    cur = hit + 1;
                }
                return npos;
            }
            int compare(utf8_string_view o) const noexcept
            {
                auto n = n_ < o.n_ ? n_ : o.n_;
                int c = n ? memcmp(p_, o.p_, n) : 0;
                return c ? c : (n_ < o.n_ ? -1 : n_ > o.n_ ? 1 : 0);
            }
            bool operator==(utf8_string_view o) const noexcept
            {
                return n_ == o.n_ && (!n_ || memcmp(p_, o.p_, n_) == 0);
            }
            bool starts_with(utf8_string_view v) const noexcept
            {
                return v.n_ <= n_ && (!v.n_ || memcmp(p_, v.p_, v.n_) == 0);
            }
        };
        template<class Allocator = heap_allocator>
        class basic_utf8_string
        {
            static_assert(detail::nothrow_policy<Allocator>);
            static constexpr size_t short_capacity = 23;
            union storage
            {
                char inline_bytes[short_capacity + 1];
                char* large;
                storage() noexcept {}
            } data_;
            size_t n_ = 0, cap_ = short_capacity;
            CE_NO_UNIQUE_ADDRESS Allocator alloc_;
            bool is_inline() const noexcept { return cap_ == short_capacity; }
            char* ptr() noexcept { return is_inline() ? data_.inline_bytes : data_.large; }
            const char* ptr() const noexcept { return is_inline() ? data_.inline_bytes : data_.large; }
            void release() noexcept
            {
                if (!is_inline())
                {
                    alloc_.deallocate(data_.large, cap_ + 1, alignof(char));
                }
            }
            CE_FORCEINLINE void initialize(utf8_string_view v)
            {
                if (v.size() <= short_capacity)
                {
                    auto n = v.size();
                    // Fixed-width copies avoid a CRT call without reading past the view.
                    if (n >= 16)
                    {
                        memcpy(data_.inline_bytes, v.data(), 16);
                        memcpy(data_.inline_bytes + n - 8, v.data() + n - 8, 8);
                    }
                    else if (n >= 8)
                    {
                        memcpy(data_.inline_bytes, v.data(), 8);
                        memcpy(data_.inline_bytes + n - 8, v.data() + n - 8, 8);
                    }
                    else if (n >= 4)
                    {
                        memcpy(data_.inline_bytes, v.data(), 4);
                        memcpy(data_.inline_bytes + n - 4, v.data() + n - 4, 4);
                    }
                    else if (n)
                    {
                        data_.inline_bytes[0] = v[0];
                        data_.inline_bytes[n / 2] = v[n / 2];
                        data_.inline_bytes[n - 1] = v[n - 1];
                    }
                    data_.inline_bytes[v.size()] = 0;
                    n_ = v.size();
                }
                else
                {
                    if (v.size() == SIZE_MAX)
                    {
                        throw std::bad_alloc();
                    }
                    auto p = static_cast<char*>(alloc_.allocate(v.size() + 1, alignof(char)));
                    if (v.size() == 2048)
                    {
                        detail::utf8_copy_2048_dispatch(p, v.data());
                    }
                    else
                    {
                        memcpy(p, v.data(), v.size());
                    }
                    p[v.size()] = 0;
                    data_.large = p;
                    n_ = cap_ = v.size();
                }
            }
            size_t grown(size_t need) const
            {
                if (need == SIZE_MAX)
                {
                    throw std::bad_alloc();
                }
                auto c = cap_ <= (SIZE_MAX - 1) / 2 ? cap_ * 2 : SIZE_MAX - 1;
                return c > need ? c : need;
            }
            CE_NOINLINE void grow_for(size_t n) { reserve(grown(n)); }

          public:
            using value_type = char;
            template<class Operation>
            void resize_and_overwrite(size_t n, Operation op)
            {
                if (n > cap_)
                {
                    reserve(grown(n));
                }
                size_t written = op(ptr(), n);
                if (written > n)
                {
                    abort();
                }
                n_ = written;
                ptr()[n_] = 0;
            }
            basic_utf8_string() noexcept { data_.inline_bytes[0] = 0; }
            explicit basic_utf8_string(const Allocator& a) noexcept : alloc_(a) { data_.inline_bytes[0] = 0; }
            CE_FORCEINLINE explicit basic_utf8_string(utf8_string_view v, const Allocator& a = Allocator{}) : alloc_(a)
            {
                initialize(v);
            }
            CE_FORCEINLINE basic_utf8_string(const basic_utf8_string& o) : alloc_(o.alloc_)
            {
                if (o.is_inline())
                {
                    if (o.n_ < 16)
                    {
                        memcpy(data_.inline_bytes, o.data_.inline_bytes, 16);
                    }
                    else
                    {
                        memcpy(data_.inline_bytes, o.data_.inline_bytes, sizeof(data_.inline_bytes));
                    }
                    n_ = o.n_;
                }
                else
                {
                    initialize(o.view());
                }
            }
            basic_utf8_string(basic_utf8_string&& o) noexcept : n_(o.n_), cap_(o.cap_), alloc_(o.alloc_)
            {
                if (o.is_inline())
                {
                    memcpy(data_.inline_bytes, o.data_.inline_bytes, o.n_ + 1);
                }
                else
                {
                    data_.large = o.data_.large;
                }
                o.n_ = 0;
                o.cap_ = short_capacity;
                o.data_.inline_bytes[0] = 0;
            }
            ~basic_utf8_string() { release(); }
            basic_utf8_string& operator=(const basic_utf8_string& o)
            {
                if (this != &o)
                {
                    basic_utf8_string copy(o);
                    *this = static_cast<basic_utf8_string&&>(copy);
                }
                return *this;
            }
            basic_utf8_string& operator=(basic_utf8_string&& o) noexcept
            {
                if (this != &o)
                {
                    release();
                    alloc_ = o.alloc_;
                    n_ = o.n_;
                    cap_ = o.cap_;
                    if (o.is_inline())
                    {
                        memcpy(data_.inline_bytes, o.data_.inline_bytes, o.n_ + 1);
                    }
                    else
                    {
                        data_.large = o.data_.large;
                    }
                    o.n_ = 0;
                    o.cap_ = short_capacity;
                    o.data_.inline_bytes[0] = 0;
                }
                return *this;
            }
            void swap(basic_utf8_string& o) noexcept
            {
                if (this == &o)
                {
                    return;
                }
                basic_utf8_string tmp(static_cast<basic_utf8_string&&>(*this));
                *this = static_cast<basic_utf8_string&&>(o);
                o = static_cast<basic_utf8_string&&>(tmp);
            }
            Allocator get_allocator() const noexcept { return alloc_; }
            void reserve(size_t c)
            {
                if (c <= cap_)
                {
                    return;
                }
                if (c == SIZE_MAX)
                {
                    throw std::bad_alloc();
                }
                auto q = static_cast<char*>(alloc_.allocate(c + 1, alignof(char)));
                memcpy(q, ptr(), n_ + 1);
                release();
                data_.large = q;
                cap_ = c;
            }
            void append(utf8_string_view v)
            {
                const size_t count = v.size(), old = n_;
                if (count <= cap_ - old)
                {
                    auto p = ptr();
                    const size_t total = old + count;
                    n_ = total;
                    if (count)
                    {
                        if (count <= 64)
                        {
                            auto destination = p + old;
                            auto source = v.data();
                            // Snapshot every source byte before writing, including overlapping views.
                            if (count >= 32)
                            {
                                char first[32], last[32];
                                memcpy(first, source, 32);
                                memcpy(last, source + count - 32, 32);
                                memcpy(destination, first, 32);
                                memcpy(destination + count - 32, last, 32);
                            }
                            else if (count >= 16)
                            {
                                char first[16], last[16];
                                memcpy(first, source, 16);
                                memcpy(last, source + count - 16, 16);
                                memcpy(destination, first, 16);
                                memcpy(destination + count - 16, last, 16);
                            }
                            else if (count >= 8)
                            {
                                uint64_t first, last;
                                memcpy(&first, source, 8);
                                memcpy(&last, source + count - 8, 8);
                                memcpy(destination, &first, 8);
                                memcpy(destination + count - 8, &last, 8);
                            }
                            else if (count >= 4)
                            {
                                uint32_t first, last;
                                memcpy(&first, source, 4);
                                memcpy(&last, source + count - 4, 4);
                                memcpy(destination, &first, 4);
                                memcpy(destination + count - 4, &last, 4);
                            }
                            else
                            {
                                const char first = source[0], middle = source[count / 2], last = source[count - 1];
                                destination[0] = first;
                                destination[count / 2] = middle;
                                destination[count - 1] = last;
                            }
                        }
                        else if (count == 2048)
                        {
                            detail::utf8_copy_2048_dispatch(p + old, v.data());
                        }
                        else
                        {
                            memmove(p + old, v.data(), count);
                        }
                    }
                    p[total] = 0;
                    return;
                }
                append_grow(v);
            }

          private:
            CE_NOINLINE void append_grow(utf8_string_view v)
            {
                const size_t count = v.size(), old = n_;
                if (count > SIZE_MAX - 1 - old)
                {
                    throw std::bad_alloc();
                }
                auto source = reinterpret_cast<uintptr_t>(v.data()), base = reinterpret_cast<uintptr_t>(ptr());
                bool alias = source >= base && source - base <= old * 1;
                size_t offset = alias ? (source - base) / 1 : 0;
                size_t total = old + count;
                reserve(grown(total));
                auto p = ptr();
                if (count)
                {
                    memmove(p + old, alias ? p + offset : v.data(), count * 1);
                }
                n_ = total;
                p[total] = 0;
            }

          public:
            void push_back(char c)
            {
                if (n_ == cap_)
                {
                    grow_for(n_ + 1);
                }
                auto p = ptr();
                auto n = n_;
                p[n] = c;
                p[n + 1] = 0;
                n_ = n + 1;
            }
            void pop_back() noexcept { ptr()[--n_] = 0; }
            void resize(size_t n, char fill = 0)
            {
                if (n == SIZE_MAX)
                {
                    throw std::bad_alloc();
                }
                if (n > cap_)
                {
                    reserve(grown(n));
                }
                if (n > n_)
                {
                    memset(ptr() + n_, static_cast<unsigned char>(fill), n - n_);
                }
                n_ = n;
                ptr()[n_] = 0;
            }
            void assign(utf8_string_view v)
            {
                auto source = reinterpret_cast<uintptr_t>(v.data()), base = reinterpret_cast<uintptr_t>(ptr());
                bool alias = source >= base && source - base <= n_;
                if (v.size() > cap_)
                {
                    basic_utf8_string tmp(v, alloc_);
                    *this = static_cast<basic_utf8_string&&>(tmp);
                }
                else
                {
                    if (v.size())
                    {
                        if (alias)
                        {
                            memmove(ptr(), v.data(), v.size());
                        }
                        else
                        {
                            memcpy(ptr(), v.data(), v.size());
                        }
                    }
                    n_ = v.size();
                    ptr()[n_] = 0;
                }
            }
            void insert(size_t pos, utf8_string_view v)
            {
                if (pos > n_)
                {
                    throw bounds_error{};
                }
                basic_utf8_string copy(v, alloc_);
                if (copy.size() > SIZE_MAX - 1 - n_)
                {
                    throw std::bad_alloc();
                }
                auto total = n_ + copy.size();
                if (total > cap_)
                {
                    reserve(grown(total));
                }
                memmove(ptr() + pos + copy.size(), ptr() + pos, n_ - pos + 1);
                if (copy.size())
                {
                    memcpy(ptr() + pos, copy.data(), copy.size());
                }
                n_ = total;
            }
            void erase(size_t pos, size_t count = SIZE_MAX)
            {
                if (pos > n_)
                {
                    throw bounds_error{};
                }
                auto n = n_ - pos;
                if (count < n)
                {
                    n = count;
                }
                memmove(ptr() + pos, ptr() + pos + n, n_ - pos - n + 1);
                n_ -= n;
            }
            void clear() noexcept
            {
                n_ = 0;
                ptr()[0] = 0;
            }
            const char* c_str() const noexcept { return ptr(); }
            char* data() noexcept { return ptr(); }
            const char* data() const noexcept { return ptr(); }
            size_t size() const noexcept { return n_; }
            size_t capacity() const noexcept { return cap_; }
            bool empty() const noexcept { return !n_; }
            char& operator[](size_t i) noexcept { return ptr()[i]; }
            const char& operator[](size_t i) const noexcept { return ptr()[i]; }
            utf8_string_view view() const noexcept { return {ptr(), n_}; }
            operator utf8_string_view() const noexcept { return view(); }
            size_t find(utf8_string_view v, size_t pos = 0) const noexcept { return view().find(v, pos); }
            bool operator==(utf8_string_view v) const noexcept { return view() == v; }
            bool operator==(const basic_utf8_string& v) const noexcept { return view() == v.view(); }
        };
        using utf8_string = basic_utf8_string<>;

        // UTF-16 storage counts code units. Unicode validity is checked at text boundaries.
        class string_view
        {
            const char16_t* p_ = nullptr;
            size_t n_ = 0;

          public:
            static constexpr size_t npos = SIZE_MAX;
            constexpr string_view() noexcept = default;
            constexpr string_view(const char16_t* p, size_t n) noexcept : p_(p), n_(n) {}
            constexpr string_view(const char16_t* p) noexcept : p_(p)
            {
                while (p[n_])
                {
                    ++n_;
                }
            }
            constexpr const char16_t* data() const noexcept { return p_; }
            constexpr size_t size() const noexcept { return n_; }
            constexpr bool empty() const noexcept { return n_ == 0; }
            constexpr char16_t operator[](size_t i) const noexcept { return p_[i]; }
            string_view substr(size_t pos, size_t count = npos) const
            {
                if (pos > n_)
                {
                    throw bounds_error{};
                }
                size_t n = n_ - pos;
                if (count < n)
                {
                    n = count;
                }
                return {pos ? p_ + pos : p_, n};
            }
            size_t find(string_view v, size_t pos = 0) const noexcept
            {
                if (pos > n_ || v.n_ > n_ - pos)
                {
                    return npos;
                }
                if (!v.n_)
                {
                    return pos;
                }
                for (size_t i = pos; i <= n_ - v.n_; ++i)
                {
                    if (p_[i] == v.p_[0] && memcmp(p_ + i, v.p_, v.n_ * sizeof(char16_t)) == 0)
                    {
                        return i;
                    }
                }
                return npos;
            }
            int compare(string_view v) const noexcept
            {
                size_t n = n_ < v.n_ ? n_ : v.n_;
                for (size_t i = 0; i < n; ++i)
                {
                    if (p_[i] != v.p_[i])
                    {
                        return p_[i] < v.p_[i] ? -1 : 1;
                    }
                }
                return n_ < v.n_ ? -1 : n_ > v.n_ ? 1 : 0;
            }
            bool operator==(string_view v) const noexcept
            {
                return n_ == v.n_ && (!n_ || memcmp(p_, v.p_, n_ * sizeof(char16_t)) == 0);
            }
            bool starts_with(string_view v) const noexcept { return v.n_ <= n_ && substr(0, v.n_) == v; }
        };
        template<class Allocator = heap_allocator>
        class basic_utf16_string
        {
            static_assert(sizeof(char16_t) == 2);
            static_assert(detail::nothrow_policy<Allocator>);
            static constexpr size_t short_capacity = 11;
            union storage
            {
                char16_t inline_units[12];
                char16_t* large;
                storage() noexcept {}
            } data_;
            size_t n_ = 0, cap_ = short_capacity;
            CE_NO_UNIQUE_ADDRESS Allocator alloc_;
            bool is_inline() const noexcept { return cap_ == short_capacity; }
            char16_t* ptr() noexcept { return is_inline() ? data_.inline_units : data_.large; }
            const char16_t* ptr() const noexcept { return is_inline() ? data_.inline_units : data_.large; }
            void release() noexcept
            {
                if (!is_inline())
                {
                    alloc_.deallocate(data_.large, (cap_ + 1) * sizeof(char16_t), alignof(char16_t));
                }
            }
            size_t grown(size_t n) const
            {
                if (n > max_size())
                {
                    throw std::bad_alloc();
                }
                size_t c = cap_ <= max_size() / 2 ? cap_ * 2 : max_size();
                return c > n ? c : n;
            }

          public:
            using value_type = char16_t;
            static constexpr size_t max_size() noexcept { return SIZE_MAX / sizeof(char16_t) - 1; }
            basic_utf16_string() noexcept { data_.inline_units[0] = 0; }
            explicit basic_utf16_string(const Allocator& a) noexcept : alloc_(a) { data_.inline_units[0] = 0; }
            explicit basic_utf16_string(string_view v, const Allocator& a = Allocator{}) : alloc_(a)
            {
                if (v.size() <= short_capacity)
                {
                    if (v.size())
                    {
                        memcpy(data_.inline_units, v.data(), v.size() * sizeof(char16_t));
                    }
                    n_ = v.size();
                    data_.inline_units[n_] = 0;
                }
                else
                {
                    if (v.size() > max_size())
                    {
                        throw std::bad_alloc();
                    }
                    auto p =
                        static_cast<char16_t*>(alloc_.allocate((v.size() + 1) * sizeof(char16_t), alignof(char16_t)));
                    memcpy(p, v.data(), v.size() * sizeof(char16_t));
                    p[v.size()] = 0;
                    data_.large = p;
                    n_ = cap_ = v.size();
                }
            }
            basic_utf16_string(const char16_t* p, size_t n, const Allocator& a = Allocator{})
                : basic_utf16_string(string_view{p, n}, a)
            {
            }
            basic_utf16_string(size_t n, char16_t c, const Allocator& a = Allocator{}) : basic_utf16_string(a)
            {
                resize(n, c);
            }
            basic_utf16_string(const basic_utf16_string& o) : basic_utf16_string(o.view(), o.alloc_) {}
            basic_utf16_string(basic_utf16_string&& o) noexcept : n_(o.n_), cap_(o.cap_), alloc_(o.alloc_)
            {
                if (o.is_inline())
                {
                    memcpy(data_.inline_units, o.data_.inline_units, (n_ + 1) * sizeof(char16_t));
                }
                else
                {
                    data_.large = o.data_.large;
                }
                o.n_ = 0;
                o.cap_ = short_capacity;
                o.data_.inline_units[0] = 0;
            }
            ~basic_utf16_string() { release(); }
            basic_utf16_string& operator=(const basic_utf16_string& o)
            {
                if (this != &o)
                {
                    basic_utf16_string c(o);
                    *this = static_cast<basic_utf16_string&&>(c);
                }
                return *this;
            }
            basic_utf16_string& operator=(basic_utf16_string&& o) noexcept
            {
                if (this != &o)
                {
                    release();
                    alloc_ = o.alloc_;
                    n_ = o.n_;
                    cap_ = o.cap_;
                    if (o.is_inline())
                    {
                        memcpy(data_.inline_units, o.data_.inline_units, (n_ + 1) * sizeof(char16_t));
                    }
                    else
                    {
                        data_.large = o.data_.large;
                    }
                    o.n_ = 0;
                    o.cap_ = short_capacity;
                    o.data_.inline_units[0] = 0;
                }
                return *this;
            }
            void swap(basic_utf16_string& o) noexcept
            {
                if (this == &o)
                {
                    return;
                }
                basic_utf16_string c(static_cast<basic_utf16_string&&>(*this));
                *this = static_cast<basic_utf16_string&&>(o);
                o = static_cast<basic_utf16_string&&>(c);
            }
            Allocator get_allocator() const noexcept { return alloc_; }
            void reserve(size_t c)
            {
                if (c <= cap_)
                {
                    return;
                }
                if (c > max_size())
                {
                    throw std::bad_alloc();
                }
                auto p = static_cast<char16_t*>(alloc_.allocate((c + 1) * sizeof(char16_t), alignof(char16_t)));
                memcpy(p, ptr(), (n_ + 1) * sizeof(char16_t));
                release();
                data_.large = p;
                cap_ = c;
            }
            void assign(string_view v)
            {
                if (v.size() > cap_)
                {
                    basic_utf16_string t(alloc_);
                    t.reserve(v.size());
                    t.assign(v);
                    *this = static_cast<basic_utf16_string&&>(t);
                    return;
                }
                if (v.size())
                {
                    memmove(ptr(), v.data(), v.size() * sizeof(char16_t));
                }
                n_ = v.size();
                ptr()[n_] = 0;
            }
            void append(string_view v)
            {
                const size_t count = v.size(), old = n_;
                if (count <= cap_ - old)
                {
                    auto p = ptr();
                    const size_t total = old + count;
                    n_ = total;
                    if (count)
                    {
                        memmove(p + old, v.data(), count * sizeof(char16_t));
                    }
                    p[total] = 0;
                    return;
                }
                append_grow(v);
            }

          private:
            CE_NOINLINE void append_grow(string_view v)
            {
                const size_t count = v.size(), old = n_;
                if (count > max_size() - old)
                {
                    throw std::bad_alloc();
                }
                auto source = reinterpret_cast<uintptr_t>(v.data()), base = reinterpret_cast<uintptr_t>(ptr());
                bool alias = source >= base && source - base <= old * sizeof(char16_t);
                size_t offset = alias ? (source - base) / sizeof(char16_t) : 0;
                size_t total = old + count;
                reserve(grown(total));
                auto p = ptr();
                if (count)
                {
                    memmove(p + old, alias ? p + offset : v.data(), count * sizeof(char16_t));
                }
                n_ = total;
                p[total] = 0;
            }

          public:
            void push_back(char16_t c)
            {
                if (n_ == cap_)
                {
                    reserve(grown(n_ + 1));
                }
                ptr()[n_++] = c;
                ptr()[n_] = 0;
            }
            void pop_back() noexcept { ptr()[--n_] = 0; }
            void resize(size_t n, char16_t fill = 0)
            {
                if (n > cap_)
                {
                    reserve(grown(n));
                }
                for (size_t i = n_; i < n; ++i)
                {
                    ptr()[i] = fill;
                }
                n_ = n;
                ptr()[n_] = 0;
            }
            template<class Operation>
            void resize_and_overwrite(size_t n, Operation op)
            {
                if (n > cap_)
                {
                    reserve(grown(n));
                }
                size_t written = op(ptr(), n);
                if (written > n)
                {
                    abort();
                }
                n_ = written;
                ptr()[n_] = 0;
            }
            void insert(size_t pos, string_view v)
            {
                if (pos > n_)
                {
                    throw bounds_error{};
                }
                basic_utf16_string copy(v, alloc_);
                if (v.size() > max_size() - n_)
                {
                    throw std::bad_alloc();
                }
                size_t total = n_ + copy.size();
                if (total > cap_)
                {
                    reserve(grown(total));
                }
                memmove(ptr() + pos + copy.size(), ptr() + pos, (n_ - pos + 1) * sizeof(char16_t));
                if (copy.size())
                {
                    memcpy(ptr() + pos, copy.data(), copy.size() * sizeof(char16_t));
                }
                n_ = total;
            }
            void erase(size_t pos, size_t count = SIZE_MAX)
            {
                if (pos > n_)
                {
                    throw bounds_error{};
                }
                size_t n = n_ - pos;
                if (count < n)
                {
                    n = count;
                }
                memmove(ptr() + pos, ptr() + pos + n, (n_ - pos - n + 1) * sizeof(char16_t));
                n_ -= n;
            }
            void clear() noexcept
            {
                n_ = 0;
                ptr()[0] = 0;
            }
            char16_t* data() noexcept { return ptr(); }
            const char16_t* data() const noexcept { return ptr(); }
            const char16_t* c_str() const noexcept { return ptr(); }
            size_t size() const noexcept { return n_; }
            size_t capacity() const noexcept { return cap_; }
            bool empty() const noexcept { return n_ == 0; }
            char16_t& operator[](size_t i) noexcept { return ptr()[i]; }
            const char16_t& operator[](size_t i) const noexcept { return ptr()[i]; }
            string_view view() const noexcept { return {ptr(), n_}; }
            operator string_view() const noexcept { return view(); }
            size_t find(string_view v, size_t pos = 0) const noexcept { return view().find(v, pos); }
            bool operator==(string_view v) const noexcept { return view() == v; }
            bool operator==(const basic_utf16_string& v) const noexcept { return view() == v.view(); }
        };
        template<class Allocator = heap_allocator>
        using basic_string = basic_utf16_string<Allocator>;
        using string = basic_string<>;
        using utf16_string = string;
        using utf16_string_view = string_view;

        enum class unicode_error
        {
            none,
            invalid_utf8,
            invalid_surrogate,
            embedded_null,
            unsupported_input_flags
        };
        struct unicode_result
        {
            unicode_error error = unicode_error::none;
            size_t offset = 0;
            explicit operator bool() const noexcept { return error == unicode_error::none; }
        };
        namespace detail
        {
            inline unicode_result decode_utf16(string_view v, size_t& i, uint32_t& cp) noexcept
            {
                size_t start = i;
                cp = v[i++];
                if (cp >= 0xd800 && cp <= 0xdbff)
                {
                    if (i == v.size() || v[i] < 0xdc00 || v[i] > 0xdfff)
                    {
                        return {unicode_error::invalid_surrogate, start};
                    }
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (v[i++] - 0xdc00);
                }
                else if (cp >= 0xdc00 && cp <= 0xdfff)
                {
                    return {unicode_error::invalid_surrogate, start};
                }
                return {};
            }
            inline unicode_result decode_utf8(utf8_string_view v, size_t& i, uint32_t& cp) noexcept
            {
                size_t start = i;
                auto c = static_cast<unsigned char>(v[i++]);
                if (c < 0x80)
                {
                    cp = c;
                    return {};
                }
                unsigned n;
                uint32_t minimum;
                if (c >= 0xc2 && c <= 0xdf)
                {
                    n = 1;
                    cp = c & 31;
                    minimum = 0x80;
                }
                else if (c >= 0xe0 && c <= 0xef)
                {
                    n = 2;
                    cp = c & 15;
                    minimum = 0x800;
                }
                else if (c >= 0xf0 && c <= 0xf4)
                {
                    n = 3;
                    cp = c & 7;
                    minimum = 0x10000;
                }
                else
                {
                    return {unicode_error::invalid_utf8, start};
                }
                if (n > v.size() - i)
                {
                    return {unicode_error::invalid_utf8, start};
                }
                for (unsigned k = 0; k < n; ++k)
                {
                    auto b = static_cast<unsigned char>(v[i++]);
                    if ((b & 0xc0) != 0x80)
                    {
                        return {unicode_error::invalid_utf8, start};
                    }
                    cp = (cp << 6) | (b & 63);
                }
                if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                {
                    return {unicode_error::invalid_utf8, start};
                }
                return {};
            }
        } // namespace detail
        namespace detail
        {
            inline bool text_overlap(const void* a, size_t an, const void* b, size_t bn) noexcept
            {
                auto x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
                return x <= y ? y - x < an : x - y < bn;
            }
            inline unicode_result utf8_length(string_view input, size_t& count)
            {
                count = 0;
                for (size_t i = 0; i < input.size();)
                {
                    uint32_t cp;
                    auto r = decode_utf16(input, i, cp);
                    if (!r)
                    {
                        return r;
                    }
                    size_t n = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
                    if (count > SIZE_MAX - n)
                    {
                        throw std::bad_alloc();
                    }
                    count += n;
                }
                return {};
            }
            inline unicode_result utf16_length(utf8_string_view input, size_t& count)
            {
                count = 0;
                for (size_t i = 0; i < input.size();)
                {
                    uint32_t cp;
                    auto r = decode_utf8(input, i, cp);
                    if (!r)
                    {
                        return r;
                    }
                    size_t n = cp > 0xffff ? 2 : 1;
                    if (count > SIZE_MAX - n)
                    {
                        throw std::bad_alloc();
                    }
                    count += n;
                }
                return {};
            }
        } // namespace detail
        // Validate before resizing: malformed input leaves the destination unchanged.
        template<class Output>
        unicode_result to_utf8(string_view input, Output& output)
        {
            size_t count = 0;
            auto validation = detail::utf8_length(input, count);
            if (!validation)
            {
                return validation;
            }
            if (detail::text_overlap(input.data(), input.size() * sizeof(char16_t), output.data(),
                                     output.capacity() + 1))
            {
                Output temporary(output.get_allocator());
                auto r = to_utf8(input, temporary);
                if (r)
                {
                    output.swap(temporary);
                }
                return r;
            }
            output.resize_and_overwrite(count,
                                        [&](char* p, size_t) noexcept
                                        {
                                            size_t n = 0;
                                            for (size_t i = 0; i < input.size();)
                                            {
                                                uint32_t cp;
                                                (void)detail::decode_utf16(input, i, cp);
                                                if (cp < 0x80)
                                                {
                                                    p[n++] = static_cast<char>(cp);
                                                }
                                                else if (cp < 0x800)
                                                {
                                                    p[n++] = static_cast<char>(0xc0 | (cp >> 6));
                                                    p[n++] = static_cast<char>(0x80 | (cp & 63));
                                                }
                                                else if (cp < 0x10000)
                                                {
                                                    p[n++] = static_cast<char>(0xe0 | (cp >> 12));
                                                    p[n++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
                                                    p[n++] = static_cast<char>(0x80 | (cp & 63));
                                                }
                                                else
                                                {
                                                    p[n++] = static_cast<char>(0xf0 | (cp >> 18));
                                                    p[n++] = static_cast<char>(0x80 | ((cp >> 12) & 63));
                                                    p[n++] = static_cast<char>(0x80 | ((cp >> 6) & 63));
                                                    p[n++] = static_cast<char>(0x80 | (cp & 63));
                                                }
                                            }
                                            return n;
                                        });
            return {};
        }
        template<class Output>
        unicode_result to_utf16(utf8_string_view input, Output& output)
        {
            size_t count = 0;
            auto validation = detail::utf16_length(input, count);
            if (!validation)
            {
                return validation;
            }
            if (detail::text_overlap(input.data(), input.size(), output.data(),
                                     (output.capacity() + 1) * sizeof(char16_t)))
            {
                Output temporary(output.get_allocator());
                auto r = to_utf16(input, temporary);
                if (r)
                {
                    output.swap(temporary);
                }
                return r;
            }
            output.resize_and_overwrite(count,
                                        [&](char16_t* p, size_t) noexcept
                                        {
                                            size_t n = 0;
                                            for (size_t i = 0; i < input.size();)
                                            {
                                                uint32_t cp;
                                                (void)detail::decode_utf8(input, i, cp);
                                                if (cp <= 0xffff)
                                                {
                                                    p[n++] = static_cast<char16_t>(cp);
                                                }
                                                else
                                                {
                                                    cp -= 0x10000;
                                                    p[n++] = static_cast<char16_t>(0xd800 + (cp >> 10));
                                                    p[n++] = static_cast<char16_t>(0xdc00 + (cp & 1023));
                                                }
                                            }
                                            return n;
                                        });
            return {};
        }
        template<class Allocator = heap_allocator>
        class basic_utf8_conversion_cache
        {
            basic_utf16_string<Allocator> source_;
            basic_utf8_string<Allocator> bytes_;
            bool initialized_ = false;

          public:
            basic_utf8_conversion_cache() = default;
            explicit basic_utf8_conversion_cache(const Allocator& a) : source_(a), bytes_(a) {}
            unicode_result update(string_view source)
            {
                if (initialized_ && source_.view() == source)
                {
                    return {};
                }
                // Own both representations; no cached pointer into caller storage.
                size_t count;
                auto r = detail::utf8_length(source, count);
                if (!r)
                {
                    return r;
                }
                // Both reservations finish before either representation is changed.
                source_.reserve(source.size());
                bytes_.reserve(count);
                source_.assign(source);
                (void)to_utf8(source_.view(), bytes_);
                initialized_ = true;
                return {};
            }
            const basic_utf8_string<Allocator>& text() const noexcept { return bytes_; }
            const char* c_str() const noexcept { return bytes_.c_str(); }
        };
        using utf8_conversion_cache = basic_utf8_conversion_cache<>;
        // One state per persistent widget. Active composition owns the UTF-8 buffer.
        template<class Allocator = heap_allocator>
        class basic_utf16_input_state
        {
            basic_utf16_string<Allocator> committed_;
            basic_utf8_string<Allocator> bytes_;
            bool initialized_ = false;

          public:
            basic_utf16_input_state() = default;
            explicit basic_utf16_input_state(const Allocator& a) : committed_(a), bytes_(a) {}
            bool conflict = false;
            unicode_result sync(string_view source, bool active)
            {
                if (initialized_ && committed_.view() == source)
                {
                    conflict = false;
                    return {};
                }
                if (active && initialized_)
                {
                    conflict = true;
                    return {};
                }
                for (size_t i = 0; i < source.size(); ++i)
                {
                    if (!source[i])
                    {
                        return {unicode_error::embedded_null, i};
                    }
                }
                size_t count;
                auto r = detail::utf8_length(source, count);
                if (!r)
                {
                    return r;
                }
                committed_.reserve(source.size());
                bytes_.reserve(count);
                committed_.assign(source);
                (void)to_utf8(committed_.view(), bytes_);
                initialized_ = true;
                conflict = false;
                return {};
            }
            template<class Output>
            unicode_result commit(Output& output)
            {
                if (conflict)
                {
                    return {}; // Caller must resolve external edits before applying local input.
                }
                size_t count;
                auto r = detail::utf16_length(bytes_.view(), count);
                if (!r)
                {
                    return r;
                }
                for (size_t i = 0; i < bytes_.size(); ++i)
                {
                    if (!bytes_[i])
                    {
                        return {unicode_error::embedded_null, i};
                    }
                }
                committed_.reserve(count);
                output.reserve(count);
                (void)to_utf16(bytes_.view(), output);
                committed_.assign(output.view());
                return {};
            }
            basic_utf8_string<Allocator>& buffer() noexcept { return bytes_; }
        };
        using utf16_input_state = basic_utf16_input_state<>;
    } // namespace ce
} // export
// CE_MODULE_BODY_END
