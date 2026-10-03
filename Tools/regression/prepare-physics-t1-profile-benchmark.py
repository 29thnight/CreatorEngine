from pathlib import Path
import sys
repo = Path(__file__).resolve().parents[2]
s = (repo / "Engine/Physics/PhysicsScene.cpp").read_text(encoding="utf-8-sig")
old = """#if !CE_SHIPPING
                            ce::profiler().publish_thread();
#endif
                            active_batch = false;"""
new = """#if !CE_SHIPPING
                            // Keep this batch counted until publication finishes.
                            // Only collecting batches can obstruct remaining SDK work.
                            const bool concurrentPublication = m_outstanding != 0 &&
                                ce::profiler().state() == ce::recorder_state::recording;
                            if (concurrentPublication)
                                lock.unlock();

                            ce::profiler().publish_thread();

                            if (concurrentPublication)
                                lock.lock();
#endif
                            active_batch = false;"""
baseline = len(sys.argv) > 1 and sys.argv[1] == "baseline"
if baseline and s.count(new) == 1:
    s = s.replace(new, old)
elif not baseline and s.count(old) == 1:
    s = s.replace(old, new)
elif s.count(old if baseline else new) != 1:
    raise RuntimeError("Dispatcher publication boundary changed")
for name in ["PhysicsScene.h", "PhysicsTestHooks.h", "../EngineDiagnostics/ProfileScope.h"]:
    s = s.replace(f'#include "{name}"', f'#include "{(repo / "Engine/Physics" / name).resolve().as_posix()}"')
out = repo / f"Build/Obj/Phase19T1Bench/{'serial' if baseline else 'profile'}/PhysicsScene.cpp"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(s, encoding="utf-8-sig")
