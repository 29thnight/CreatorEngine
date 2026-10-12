"""Exercise real pixel-consumer entry points with damaged actual capture metadata.

Only manifests are cloned; original pixels and capture bytes are never modified.
The positive control checks the shared contract, not animation/material quality.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import runpy

from base0_artifacts import audit_pixel_measurement


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.capture / 'manifest.json'
    original = source.read_bytes()
    manifest = json.loads(original.decode('utf-8-sig'))
    audit_pixel_measurement(manifest)
    cases = []
    for key in ('temporalProvenanceSchemaVersion', 'frameKind', 'realFrameId', 'renderWidth',
                'renderHeight', 'displayWidth', 'displayHeight', 'generatedOrdinal', 'measurement'):
        damaged = copy.deepcopy(manifest)
        del damaged[key]
        cases.append(('missing-' + key, damaged))
    for key, value in (('frameKind', 'generated'), ('generatedOrdinal', 1),
                       ('realFrameId', 0), ('renderWidth', 0), ('displayHeight', 0),
                       ('upscaler', 'dlss'), ('frameGenerator', 'fsr'),
                       ('temporalNativeGateActive', False), ('goldenEligible', False),
                       ('spatialMode', 'sharpen'), ('deepDvcApplied', True)):
        damaged = copy.deepcopy(manifest)
        damaged[key] = value
        cases.append(('wrong-' + key, damaged))
    for key in ('frameKind', 'renderWidth', 'realFrameId'):
        damaged = copy.deepcopy(manifest)
        damaged['measurement'][key] = 'generated' if key == 'frameKind' else 0
        cases.append(('measurement-' + key, damaged))
    for key, value in (('frameKind', 'generated'), ('realFrameId', 0), ('width', 0), ('generatedOrdinal', 1)):
        damaged = copy.deepcopy(manifest)
        damaged['attachments'][0][key] = value
        cases.append(('attachment-' + key, damaged))
    animation = load('animation_visual_pixels')
    ik = load('animation_ik_visual_pixels')
    material = load('compare-material-captures')
    rejected = []
    with tempfile.TemporaryDirectory(prefix='tr0-pixel-metadata-') as temporary:
        root = Path(temporary)
        names = ('a', 'scene-on') + ik.POSES
        for name in names:
            (root / name).mkdir()
        for name, damaged in cases:
            for pose in names:
                (root / pose / 'manifest.json').write_text(json.dumps(damaged), encoding='utf-8')
            for driver, poses in ((animation, ('a',)), (ik, ik.POSES)):
                rows = [dict(command='animation.visual.probe', data={})]
                rows += [dict(command='render.pbr.capture', data=dict(directory=str(root / pose))) for pose in poses]
                (root / 'results.jsonl').write_text('\n'.join(map(json.dumps, rows)), encoding='utf-8')
                try:
                    driver.main(root)
                except (KeyError, ValueError) as error:
                    rejected.append(dict(case=name, consumer=driver.__name__, reason=str(error)))
                else:
                    raise AssertionError('Pixel consumer accepted damaged measurement: ' + name)
            saved = sys.argv
            try:
                sys.argv = ['compare-material-captures', str(root / 'a'), str(root / 'a'), str(root / 'result.json')]
                try:
                    material.main()
                except (KeyError, ValueError) as error:
                    rejected.append(dict(case=name, consumer=material.__name__, reason=str(error)))
                else:
                    raise AssertionError('Material consumer accepted damaged measurement: ' + name)
            finally:
                sys.argv = saved
            saved = sys.argv
            try:
                sys.argv = ['compare-editor-environment-visibility', str(root)]
                try:
                    runpy.run_path(str(Path(__file__).with_name('compare-editor-environment-visibility.py')), run_name='__main__')
                except (KeyError, ValueError) as error:
                    rejected.append(dict(case=name, consumer='environment-visibility', reason=str(error)))
                else:
                    raise AssertionError('Environment consumer accepted damaged measurement: ' + name)
            finally:
                sys.argv = saved
    assert source.read_bytes() == original
    report = dict(passed=True, captureSha256=hashlib.sha256(original).hexdigest(),
                  cases=len(cases), consumers=4, rejected=rejected, pixelClones=0,
                  scope='actual manifest positive contract and pixel-consumer admission; not fixture quality')
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS {len(cases)} metadata mutations / {len(rejected)} consumer rejections; no pixel copies')


if __name__ == '__main__':
    main()
