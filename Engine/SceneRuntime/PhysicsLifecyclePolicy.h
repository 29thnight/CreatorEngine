#pragma once
#include "../Physics/PhysicsTypes.h"

namespace ce::physics
{
// Void lifecycle hooks cannot return a failure to the scene's ownership drain.
// Report the original failure before quiescing physics; detach must succeed before release.
template<class Detach, class Stop, class Report>
result<void> RetirePhysicsOwner(Detach&& detach, Stop&& stop, Report&& report)
{
    auto removed = detach();
    if (removed) return {};

    report(removed.error());
    auto stopped = stop();
    if (!stopped) return stopped;

    return detach();
}

// Component::SetEnabled publishes its local flag before invoking the hook.
// Restore that flag without invoking another lifecycle hook on failed SDK transition.
template<class Apply, class Restore, class Report>
result<void> ApplyPhysicsEnabledTransition(bool enabled, Apply&& apply, Restore&& restore, Report&& report)
{
    auto changed = apply(enabled);
    if (changed) return {};

    restore(!enabled);
    report(changed.error());
    return changed;
}
}
