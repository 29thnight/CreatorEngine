from pathlib import Path
import sys

repo = Path(__file__).resolve().parents[2]
side = sys.argv[1] if len(sys.argv) == 2 else 'serial'
if side not in ('serial', 'candidate', 'merge'):
    raise ValueError('Expected serial, candidate or merge')
text = (repo / 'Engine/SceneRuntime/ScenePhysicsSimulation.cpp').read_text(encoding='utf-8-sig')
if side == 'candidate':
    boundary = '        const auto prepare = [&] {'
    submit = '        const auto finished = m_runtime->finish_step();'
    after = '        prepare();\n        ++ticks;'
    for token in (boundary, submit, after):
        if text.count(token) != 1:
            raise RuntimeError('Scheduling boundary changed; update isolated candidate')
    text = text.replace(boundary, '        const bool overlap = m_render.size() >= 256;\n' + boundary)
    text = text.replace('ce::profile_scope scope{ce::marker<"Physics.RenderPrepare">()};',
                        'ce::profile_scope scope{(overlap ? ce::marker<"Physics.InFlightRenderPrepare">() : ce::marker<"Physics.RenderPrepare">())};')
    text = text.replace(submit, '        if (overlap)\n            prepare();\n' + submit)
    text = text.replace(after, '        if (!overlap)\n            prepare();\n        ++ticks;')
if side == 'merge':
    replacements = [
        ('            m_nextRender.clear();\n            m_renderPrepared.clear();\n            for (const auto& old : m_render)',
         '            for (const auto& old : m_render)'),
        ('                value.prepared_tick = submitted.last_tick.value;\n                value.render_index = m_nextRender.size();',
         '                if (value.pose_tick == submitted.last_tick.value)\n                    continue;\n\n                value.previous = value.state.transform;'),
        ('                m_renderPrepared.push_back(&value);', ''),
        ('        prepare();\n        ++ticks;', '        m_nextRender.clear();\n\n        ++ticks;'),
        ('            if (value.prepared_tick == completedTick)\n                m_nextRender[value.render_index] = rendered;\n            else\n                m_nextRender.push_back(rendered);', '            m_nextRender.push_back(rendered);'),
        ('        // Commit convergence only after successful fetch. Preparation never changes\n        // public completed state or previous/current history on a failed step.\n        for (auto* value : m_renderPrepared)\n            if (value->pose_tick != completedTick)\n                value->previous = value->state.transform;',
         '        // Active poses are merged once; only newly inactive histories converge.\n        prepare();'),
    ]
    for original, replacement in replacements:
        if text.count(original) != 1:
            raise RuntimeError('Merge boundary changed; update isolated candidate')
        text = text.replace(original, replacement)
for original, path in [('ScenePhysicsSimulation.h', 'Engine/SceneRuntime/ScenePhysicsSimulation.h'),
                       ('../EngineDiagnostics/ProfileScope.h', 'Engine/EngineDiagnostics/ProfileScope.h')]:
    text = text.replace(f'#include "{original}"', f'#include "{(repo / path).as_posix()}"')
output = repo / f'Build/Obj/Phase19T1Bench/{side}/ScenePhysicsSimulation.cpp'
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(text, encoding='utf-8-sig')
