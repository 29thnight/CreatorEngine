#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Runtime
{
    // Engine-owned contract: synchronous OnTickSimulate(float fixedDelta), before physics.
    // Presentation frames never determine how often a script updates.
    struct SimulationTick
    {
        static constexpr double seconds = 1.0 / 60.0;
        static constexpr std::uint32_t max_catchup = 4;
        double pending = 0;
        double dropped = 0;
        std::uint64_t committed = 0;

        bool Admit(double elapsed) noexcept
        {
            if (!std::isfinite(elapsed) || elapsed < 0) return false;
            const double accepted = (std::min)(elapsed, seconds * max_catchup - pending);
            pending += accepted;
            const double discarded = elapsed - accepted;
            dropped = discarded > (std::numeric_limits<double>::max)() - dropped
                ? (std::numeric_limits<double>::max)() : dropped + discarded;
            return true;
        }
        bool Ready() const noexcept { return pending + 1e-12 >= seconds; }
        void Commit() noexcept { pending = (std::max)(0.0, pending - seconds); ++committed; }
        double Alpha() const noexcept { return std::clamp(pending / seconds, 0.0, 1.0); }
    };
}
