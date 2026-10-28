#pragma once
#include "../include/ce/memory.h"
namespace ce
{
    // Stable unsigned-key radix sort; no throwing moves/assignments or key
    // extraction.
    template<class T, class Key, class Allocator = heap_allocator>
    void radix_sort(T* data, size_t size, Key key, const Allocator& resource = Allocator{})
    {
        using K = decltype(key(*data));
        static_assert((sizeof(K) == 4 || sizeof(K) == 8) && K(-1) > K(0));
        static_assert(detail::nothrow_move<T>);
        static_assert(noexcept(*data = static_cast<T&&>(*data)));
        static_assert(noexcept(key(*data)));
        if (size < 2)
            return;
        Allocator alloc = resource;
        auto bytes = detail::checked_bytes(size, sizeof(T));
        auto temp = static_cast<T*>(alloc.allocate(bytes, alignof(T)));
        // Small ranges amortize counters poorly. Stable insertion runs followed by
        // merges use the same scratch allocation and preserve the unsigned-key order.
        if (size < 1024)
        {
            constexpr size_t run = 16;
            for (size_t start = 0; start < size; start += run)
            {
                auto end = start + run < size ? start + run : size;
                for (size_t i = start + 1; i < end; ++i)
                {
                    T value(static_cast<T&&>(data[i]));
                    size_t j = i;
                    while (j > start && key(value) < key(data[j - 1]))
                    {
                        data[j] = static_cast<T&&>(data[j - 1]);
                        --j;
                    }
                    data[j] = static_cast<T&&>(value);
                }
            }
            if (size <= run)
            {
                alloc.deallocate(temp, bytes, alignof(T));
                return;
            }
            T* src = data;
            T* dst = temp;
            bool construct = true;
            for (size_t width = run; width < size; width *= 2)
            {
                for (size_t start = 0; start < size; start += 2 * width)
                {
                    size_t mid = start + width < size ? start + width : size;
                    size_t end = start + 2 * width < size ? start + 2 * width : size;
                    auto merge = [&]<bool Construct>()
                    {
                        size_t left = start, right = mid, out = start;
                        auto write = [&](size_t from)
                        {
                            if constexpr (Construct)
                                ::new (static_cast<void*>(dst + out)) T(static_cast<T&&>(src[from]));
                            else
                                dst[out] = static_cast<T&&>(src[from]);
                            ++out;
                        };
                        while (left < mid && right < end)
                            write(key(src[right]) < key(src[left]) ? right++ : left++);
                        while (left < mid)
                            write(left++);
                        while (right < end)
                            write(right++);
                    };
                    if (construct)
                        merge.template operator()<true>();
                    else
                        merge.template operator()<false>();
                }
                auto swap = src;
                src = dst;
                dst = swap;
                construct = false;
            }
            if (src != data)
                for (size_t i = 0; i < size; ++i)
                    data[i] = static_cast<T&&>(src[i]);
            for (size_t i = size; i; --i)
                temp[i - 1].~T();
            alloc.deallocate(temp, bytes, alignof(T));
            return;
        }
        for (size_t pass = 0; pass < sizeof(K); ++pass)
        {
            size_t counts[256]{};
            T* src = pass % 2 ? temp : data;
            T* dst = pass % 2 ? data : temp;
            for (size_t i = 0; i < size; ++i)
                ++counts[(uint64_t(key(src[i])) >> (pass * 8)) & 255];
            size_t total = 0;
            for (size_t i = 0; i < 256; ++i)
            {
                auto n = counts[i];
                counts[i] = total;
                total += n;
            }
            for (size_t i = 0; i < size; ++i)
            {
                auto bucket = (uint64_t(key(src[i])) >> (pass * 8)) & 255;
                auto pos = counts[bucket]++;
                if (!pass)
                    ::new (static_cast<void*>(dst + pos)) T(static_cast<T&&>(src[i]));
                else
                    dst[pos] = static_cast<T&&>(src[i]);
            }
        }
        for (size_t i = size; i; --i)
            temp[i - 1].~T();
        alloc.deallocate(temp, bytes, alignof(T));
    }
} // namespace ce
