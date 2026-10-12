"""Compare actual post-reconstruction editor overlay pixels at a fixed camera."""
import argparse
import array
import json
import math
from pathlib import Path


def observe(path):
    manifest = json.loads((path / 'manifest.json').read_text(encoding='utf-8-sig'))
    assert manifest['captureMode'] == 'temporal-observation-v1', 'capture changed runtime settings'
    assert manifest['temporalNativeGateActive'] is False, 'native-only capture cannot test reconstruction'
    assert manifest['goldenEligible'] in (False, 0), 'observed reconstruction mislabeled as a native golden'
    assert manifest['validationCount'] == 0, 'capture contains validation failures'
    assert manifest['finite'], 'nonfinite output'
    live_passes = [item for item in manifest['compiledGraph']['passes'] if not item['culled']]
    post = next(item['compiledIndex'] for item in live_passes if item['name'] == 'PostChain.Uber')
    ui = next(item['compiledIndex'] for item in live_passes if item['name'] == 'UI.Draw')
    assert post < ui, 'screen UI must follow tone mapping'
    if manifest['upscaler'] != 'none':
        upscale = next(item['compiledIndex'] for item in live_passes if item['name'] == 'Temporal.Upscale')
        first_post = min(item['compiledIndex'] for item in live_passes if item['name'].startswith('PostChain.'))
        assert upscale < first_post, 'HDR reconstruction must precede the post chain'
    attachment = next(item for item in manifest['attachments'] if item['name'] == 'display')
    width, height = attachment['width'], attachment['height']
    pixels = array.array('f', (path / attachment['file']).read_bytes())
    assert len(pixels) == width * height * 4
    assert all(math.isfinite(value) for value in pixels)
    icon = []
    outside_grid = 0
    # This fixture has a white light icon and a neutral grid, with no models.
    # The broken viewport confines both to the upper-left render-sized rectangle.
    for y in range(height):
        for x in range(width):
            value = pixels[(y * width + x) * 4]
            if value > 0.6:
                icon.append((x, y))
            if x > width * 0.75 and y > height * 0.7 and abs(value - pixels[(y * width + x - 1) * 4]) > 0.01:
                outside_grid += 1
    assert icon, 'light icon not drawn'
    box = [min(x for x, _ in icon), min(y for _, y in icon),
           max(x for x, _ in icon), max(y for _, y in icon)]
    return manifest, {'capture': str(path), 'iconBox': box, 'outerGridPixels': outside_grid,
                      'upscaler': manifest['upscaler'], 'renderWidth': manifest['renderWidth'],
                      'renderHeight': manifest['renderHeight'], 'width': width, 'height': height}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run', type=Path)
    parser.add_argument('--require-requested-providers', action='store_true')
    args = parser.parse_args()
    paths = sorted((p for p in args.run.iterdir() if p.is_dir() and (p / 'manifest.json').exists()),
                   key=lambda p: int(p.name.split('-')[0]))
    records = []
    problems = []
    baseline = None
    camera = None
    for path in paths:
        manifest, record = observe(path)
        if args.require_requested_providers and manifest['upscaler'] != path.name.split('-')[1]:
            problems.append(f'{path.name}: requested provider fell back to {manifest["upscaler"]}')
        if baseline is None:
            assert manifest['upscaler'] == 'none'
            baseline, camera = record, manifest['camera']['view']
            assert baseline['outerGridPixels'] > 0, 'fixture grid does not cover outer display area'
        if manifest['camera']['view'] != camera:
            problems.append(f'{path.name}: camera view changed')
        if max(abs(a - b) for a, b in zip(record['iconBox'], baseline['iconBox'])) > 1:
            problems.append(f'{path.name}: light icon moved or resized')
        if record['outerGridPixels'] < baseline['outerGridPixels'] * 0.95:
            problems.append(f'{path.name}: grid lost display extent coverage')
        if 'none' not in path.name and 'native-aa' not in path.name:
            if manifest['upscaler'] == 'none' or record['renderWidth'] >= record['width']:
                problems.append(f'{path.name}: reconstruction was not exercised')
        records.append(record)
    assert len(records) >= 5, 'missing provider transition captures'
    summary = {'passed': not problems, 'scope': 'fixed-camera real-frame editor overlay alignment; actual provider recorded',
               'records': records, 'errors': problems}
    (args.run / 'overlay-summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))
    raise SystemExit(0 if summary['passed'] else 1)


if __name__ == '__main__':
    main()
