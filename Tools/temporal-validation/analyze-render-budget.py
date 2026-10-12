"""Apply an explicit CPU render-submit budget to one recorded native-real view.

CPU recording duration is separate from simulation frame time, GPU pass time,
Present cadence and input-to-photon latency. No default product budget is set.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path

spec = importlib.util.spec_from_file_location('profile_summary', Path(__file__).resolve().parents[1] / 'regression/summarize-material-profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def evaluate(path, view, render, display, limit, minimum=20):
    profile.require(math.isfinite(limit) and limit > 0 and minimum >= 20, 'Invalid explicit budget/sample policy')
    profile.require(view > 0 and all(x > 0 for x in render + display), 'Expected view/extents must be explicit')
    report = profile.analyze(path)
    profile.require(report['nativeRealPerformanceGateEligible'], 'Incomplete, missing, mixed or non-native-real measurement proof')
    # Scene and Game may both render in one capture. An explicit view selects
    # its population; never pool their CPU samples or inherit another view's cost.
    groups = [g for g in report['renderGroups'] if g['viewId'] == view]
    profile.require(len(groups) == 1, 'Mixed view/scene/settings/extents; do not pool a budget population')
    group = groups[0]
    profile.require(group['viewId'] == view and (group['renderWidth'], group['renderHeight']) == render and
                    (group['displayWidth'], group['displayHeight']) == display, 'Budget view/extents disagree with recording')
    capture = profile.read_capture(path)
    samples, identities = [], set()
    for frame in capture['frames']:
        for sample in frame['measurements']:
            if sample['axis'] != 1 or sample['viewId'] != view:
                continue
            key = tuple(sample[k] for k in ('viewId', 'sceneEpoch', 'realFrameId', 'publicationFrameId', 'submissionId'))
            profile.require(key not in identities, 'Duplicate real-view CPU submission; budget population is ambiguous')
            identities.add(key)
            samples.append((sample['end'] - sample['begin']) * 1000 / capture['hz'])
    profile.require(len(samples) >= minimum, 'Insufficient independent CPU submissions')
    distribution = profile.stats(samples)
    return dict(schema='temporal.render-budget.v1', passed=distribution['p95'] <= limit,
        captureSha256=capture['sha256'], viewId=view, sceneEpoch=group['sceneEpoch'],
        renderExtent=list(render), displayExtent=list(display), frameKind='real',
        axis='per-view-cpu-render-submit-duration', samples=len(samples), cpuRenderSubmitMs=distribution,
        policy=dict(maximumP95CpuRenderSubmitMs=limit, minimumSamples=minimum),
        simulationFrameTimeAcceptance=False, gpuFrameTimeAcceptance=False,
        presentationPacingAcceptance=False, latencyAcceptance=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--view', type=int, required=True)
    parser.add_argument('--render', type=int, nargs=2, required=True)
    parser.add_argument('--display', type=int, nargs=2, required=True)
    parser.add_argument('--maximum-p95-cpu-ms', type=float, required=True)
    parser.add_argument('--minimum-samples', type=int, default=20)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = evaluate(args.capture, args.view, tuple(args.render), tuple(args.display), args.maximum_p95_cpu_ms, args.minimum_samples)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
    profile.require(result['passed'], 'CPU render-submit p95 exceeds the explicit budget')


if __name__ == '__main__':
    main()
