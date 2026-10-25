#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Stopwatch.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace ce
{
    class stopwatch final
    {
      public:
        stopwatch() : start_(clock_type::now()) {}

        double elapsed_milliseconds() const
        {
            const std::chrono::duration<double, std::milli> elapsed = clock_type::now() - start_;
            return elapsed.count();
        }

      private:
        using clock_type = std::chrono::high_resolution_clock;
        clock_type::time_point start_;
    };
}
