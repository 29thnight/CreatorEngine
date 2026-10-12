"""Audit actual material measurement stages and nonempty captured model output.

CPU render-submit durations and completed real-view throughput are separate axes.
No master comparison, product budget, display cadence or latency is accepted here.
"""
import argparse
import importlib.util
import json
from pathlib import Path
from base0_artifacts import load_capture, require

spec = importlib.util.spec_from_file_location('material_profile', Path(__file__).with_name('summarize-material-profile.py'))
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


def analyze(root):
    stages = {}
    model_stage = 'model' if (root / 'model.json').is_file() else 'robot'
    for stage in ('empty', model_stage):
        result = profile.analyze(root / (stage + '.ceprof'))
        require(result['nativeRealPerformanceGateEligible'], 'Stage lacks complete native-real recorded provenance')
        require(len(result['renderGroups']) == 1, 'Mixed view/scene/settings populations in material stage')
        group = result['renderGroups'][0]
        require(group['cpuSamples'] >= 20, 'Too few native-real CPU render samples')
        live = json.loads((root / (stage + '.json')).read_text(encoding='utf-8-sig'))
        require(live['axis'] == 'completed-real-view-renders-not-display-fps' and
                live['nativeRealProvenanceVerified'] is True, 'Unknown material throughput axis')
        for boundary in ('start', 'end'):
            for owner in ('temporalProvenance', 'gpuTemporalProvenance'):
                p = live[boundary][owner]
                require(p['valid'] is True and p['frameKind'] == 'real' and p['generatedOrdinal'] == 0 and
                        p['upscaler'] == p['frameGenerator'] == 'none' and p['spatialMode'] == 'off' and
                        p['deepDvcApplied'] is False and p['resolutionState'] == 'native', 'Non-native stage boundary')
                require(int(p['viewId']) == group['viewId'] and int(p['sceneEpoch']) == group['sceneEpoch'] and
                        all(p[field] == group[field] for field in
                            ('renderWidth', 'renderHeight', 'displayWidth', 'displayHeight')),
                        'Live boundary does not describe the recorded population')
        require(live['end']['framesRendered'] > live['start']['framesRendered'], 'Stage rendered no new views')
        if stage != 'empty':
            require(live['end']['materialGeometry']['uploads'] == live['start']['materialGeometry']['uploads'] and
                    live['start']['materialGeometry']['entries'] > 0 and
                    live['start']['materialGeometry']['residentBytes'] > 0, 'Cold or reuploaded model geometry')
        stages[stage] = dict(profileSha256=result['sha256'], complete=result['complete'],
            gpuEvents=result['gpuEvents'], matchedGpuEvents=result['matchedGpuEvents'],
            cpuSamples=group['cpuSamples'], viewId=group['viewId'], sceneEpoch=group['sceneEpoch'],
            renderExtent=[group['renderWidth'], group['renderHeight']],
            cpuRenderSubmitMs=group['cpuRenderSubmitMs'],
            completedRealViewRendersPerSecond=live['completedRealViewRendersPerSecond'])
    capture = root / 'ModelCapture'
    if not capture.is_dir(): capture = root / 'RobotCapture'
    manifest, _, pixels = load_capture(capture)
    require(manifest['draws'], 'Model capture has no submitted geometry')
    foreground = sum(0 < depth < 1 for depth in pixels['depth'])
    require(foreground >= 64, 'Model capture has no visible foreground')
    model = stages[model_stage]
    require(manifest['viewId'] == model['viewId'] and manifest['sceneEpoch'] == model['sceneEpoch'] and
            [manifest['width'], manifest['height']] == model['renderExtent'],
            'Captured model view/scene/extent differs from the measured population')
    return dict(passed=True, stages=stages, renderedDepthPixels=foreground,
        capturedDraws=len(manifest['draws']), productBudgetAccepted=False, masterComparisonAccepted=False,
        physicalAcceptance=False, scope='native recorded stage identity and nonempty model capture; no image quality or performance target acceptance')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.directory)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"PASS material measurement: {result['capturedDraws']} draws, {result['renderedDepthPixels']} foreground pixels")
