"""Exercise live recovery acceptance with recorded GPU data and corruptions."""
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import struct

spec = importlib.util.spec_from_file_location('live_ssgi_oracle',
    Path(__file__).resolve().parents[1] / 'temporal-validation' / 'Compare-LiveSSGIRecovery.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def main(root, output, provider='none', quality='quality'):
    accepted = oracle.main(root, provider, quality)
    records = json.loads((root / 'records.json').read_text(encoding='utf-8-sig'))
    record = next(r for r in records if r['name'] == 'cut-1')
    mutations = [
        ('ordinal', lambda r: r.update(ordinal=8)),
        ('cut-revision', lambda r: r['receipt'].update(cameraCutRevision=str(int(r['receipt']['cameraCutRevision'])+1))),
        ('view', lambda r: r['before'].update(viewId=str(int(r['before']['viewId'])+1))),
        ('scene', lambda r: r['before'].update(sceneEpoch=str(int(r['before']['sceneEpoch'])+1))),
        ('reset-revision', lambda r: r['before'].update(historyRevision=r['after']['historyRevision'])),
        ('superseded-revision', lambda r: r['after'].update(historyRevision=str(int(r['after']['historyRevision'])+1))),
    ]
    if provider != 'none':
        mutations += [
            ('provider-fallback', lambda r: r['after'].update(activeUpscaler='none')),
            ('quality', lambda r: r['after'].update(activeUpscaleQuality='invalid')),
            ('double-aa', lambda r: r['after'].update(fxaaApplied=True)),
        ]
    rejected = []
    for name, mutate in mutations:
        candidate = copy.deepcopy(record)
        mutate(candidate)
        try:
            oracle.read_capture(root, candidate, provider, quality)
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError('Accepted corrupted ' + name)
    output.mkdir(parents=True, exist_ok=False)
    source = root / record['name']
    m = json.loads((source / 'manifest.json').read_text(encoding='utf-8'))
    names = [a['file'] for a in m['attachments'] if a['name'] in ('display', 'depth', 'baseColor', 'preToneHdr')]
    names += ['manifest.json', 'ssgi-history.f32']
    original = oracle.read_capture(root, record, provider, quality)[1]
    if provider != 'none':
        for label, mutate in [
            ('clock', lambda m: m['historyRecovery'].update(fixedSdkClock=False)),
            ('clock-delta', lambda m: m['historyRecovery'].update(sdkFrameMilliseconds=33.0)),
            ('captured-quality', lambda m: m['historyRecovery'].update(upscaleQuality=99)),
            ('input-extent', lambda m: m.update(renderWidth=m['renderWidth']+2)),
        ]:
            directory = output / label / record['name']
            directory.mkdir(parents=True)
            for name in names:
                shutil.copyfile(source / name, directory / name)
            altered_manifest = copy.deepcopy(m)
            mutate(altered_manifest)
            (directory / 'manifest.json').write_text(json.dumps(altered_manifest), encoding='utf-8')
            try:
                oracle.read_capture(directory.parent, record, provider, quality)
            except ValueError:
                rejected.append(label)
            else:
                raise AssertionError('Accepted corrupted ' + label)
    for label, filename, image_name, value, tolerance in [
        ('nonfinite', 'ssgi-history.f32', 'ssgi', float('nan'), 1e-6),
        ('finite-pixel', 'ssgi-history.f32', 'ssgi', 20.0, 1e-6),
        ('reconstructed-pixel', 'preToneHdr.f32', 'reconstructed', 20.0, 1e-6),
        ('display-pixel', 'display.f32', 'display', 20.0, 1/255 + 1e-7),
    ]:
        directory = output / label / record['name']
        directory.mkdir(parents=True)
        for name in names:
            shutil.copyfile(source / name, directory / name)
        pixel_path = directory / filename
        raw = bytearray(pixel_path.read_bytes())
        struct.pack_into('<f', raw, 0, value)
        pixel_path.write_bytes(raw)
        try:
            altered = oracle.read_capture(directory.parent, record, provider, quality)[1]
            oracle.compare(altered, original, image_name, tolerance)
        except ValueError:
            rejected.append(label)
        else:
            raise AssertionError('Accepted corrupted ' + label)
    for image_name, filename in [('reconstructed', 'preToneHdr.f32'), ('display', 'display.f32')]:
        label = 'constant-' + image_name
        directory = output / label / record['name']
        directory.mkdir(parents=True)
        for name in names:
            shutil.copyfile(source / name, directory / name)
        pixel_path = directory / filename
        pixel_path.write_bytes(struct.pack('<f', 1.0) * (pixel_path.stat().st_size // 4))
        try:
            oracle.read_capture(directory.parent, record, provider, quality)
        except ValueError:
            rejected.append(label)
        else:
            raise AssertionError('Accepted constant ' + image_name)
    result = dict(passed=True, acceptedCaptures=accepted['captures'], rejectedMutations=rejected)
    (output / 'results.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--provider', default='none')
    parser.add_argument('--quality', default='quality')
    args = parser.parse_args()
    main(args.directory.resolve(), args.output.resolve(), args.provider, args.quality)
