#pragma once
#include "SimulationTick.h"
#include "SimulationExchange.h"

namespace Runtime
{
    enum class TickExecutionError
    {
        None, Command, Managed, Context, Physics, Capture, Commit
    };

    // Shared by the presentation host and the render-free host. No host can
    // publish an unfinished tick, change the managed/physics order, or give the
    // physics SDK a second accumulator through this transaction.
    template<class Apply, class Managed, class Valid, class Physics, class Publish, class Capture>
    TickExecutionError ExecuteSimulationTick(SimulationTick& clock, SimulationExchange& exchange,
        Apply&& apply, Managed&& managed, Valid&& valid, Physics&& physics,
        Publish&& publish, Capture&& capture)
    {
        const auto epoch = exchange.Status().epoch;
        const auto fail = [&](TickExecutionError error)
        {
            exchange.Fail();
            return error;
        };

        // A callback can throw after Begin has reserved the tick. Retire the
        // exchange on every exceptional exit before the caller handles it.
        struct Guard
        {
            SimulationExchange& exchange;
            bool committed = false;
            ~Guard() { if (!committed) exchange.Fail(); }
        } guard{exchange};

        if (exchange.Begin(clock.committed + 1, apply) != TickResult::Ok)
            return fail(TickExecutionError::Command);
        if (!managed(static_cast<float>(SimulationTick::seconds)))
            return fail(TickExecutionError::Managed);
        if (!valid() || exchange.Status().epoch != epoch)
            return fail(TickExecutionError::Context);
        if (!physics(static_cast<float>(SimulationTick::seconds)))
            return fail(TickExecutionError::Physics);

        publish();
        if (exchange.Prepare(capture) != TickResult::Ok)
            return fail(TickExecutionError::Capture);
        if (!valid() || exchange.Status().epoch != epoch)
            return fail(TickExecutionError::Context);

        clock.Commit();
        if (exchange.Commit() != TickResult::Ok)
            return fail(TickExecutionError::Commit);
        guard.committed = true;
        return TickExecutionError::None;
    }
}
