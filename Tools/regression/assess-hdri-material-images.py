"""Judge fixed forest/autumn Core/Layered inputs under the existing MAT-9 targets.

Requires independently rendered reference seeds and rechecks measured pixels.
This measures scene-linear engineering error, not EEVEE/Material Preview or FPS parity.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

POLICY = dict(relative_rms=.01, p95_normalized=.01, max_normalized=.05,
              reference_repeat_relative_rms=.0025)
SPEC = importlib.util.spec_from_file_location('images', Path(__file__).with_name('compare-material-blender-images.py'))
images = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(images)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('comparison', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    measured = json.loads(args.comparison.read_text(encoding='utf-8'))
    reference, native, repeat = (Path(measured[k]) for k in ('reference', 'native', 'reference_repeat'))
    manifest = json.loads((reference/'manifest.json').read_text(encoding='utf-8'))
    repeated_manifest = json.loads((repeat/'manifest.json').read_text(encoding='utf-8'))
    captured_manifest = json.loads((native/'reference-manifest.json').read_text(encoding='utf-8'))
    if manifest.get('lighting') != 'hdri' or not measured['control_passed'] or manifest != captured_manifest:
        raise ValueError('Requires matched HDRI suite and valid capture controls')
    shader_contract = native/'shader-root.txt'
    product_shaders = Path(__file__).resolve().parents[2]/'Dynamic_CPP/Assets/Shaders/DefaultPassShader'
    if shader_contract.exists() and Path(shader_contract.read_text(encoding='utf-8').strip()).resolve() != product_shaders:
        raise ValueError('Diagnostic shader snapshots cannot enter product acceptance')
    if (native/'mis-disabled.txt').exists():
        raise ValueError('Disabled-MIS diagnostic captures cannot enter product acceptance')
    for root, record, source in ((reference,manifest,True),(repeat,repeated_manifest,True),(native,manifest,False)):
        images.validate_reference(root,record,reference_images=source)
    def setup(value):
        return {**{k:v for k,v in value.items() if k not in ('cases','seed')},
                'cases':[{k:v for k,v in case.items() if k != 'reference_sha256'} for case in value['cases']]}
    if setup(manifest) != setup(repeated_manifest) or manifest['seed'] == repeated_manifest['seed']:
        raise ValueError('Reference repeat setup/seed differs')
    # The authored cases come from the already fixed punctual/furnace suite.
    fixed = Path(__file__).parents[1]/'blender/fixtures/material-matched-5.1.1'
    fixed_manifest = json.loads((fixed/'manifest.json').read_text(encoding='utf-8'))
    images.validate_reference(fixed,fixed_manifest)
    definitions = {case['id'].removeprefix('sun-'):case['inputs'] for case in fixed_manifest['cases']
                   if case['id'].startswith('sun-')}
    definitions['control_mirror'] = {'Base Color':[1,1,1,1],'Metallic':1,'Roughness':0}
    expected = {mode+'-'+name:values for mode in ('forest','autumn') for name,values in definitions.items()}
    if {case['id']:case['inputs'] for case in manifest['cases']} != expected:
        raise ValueError('HDRI authored case set changed')
    rows = {row['id']:row for row in measured['cases']}
    if set(rows) != set(expected) or len(rows) != len(measured['cases']):
        raise ValueError('Measured case set differs')
    masks = {}
    for mode in ('forest','autumn'):
        a,b = (images.read(root/(mode+'-control_emission.f32')) for root in (reference,native))
        covered = [a[4*p+3] > .999 and b[4*p] > 1 for p in range(4096)]
        masks[mode] = [p for p in range(4096) if 2<=p%64<62 and 2<=p//64<62 and
                       all(covered[p+dy*64+dx] for dy in range(-2,3) for dx in range(-2,3))]
        if len(masks[mode])<100:
            raise ValueError('Common coverage mask failed')
    diagnostics = {}
    for name in ('debug-normal','debug-view'):
        a,b = (images.read(root/(name+'.f32')) for root in (reference,native))
        error = max(abs(a[4*p+c]-b[4*p+c]) for p in masks['forest'] for c in range(3))
        if error > .002:
            raise ValueError('Shared geometry/view diagnostic differs')
        diagnostics[name] = dict(max_abs=error)
    results = []
    for name,row in rows.items():
        a,b,r = (images.read(root/(name+'.f32')) for root in (reference,native,repeat))
        if hashlib.sha256((native/(name+'.f32')).read_bytes()).hexdigest() != row['native_sha256']:
            raise ValueError('Native image hash changed: '+name)
        pixels = masks[name.split('-')[0]]
        energy = max(sum(a[4*p+c]**2 for p in pixels for c in range(3)),1e-20)
        error = [abs(a[4*p+c]-b[4*p+c])/max(1,abs(a[4*p+c])) for p in pixels for c in range(3)]
        values = dict(relative_rms=math.sqrt(sum((a[4*p+c]-b[4*p+c])**2 for p in pixels for c in range(3))/energy),
                      p95_normalized=sorted(error)[int(.95*(len(error)-1))],max_normalized=max(error))
        noise = math.sqrt(sum((a[4*p+c]-r[4*p+c])**2 for p in pixels for c in range(3))/energy)
        if row['interior_pixels'] != len(pixels) or any(not math.isfinite(row[k]) or abs(row[k]-v)>1e-12
                for k,v in values.items()) or not math.isfinite(row['reference_repeat']['relative_rms']) or \
                abs(row['reference_repeat']['relative_rms']-noise)>1e-12:
            raise ValueError('Reported metrics differ from actual pixels: '+name)
        failures = [k for k,v in values.items() if v>POLICY[k]]
        if noise>POLICY['reference_repeat_relative_rms']:
            failures.append('reference_noise')
        result = dict(id=name,accepted=not failures,failed_targets=failures,**values,reference_noise_rms=noise)
        results.append(result)
        print(name, 'rms=%.4f%%'%(100*values['relative_rms']), 'noise=%.4f%%'%(100*noise),
              'accepted='+str(not failures), 'failures='+','.join(failures))
    materials = [r for r in results if '-control_' not in r['id']]
    controls = [r for r in results if '-control_' in r['id']]
    report = dict(schema='creator.material.hdri-acceptance.v1',policy=POLICY,
                  comparison=str(args.comparison),accepted=all(r['accepted'] for r in results),
                  material_accepted_count=sum(r['accepted'] for r in materials),material_count=len(materials),
                  control_accepted_count=sum(r['accepted'] for r in controls),control_count=len(controls),
                  scope='10 constant Core/Layered inputs, two HDRIs, no direct light; original area-light/textured grid excluded',
                  geometry_sha256=manifest['geometry_sha256'],diagnostics=diagnostics,cases=results)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('MAT9_HDRI_JUDGED accepted='+str(report['accepted']))
    return 0 if report['accepted'] else 2


if __name__ == '__main__':
    sys.exit(main())
