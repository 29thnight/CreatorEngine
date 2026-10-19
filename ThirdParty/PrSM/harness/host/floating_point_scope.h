#pragma once

#include "prsm_abi.h"
#include <cfenv>
#include <cstdint>

namespace prsm::host {
// Bounded native x86-64 profile. Save the caller's complete floating-point
// environment, establish nearest-even with gradual underflow and masked
// exceptions, then restore the exact incoming state. Guards may nest across
// script -> engine -> script calls. This is not a panic recovery boundary.
class CE_API FloatingPointScope {
public:
    FloatingPointScope();
    ~FloatingPointScope() noexcept;
    FloatingPointScope(const FloatingPointScope&) = delete;
    FloatingPointScope& operator=(const FloatingPointScope&) = delete;

private:
    std::fenv_t saved_{};
    std::uint32_t mxcsr_ = 0;
};
}
