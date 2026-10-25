module;
#include <CreatorContractConfig.h>
// stderr is a C macro and remains local to this module.
#include <stdio.h>

export module Creator.Contracts;

import std;

namespace ce::contract_detail
{
    [[noreturn]] inline void fail(const char* expression, std::source_location location) noexcept
    {
        std::fprintf(stderr, "Contract violation: %s\n%s(%u,%u): %s\n", expression,
            location.file_name(), location.line(), location.column(), location.function_name());
        std::fflush(stderr);
        std::abort();
    }

}

export namespace ce::contracts
{
    inline constexpr bool assertions_enabled = ::ce::contract_detail::assertions_enabled;

    // Fixed enforce semantics. A predicate exception also terminates. No compiler
    // assumptions, observe mode or repeated evaluations are implemented.
    template<typename Predicate>
    constexpr void enforce_that(Predicate&& predicate, const char* expression = "condition",
        std::source_location location = std::source_location::current()) noexcept
    {
        try
        {
            if (!static_cast<bool>(predicate()))
            {
                ::ce::contract_detail::fail(expression, location);
            }
        }
        catch (...)
        {
            ::ce::contract_detail::fail(expression, location);
        }
    }

    // Forming the lambda does not evaluate its body. Do not put required work in
    // production assertions: Release discards the predicate invocation.
    template<typename Predicate>
    constexpr void assert_that([[maybe_unused]] Predicate&& predicate,
        [[maybe_unused]] const char* expression = "condition",
        [[maybe_unused]] std::source_location location = std::source_location::current()) noexcept
    {
        if constexpr (assertions_enabled)
        {
            enforce_that(static_cast<Predicate&&>(predicate), expression, location);
        }
    }
}
