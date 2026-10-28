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
#define CE_UTF8_MODULE_IMPLEMENTATION 1
#if defined(_M_X64) && !defined(_M_ARM64EC) && !defined(__clang__) && _MSC_VER >= 1940
#include <immintrin.h>
#include <isa_availability.h>
#elif defined(__x86_64__) && !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#endif

export module Creator.Containers:utf8_copy;
export import :memory;

// CE_MODULE_BODY_BEGIN
export {
// CE_HEADER_INCLUDE #include "memory.h"
// CE_HEADER_INCLUDE #include <string.h>
#if defined(_M_X64) && !defined(_M_ARM64EC) && !defined(__clang__) && _MSC_VER >= 1940
#define CE_UTF8_VECTOR_BACKEND 1
// CE_HEADER_INCLUDE #include <immintrin.h>
// CE_HEADER_INCLUDE #include <isa_availability.h>
#elif defined(__x86_64__) && !defined(_MSC_VER) && (defined(__GNUC__) || defined(__clang__))
#define CE_UTF8_VECTOR_BACKEND 1
// CE_HEADER_INCLUDE #include <immintrin.h>
#endif
    namespace ce
    {

        namespace detail
        {
            // Same bytes and ownership rules as memmove. Only disjoint ranges enter the
            // aligned fixed-block kernel; arbitrary alignment and overlap retain the CRT.
            inline bool utf8_copy_2048_candidate(const void* destination, const void* source) noexcept
            {
                auto d = reinterpret_cast<uintptr_t>(destination), s = reinterpret_cast<uintptr_t>(source);
                return (d & 15) == 0 && ((d - s) & 4095) < 256 && (d > s ? d - s : s - d) >= 2048;
            }
            inline bool utf8_copy_avx2_supported() noexcept
            {
#if defined(CE_UTF8_VECTOR_BACKEND) && defined(_MSC_VER)
                return __check_isa_support(__IA_SUPPORT_VECTOR256, 0);
#elif defined(CE_UTF8_VECTOR_BACKEND)
                return __builtin_cpu_supports("avx2") != 0;
#else
                return false;
#endif
            }
#if defined(CE_UTF8_VECTOR_BACKEND)
#if defined(_MSC_VER)
// ISA use is confined to a noinline kernel reached only after the runtime check.
#pragma warning(push)
#pragma warning(disable : 4752)
#define CE_UTF8_AVX2_TARGET
#else
#define CE_UTF8_AVX2_TARGET __attribute__((target("avx2")))
#endif
            template<unsigned Blocks>
            CE_UTF8_AVX2_TARGET CE_FORCEINLINE void utf8_copy_reverse_blocks(char* d, const char* s) noexcept
            {
                constexpr size_t k = (Blocks - 1) * 128;
                auto a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + k));
                auto b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + k + 32));
                auto c = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + k + 64));
                auto e = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + k + 96));
                _mm256_store_si256(reinterpret_cast<__m256i*>(d + k), a);
                _mm256_store_si256(reinterpret_cast<__m256i*>(d + k + 32), b);
                _mm256_store_si256(reinterpret_cast<__m256i*>(d + k + 64), c);
                _mm256_store_si256(reinterpret_cast<__m256i*>(d + k + 96), e);
                if constexpr (Blocks > 1)
                {
                    utf8_copy_reverse_blocks<Blocks - 1>(d, s);
                }
            }
#if defined(CE_UTF8_MODULE_IMPLEMENTATION)
#define CE_UTF8_KERNEL_INLINE
#else
#define CE_UTF8_KERNEL_INLINE inline
#endif
            CE_UTF8_AVX2_TARGET CE_UTF8_KERNEL_INLINE
#if defined(_MSC_VER)
                CE_FORCEINLINE
#else
                CE_NOINLINE
#endif
                void utf8_copy_2048_avx2(char* d, const char* s) noexcept
            {
                if ((reinterpret_cast<uintptr_t>(d) & 31) == 0)
                {
                    utf8_copy_reverse_blocks<16>(d, s);
                }
                else
                {
                    auto tail = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 2032));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(d + 2032), tail);
                    auto a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 1936));
                    auto b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 1968));
                    auto c = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 2000));
                    _mm256_store_si256(reinterpret_cast<__m256i*>(d + 1936), a);
                    _mm256_store_si256(reinterpret_cast<__m256i*>(d + 1968), b);
                    _mm256_store_si256(reinterpret_cast<__m256i*>(d + 2000), c);
                    utf8_copy_reverse_blocks<15>(d + 16, s + 16);
                    auto head = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(d), head);
                }
                _mm256_zeroupper();
            }
#undef CE_UTF8_KERNEL_INLINE
#undef CE_UTF8_AVX2_TARGET
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif
            template<bool AllowVector = true>
            CE_FORCEINLINE bool try_utf8_copy_2048(char* d, const char* s, size_t bytes) noexcept
            {
#if defined(CE_UTF8_VECTOR_BACKEND)
                if constexpr (AllowVector)
                {
                    if (bytes == 2048 && utf8_copy_2048_candidate(d, s) && utf8_copy_avx2_supported())
                    {
                        utf8_copy_2048_avx2(d, s);
                        return true;
                    }
                }
#else
                (void)d;
                (void)s;
                (void)bytes;
#endif
                return false;
            }
            template<bool AllowVector = true>
            CE_FORCEINLINE void utf8_copy_bytes(char* d, const char* s, size_t bytes) noexcept
            {
                if (bytes && !try_utf8_copy_2048<AllowVector>(d, s, bytes))
                {
                    memmove(d, s, bytes);
                }
            }
        } // namespace detail
#undef CE_UTF8_VECTOR_BACKEND

#if defined(CE_UTF8_MODULE_IMPLEMENTATION)
#define CE_UTF8_DISPATCH_INLINE
#else
#define CE_UTF8_DISPATCH_INLINE inline
#endif
        namespace detail
        {
            CE_NOINLINE CE_UTF8_DISPATCH_INLINE void utf8_copy_2048_dispatch(char* d, const char* s) noexcept
            {
                utf8_copy_bytes(d, s, 2048);
            }
        } // namespace detail
#undef CE_UTF8_DISPATCH_INLINE
    } // namespace ce
} // export
// CE_MODULE_BODY_END
#undef CE_UTF8_MODULE_IMPLEMENTATION
