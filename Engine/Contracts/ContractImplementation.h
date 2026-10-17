#pragma once

// Included after the module's import std, or after the parser/probe adapter's
// classic headers. Configuration and standard-library dependencies are supplied
// explicitly by those entry points; a missing configuration is an error.

namespace ce::contract_detail
{
    [[noreturn]] inline void fail(const char* expression, std::source_location location) noexcept
    {
        std::fprintf(stderr, "Contract violation: %s\n%s(%u,%u): %s\n", expression,
            location.file_name(), location.line(), location.column(), location.function_name());
        std::fflush(stderr);
        std::abort();
    }

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
                fail(expression, location);
            }
        }
        catch (...)
        {
            fail(expression, location);
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
