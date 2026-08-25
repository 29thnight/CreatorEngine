// mathematics/arch/simd_select.hpp — includes exactly one backend.
//
// Selection is entirely compile-time. Mathematics performs no runtime ISA dispatch, so
// that an indirect call never enters the hot path; this is the same policy
// DirectXMath follows. config.hpp decides which backend applies from the target
// and the compiler's own ISA macros.
#ifndef MATHEMATICS_ARCH_SIMD_SELECT_HPP
#define MATHEMATICS_ARCH_SIMD_SELECT_HPP

#include <mathematics/config.hpp>

#if MATHEMATICS_SIMD_SSE
#  include <mathematics/arch/simd_sse.hpp>
#elif MATHEMATICS_SIMD_NEON
#  include <mathematics/arch/simd_neon.hpp>
#elif MATHEMATICS_SIMD_SCALAR
#  include <mathematics/arch/simd_scalar.hpp>
#else
#  error "no SIMD backend selected; config.hpp should always pick one"
#endif

#endif // MATHEMATICS_ARCH_SIMD_SELECT_HPP
