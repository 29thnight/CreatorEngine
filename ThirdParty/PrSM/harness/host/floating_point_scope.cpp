#include "floating_point_scope.h"

#include <exception>
#include <stdexcept>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

namespace prsm::host {
FloatingPointScope::FloatingPointScope()
{
#if defined(__x86_64__) || defined(_M_X64)
    mxcsr_ = _mm_getcsr();
    if (std::fegetenv(&saved_) != 0)
        throw std::runtime_error("cannot save floating-point environment");
    std::fenv_t ignored{};
    if (std::feholdexcept(&ignored) != 0 || std::fesetround(FE_TONEAREST) != 0) {
        std::fesetenv(&saved_);
        _mm_setcsr(mxcsr_);
        throw std::runtime_error("cannot establish floating-point environment");
    }
    // SSE rounding nearest, all exception masks set, flags clear, FTZ/DAZ off.
    // x86-64 generated scalar operations use SSE rather than extended x87.
    _mm_setcsr(0x1f80u);
#else
    throw std::runtime_error("floating-point host profile requires x86-64");
#endif
}

FloatingPointScope::~FloatingPointScope() noexcept
{
#if defined(__x86_64__) || defined(_M_X64)
    const int restored = std::fesetenv(&saved_);
    _mm_setcsr(mxcsr_);
    if (restored != 0) std::terminate();
#endif
}
}
