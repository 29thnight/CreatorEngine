from pathlib import Path
repo = Path(__file__).resolve().parents[2]
s = (repo / 'Engine/Physics/PhysicsScene.cpp').read_text(encoding='utf-8-sig')
changes = [
    ('            m_workers.emplace_back([this, index, identity](std::stop_token stop) {',
     '            m_workers.emplace_back([this, index, identity](std::stop_token stop) {\n                s_workerDispatcher = this;'),
    ('                if (registered)\n                    ce::profiler().unregister_thread();\n#endif\n            });',
     '                if (registered)\n                    ce::profiler().unregister_thread();\n#endif\n                s_workerDispatcher = nullptr;\n            });'),
    ('                m_wake.notify_one();\n                return;',
     '                // Only release-time successors can be handed back to this worker.\n                // Run-time submissions may wait on nested work and must still wake.\n                if (s_releasingDispatcher != this || m_size > 1)\n                    m_wake.notify_one();\n                return;'),
    ('    void execute(const task_entry& entry)\n    {',
     '    inline static thread_local task_dispatcher* s_workerDispatcher = nullptr;\n    inline static thread_local task_dispatcher* s_releasingDispatcher = nullptr;\n\n    struct release_context final\n    {\n        task_dispatcher* previous = std::exchange(s_releasingDispatcher, nullptr);\n        ~release_context() { s_releasingDispatcher = previous; }\n    };\n\n    void execute(const task_entry& entry)\n    {'),
    ('            entry.task->run();\n            entry.task->release();',
     '            release_context release;\n            entry.task->run();\n            s_releasingDispatcher = s_workerDispatcher == this ? this : nullptr;\n            entry.task->release();'),
]
for old, new in changes:
    if s.count(old) != 1:
        raise RuntimeError('Dispatcher boundary changed; update isolated wake candidate')
    s = s.replace(old, new)
for name in ['PhysicsScene.h', 'PhysicsTestHooks.h', '../EngineDiagnostics/ProfileScope.h']:
    target = (repo / 'Engine/Physics' / name).resolve()
    s = s.replace(f'#include "{name}"', f'#include "{target.as_posix()}"')
out = repo / 'Build/Obj/Phase19T1Bench/wake/PhysicsScene.cpp'
out.parent.mkdir(parents=True, exist_ok=True)
s += '\n#if defined(CE_PHYSICS_WAKE_TEST)\n#include "' + (repo / 'Tools/regression/physics_t1_dispatcher_probe.inc').as_posix() + '"\n#endif\n'
out.write_text(s, encoding='utf-8-sig')
