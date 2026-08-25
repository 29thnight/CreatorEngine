// mathematics/config.hpp — compiler, ISA, and language-standard detection.
// Every other Mathematics header includes this first. See docs/SPIKE-RESULTS.md for
// the measurements that drove these choices.
#ifndef MATHEMATICS_CONFIG_HPP
#define MATHEMATICS_CONFIG_HPP

#include <version>
#include <type_traits>   // std::is_constant_evaluated (MATHEMATICS_IF_CONSTEVAL fallback)

// ---------------------------------------------------------------- standard
#if defined(_MSVC_LANG)
#  define MATHEMATICS_CPLUSPLUS _MSVC_LANG
#else
#  define MATHEMATICS_CPLUSPLUS __cplusplus
#endif

#if MATHEMATICS_CPLUSPLUS < 202002L
#  error "Mathematics requires C++20 or later (MSVC: /std:c++20, others: -std=c++20)."
#endif

#define MATHEMATICS_HAS_CPP23 (MATHEMATICS_CPLUSPLUS >= 202302L)

// C++23 features are opt-in per-feature: MSVC and Clang adopted them unevenly,
// so each is gated on its own feature-test macro rather than on the standard.
#if MATHEMATICS_HAS_CPP23 && defined(__cpp_if_consteval) && __cpp_if_consteval >= 202106L
#  define MATHEMATICS_HAS_IF_CONSTEVAL 1
#else
#  define MATHEMATICS_HAS_IF_CONSTEVAL 0
#endif

#if MATHEMATICS_HAS_CPP23 && defined(__cpp_multidimensional_subscript) \
    && __cpp_multidimensional_subscript >= 202110L
#  define MATHEMATICS_HAS_MULTIDIM_SUBSCRIPT 1   // enables m[r, c]
#else
#  define MATHEMATICS_HAS_MULTIDIM_SUBSCRIPT 0
#endif

#if MATHEMATICS_HAS_CPP23 && defined(__cpp_lib_mdspan) \
    && __cpp_lib_mdspan >= 202207L
#  define MATHEMATICS_HAS_MDSPAN 1
#else
#  define MATHEMATICS_HAS_MDSPAN 0
#endif

// ---------------------------------------------------------------- compiler
#if defined(__clang__)
#  define MATHEMATICS_COMPILER_CLANG 1
#elif defined(_MSC_VER)
#  define MATHEMATICS_COMPILER_MSVC 1
#elif defined(__GNUC__)
#  define MATHEMATICS_COMPILER_GCC 1
#endif

#ifndef MATHEMATICS_COMPILER_CLANG
#  define MATHEMATICS_COMPILER_CLANG 0
#endif
#ifndef MATHEMATICS_COMPILER_MSVC
#  define MATHEMATICS_COMPILER_MSVC 0
#endif
#ifndef MATHEMATICS_COMPILER_GCC
#  define MATHEMATICS_COMPILER_GCC 0
#endif

// True only for genuine MSVC, where the intrinsic vector types are magic unions
// (__m128 with .m128_f32[], __n128 with .n128_f32[]) rather than native vector
// types. Lane access must go through those members. clang-cl defines _MSC_VER
// too but follows Clang semantics, where the same types are subscriptable.
#define MATHEMATICS_MSVC_INTRINSIC_UNION (MATHEMATICS_COMPILER_MSVC && !MATHEMATICS_COMPILER_CLANG)

// ---------------------------------------------------------- inline & callconv
#if MATHEMATICS_COMPILER_MSVC || (MATHEMATICS_COMPILER_CLANG && defined(_MSC_VER))
#  define MATHEMATICS_INLINE __forceinline
#  define MATHEMATICS_NOINLINE __declspec(noinline)
#elif MATHEMATICS_COMPILER_CLANG || MATHEMATICS_COMPILER_GCC
#  define MATHEMATICS_INLINE inline __attribute__((always_inline))
#  define MATHEMATICS_NOINLINE __attribute__((noinline))
#else
#  define MATHEMATICS_INLINE inline
#  define MATHEMATICS_NOINLINE
#endif

// The coverage build asks for the plain keyword instead. Forcing the inline
// there costs a line of coverage per function and can never earn it back: gcov
// attributes the standalone body that -fkeep-inline-functions emits to the
// signature line, nothing ever calls that copy, and the inlined copies at the
// call sites are counted against the body lines instead. Every API function
// here is MATHEMATICS_INLINE, so that was 322 of the 610 uncovered lines -- 13%
// of the measured total, and no test could have reached one of them. Inlining
// is a speed decision; the coverage build is not measuring speed. NOINLINE is
// left alone: it only ever helps attribution.
#ifdef MATHEMATICS_COVERAGE_BUILD
#  undef MATHEMATICS_INLINE
#  define MATHEMATICS_INLINE inline
#endif

// __vectorcall keeps vec_reg in xmm registers across non-inlined boundaries,
// matching DirectXMath's XM_CALLCONV. On SysV the default ABI already does this.
//
// _M_ARM64EC must be excluded explicitly: ARM64EC defines _M_X64 for x64
// source compatibility while leaving _M_ARM64 undefined, so testing only for
// those two would enable __vectorcall on a target whose vec_reg is backed by
// float32x4_t. DirectXMath's XM_CALLCONV excludes it for the same reason.
#if defined(_M_X64) && (MATHEMATICS_COMPILER_MSVC || MATHEMATICS_COMPILER_CLANG) \
    && !defined(_M_ARM64) && !defined(_M_ARM64EC) && !defined(MATHEMATICS_NO_VECTORCALL)
