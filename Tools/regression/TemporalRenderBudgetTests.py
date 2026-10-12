"""Exercise budget admission with a real capture; test limits are not product policy."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('render_budget', Path(__file__).resolve().parents[1] / 'temporal-validation/analyze-render-budget.py')
budget = importlib.util.module_from_spec(spec)
spec.loader.exec_module(budget)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    original = budget.profile.read_capture(args.capture)
    summary = budget.profile.analyze(args.capture)
    group = summary['renderGroups'][0]
    view = group['viewId']
    render = group['renderWidth'], group['renderHeight']
    display = group['displayWidth'], group['displayHeight']
    # An intentionally loose positive-control threshold validates the contract,
    # not the project's performance target. Tight control must fail separately.
    limit = group['cpuRenderSubmitMs']['p95'] + 1
    positive = budget.evaluate(args.capture, view, render, display, limit)
    assert positive['passed']
    tight = budget.evaluate(args.capture, view, render, display, group['cpuRenderSubmitMs']['p95'] / 2)
    assert not tight['passed'], 'Over-budget positive identity must still fail'
    isolated_views = []
    for selected in summary['renderGroups']:
        isolated = budget.evaluate(args.capture, selected['viewId'],
            (selected['renderWidth'], selected['renderHeight']),
            (selected['displayWidth'], selected['displayHeight']),
            selected['cpuRenderSubmitMs']['p95'] + 1)
        assert isolated['samples'] == selected['cpuSamples'], 'Other view samples pooled into budget'
        assert isolated['cpuRenderSubmitMs']['p95'] == selected['cpuRenderSubmitMs']['p95']
        isolated_views.append(selected['viewId'])
    reader = budget.profile.read_capture
    rejected = []
    for name in ('missing-resolution', 'generated', 'duplicate-cpu', 'no-cpu', 'lost', 'unfinished', 'mixed-scene'):
        damaged = copy.deepcopy(original)
        frame = next(f for f in damaged['frames'] if any(m['axis'] == 1 and m['viewId'] == view for m in f['measurements']))
        cpu = next(m for m in frame['measurements'] if m['axis'] == 1 and m['viewId'] == view)
        if name == 'missing-resolution': cpu['displayWidth'] = 0
        elif name == 'generated': cpu.update(frameKind=2, generatedOrdinal=1, frameGenerator=1)
        elif name == 'duplicate-cpu': frame['measurements'].append(copy.deepcopy(cpu))
        elif name == 'no-cpu':
            for f in damaged['frames']: f['measurements'] = [m for m in f['measurements'] if m['axis'] != 1]
        elif name == 'lost': frame['dropped'] += 1
        elif name == 'unfinished': damaged['complete'] = False
        elif name == 'mixed-scene': cpu['sceneEpoch'] += 1
        budget.profile.read_capture = lambda _: damaged
        try:
            try:
                budget.evaluate(args.capture, view, render, display, limit)
            except ValueError:
                rejected.append(name)
            else:
                raise AssertionError('Mutation accepted: ' + name)
        finally:
            budget.profile.read_capture = reader
    for name, v, r, d, l, minimum in (
        ('wrong-view', max(g['viewId'] for g in summary['renderGroups']) + 1, render, display, limit, 20),
        ('wrong-render', view, (render[0] + 1, render[1]), display, limit, 20),
        ('wrong-display', view, render, (display[0] + 1, display[1]), limit, 20),
        ('nan-policy', view, render, display, float('nan'), 20),
        ('zero-policy', view, render, display, 0, 20),
        ('insufficient-samples', view, render, display, limit, positive['samples'] + 1)):
        try:
            budget.evaluate(args.capture, v, r, d, l, minimum)
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError('Invalid budget policy accepted: ' + name)
    result = dict(passed=True, captureSha256=original['sha256'], rejected=rejected,
        isolatedViews=isolated_views,
        tightBudgetRejected=True, productBudgetAccepted=False,
        scope='actual capture admission with analysis mutations and test thresholds; no product performance target accepted')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'PASS budget admission: {len(rejected)} rejections and over-budget control')


if __name__ == '__main__':
    main()
