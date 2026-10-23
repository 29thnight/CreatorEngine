#include "../../Engine/SceneRuntime/SimulationTick.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main()
{
    for (const int fps : {30, 60, 144})
    {
        Runtime::SimulationTick clock;
        for (int frame = 0; frame < fps * 10; ++frame)
        {
            assert(clock.Admit(1.0 / fps));
            int calls = 0;
            while (clock.Ready()) { ++calls; clock.Commit(); }
            assert(calls <= 4 && clock.Alpha() >= 0 && clock.Alpha() < 1);
        }
        assert(clock.committed == 600);
    }
    Runtime::SimulationTick clock;
    assert(clock.Admit(1.0 / 144) && !clock.Ready());
    const auto debt = clock.pending;
    assert(clock.Admit(0) && clock.pending == debt); // pause does not advance or discard fractional debt
    assert(!clock.Admit(-1) && !clock.Admit(std::numeric_limits<double>::quiet_NaN()));
    assert(clock.pending == debt);
    assert(clock.Admit(1));
    int catchup = 0;
    while (clock.Ready()) { ++catchup; clock.Commit(); }
    assert(catchup == 4 && clock.dropped > .9);
    clock = {}; // new scene/play epoch owns no previous debt
    assert(clock.committed == 0 && clock.pending == 0 && !clock.Ready());
    std::puts("FIXED_SIMULATION_CLOCK_OK fps=30,60,144 catchup=4 pause=pass reset=pass invalid=pass");
}