#  define MATHEMATICS_CALL __vectorcall
#  define MATHEMATICS_CALL_NAME "__vectorcall"
#else
#  define MATHEMATICS_CALL
#  define MATHEMATICS_CALL_NAME "default"
#endif

// ---------------------------------------------------------------- architecture
#if defined(_M_ARM64) || defined(__aarch64__) || defined(_M_ARM64EC)
#  define MATHEMATICS_ARCH_ARM64 1
#elif defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#  define MATHEMATICS_ARCH_X86 1
#endif

#ifndef MATHEMATICS_ARCH_ARM64
#  define MATHEMATICS_ARCH_ARM64 0
#endif
#ifndef MATHEMATICS_ARCH_X86
#  define MATHEMATICS_ARCH_X86 0
#endif

// ---------------------------------------------------------------- SIMD backend
// Selection is compile-time only; Mathematics performs no runtime ISA dispatch, so
// that call overhead never enters the hot path (same policy as DirectXMath).
// Force a backend with -DMATHEMATICS_FORCE_SCALAR for testing the fallback.

#if defined(MATHEMATICS_FORCE_SCALAR)
#  define MATHEMATICS_SIMD_SCALAR 1
#elif MATHEMATICS_ARCH_ARM64 && (defined(__ARM_NEON) || defined(_M_ARM64) || defined(_M_ARM64EC))
#  define MATHEMATICS_SIMD_NEON 1
#elif MATHEMATICS_ARCH_X86 && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#  define MATHEMATICS_SIMD_SSE 1
#else
#  define MATHEMATICS_SIMD_SCALAR 1
#endif

#ifndef MATHEMATICS_SIMD_SSE
#  define MATHEMATICS_SIMD_SSE 0
#endif
#ifndef MATHEMATICS_SIMD_NEON
#  define MATHEMATICS_SIMD_NEON 0
#endif
#ifndef MATHEMATICS_SIMD_SCALAR
#  define MATHEMATICS_SIMD_SCALAR 0
#endif

// --- x86 ISA level -----------------------------------------------------------
// MSVC never defines __SSE4_1__, so SSE4.1 (needed for dpps -- see
// docs/SPIKE-RESULTS.md §4) must be inferred from /arch:AVX or opted into
// explicitly with MATHEMATICS_ENABLE_SSE4, mirroring DirectXMath's _XM_SSE4_INTRINSICS_.
#if MATHEMATICS_SIMD_SSE
#  if defined(__SSE4_1__) || defined(__AVX__) || defined(MATHEMATICS_ENABLE_SSE4)
#    define MATHEMATICS_HAS_SSE4 1
#  else
#    define MATHEMATICS_HAS_SSE4 0
#  endif
#  if defined(__AVX__)
#    define MATHEMATICS_HAS_AVX 1
#  else
#    define MATHEMATICS_HAS_AVX 0
#  endif
// /arch:AVX2 implies FMA on MSVC; Clang/GCC define __FMA__ separately.
#  if defined(__FMA__) || defined(__AVX2__)
#    define MATHEMATICS_HAS_FMA 1
#  else
#    define MATHEMATICS_HAS_FMA 0
#  endif
#else
#  define MATHEMATICS_HAS_SSE4 0
#  define MATHEMATICS_HAS_AVX  0
#  if MATHEMATICS_SIMD_NEON && MATHEMATICS_ARCH_ARM64
#    define MATHEMATICS_HAS_FMA 1   // AArch64 NEON always has fused multiply-add
#  else
#    define MATHEMATICS_HAS_FMA 0
#  endif
#endif

// ---------------------------------------------------------------- attributes
#define MATHEMATICS_NODISCARD [[nodiscard]]
#define MATHEMATICS_NODISCARD_MSG(message) [[nodiscard(message)]]

// Prefer C++23's `if consteval`. On MSVC this is a performance feature, not a
// spelling preference: with the C++20 fallback the compile-time branch is still
// analyzed, and because it reads __m128's float[4] union member, MSVC represents
// the whole object as four scalars from then on -- rebuilding the vector with
// vinsertps before each operation and tearing it apart with vshufps after.
// Measured at 1.9x slower than DirectXMath on a mul_add chain, and 2.2x on dot3.
// Clang and GCC are unaffected. See docs/SPIKE-RESULTS.md.
#if MATHEMATICS_HAS_IF_CONSTEVAL
#  define MATHEMATICS_IF_CONSTEVAL if consteval
#else
#  define MATHEMATICS_IF_CONSTEVAL if (std::is_constant_evaluated())
#  if MATHEMATICS_MSVC_INTRINSIC_UNION && !defined(MATHEMATICS_NO_PERF_WARNINGS)
#    pragma message(                                                           \
        "Mathematics: building as C++20 on MSVC. Reading a lane (get_x, lane, ...) "  \
        "from a vector that outlives the read costs roughly 2x on this "       \
        "configuration; compiling as C++23 removes it. Define "                \
        "MATHEMATICS_NO_PERF_WARNINGS to silence this.")
#  endif
#endif

// ---------------------------------------------------------------- diagnostics
#define MATHEMATICS_STRINGIFY_(x) #x
#define MATHEMATICS_STRINGIFY(x) MATHEMATICS_STRINGIFY_(x)

#if MATHEMATICS_SIMD_SSE
#  define MATHEMATICS_BACKEND_NAME "SSE"
#elif MATHEMATICS_SIMD_NEON
#  define MATHEMATICS_BACKEND_NAME "NEON"
#else
#  define MATHEMATICS_BACKEND_NAME "scalar"
#endif

#endif // MATHEMATICS_CONFIG_HPP
