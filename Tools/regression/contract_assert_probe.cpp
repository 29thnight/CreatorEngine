// Configure through ContractProbeConfiguration.ps1 before standalone compilation.
// Normal run verifies evaluation counts. "assert-fail" must terminate only when
// assertions_enabled is true; "enforce-fail" and "throw" terminate in both modes.
#include "../../Engine/Utility_Framework/Contract.h"

#include "../../Engine/Utility_Framework/StandardLibrary.h"

constexpr bool ConstantEvaluation()
{
    ::ce::contracts::assert_that(
        [] {
            return true;
        },
        "constant assertion");
    ::ce::contracts::enforce_that(
        [] {
            return true;
        },
        "constant enforcement");
    return true;
}

static_assert(ConstantEvaluation());

int main(int argc, char** argv)
{
    int evaluations = 0;
    ::ce::contracts::assert_that(
        [&] {
            return ++evaluations == 1;
        },
        "lazy evaluation");
    if (evaluations != (ce::contracts::assertions_enabled ? 1 : 0))
    {
        return 1;
    }

    int enforced = 0;
    ::ce::contracts::enforce_that(
        [&] {
            return ++enforced == 1;
        },
        "always evaluated once");
    if (enforced != 1)
    {
        return 2;
    }
    const auto owner = std::make_unique<int>(42);
    ::ce::contracts::enforce_that(
        [&] {
            return static_cast<bool>(owner);
        },
        "explicit bool conversion");

    if (argc > 1 && std::string_view(argv[1]) == "assert-fail")
    {
        ::ce::contracts::assert_that(
            [] {
                return false;
            },
            "expected assertion failure");
    }
    if (argc > 1 && std::string_view(argv[1]) == "enforce-fail")
    {
        ::ce::contracts::enforce_that(
            [] {
                return false;
            },
            "expected enforced failure");
        return 3;
    }
    if (argc > 1 && std::string_view(argv[1]) == "throw")
    {
        ::ce::contracts::enforce_that(
            []() -> bool {
                throw 42;
            },
            "predicate threw");
        return 4;
    }
    return 0;
}
