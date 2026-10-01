"""Preserve Blender reference pixels while changing only the engine cook under test.

Blender renders the original HDR/EXR, never the engine's cook. Inputs, source
hashes, image hashes and geometry remain frozen; original captures are untouched.
"""
import argparse
import hashlib
import json
import importlib.util
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--forest', type=Path, required=True)
    parser.add_argument('--autumn', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Reference output must be new')
    spec = importlib.util.spec_from_file_location('comparison', Path(__file__).with_name('compare-material-blender-images.py'))
    comparison = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(comparison)
    manifest = json.loads((args.reference/'manifest.json').read_text(encoding='utf-8'))
    comparison.validate_reference(args.reference, manifest)
    if manifest.get('lighting') != 'hdri':
        raise ValueError('Requires a frozen HDRI reference')
    previous = json.loads(json.dumps(manifest['environments']))
    for mode in ('forest', 'autumn'):
        cook = getattr(args, mode).resolve()
        data = cook.read_bytes()
        if (data[:8] not in (b'CEIBL001', b'CEIBL002', b'CEIBL003', b'CEIBL004', b'CEIBL005') or
                hashlib.sha256(data[40:]).digest() != data[8:40] or
                data[48:80].hex() != previous[mode]['source_sha256']):
            raise ValueError('Replacement cook checksum or original source identity differs')
        manifest['environments'][mode]['cooked'] = str(cook)
        manifest['environments'][mode]['cooked_sha256'] = hashlib.sha256(data).hexdigest()
    manifest.pop('cook_migration', None)
    manifest['cook_rebind'] = dict(method='engine cook under test changed; Blender reads unchanged original source',
                                  reference_images_rerendered=False, reference_pixels_and_geometry_unchanged=True,
                                  previous_environments=previous)
    shutil.copytree(args.reference, args.output)
    (args.output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    (args.output/'environment.config').write_text(''.join(
        mode+' '+json.dumps(record['cooked'])+' 0.35\n'
        for mode, record in manifest['environments'].items()), encoding='utf-8')
    comparison.validate_reference(args.output, manifest)
    print('ENVIRONMENT_REFERENCE_REBOUND cases=%d reference_pixels=unchanged' % len(manifest['cases']))


if __name__ == '__main__':
    main()
