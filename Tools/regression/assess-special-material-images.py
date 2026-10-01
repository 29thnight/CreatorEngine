"""Recheck fixed MAT-9 Special pixels and existing error targets without accepting model limits silently."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

SPEC = importlib.util.spec_from_file_location('images', Path(__file__).with_name('compare-material-blender-images.py'))
images = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(images)
POLICY = dict(relative_rms=.01, p95_normalized=.01, max_normalized=.05,
              reference_noise_rms=.0025)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('comparison', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    measured = json.loads(args.comparison.read_text(encoding='utf-8'))
    if not measured.get('reference_repeat'):
        raise ValueError('Independent reference repeat is required for Special assessment')
    reference, native, repeat = (Path(measured[k]) for k in ('reference', 'native', 'reference_repeat'))
    manifests = [json.loads((root/name).read_text(encoding='utf-8')) for root,name in
                 ((reference,'manifest.json'),(native,'reference-manifest.json'),(repeat,'manifest.json'))]
    primary, captured, repeated = manifests
    if (primary.get('suite') not in ('special','special-volume','special-surface') or primary.get('lighting') == 'hdri' or
            primary.get('diagnostic_subset') or primary.get('diagnostic_geometry') or
            primary.get('diagnostic_volume_shadow') or 'diagnostic_volume_bounces' in primary or
            primary != captured or not measured['control_passed']):
        raise ValueError('Requires full fixed Special suite and valid native controls')
    def setup(value):
        return {**{k:v for k,v in value.items() if k not in ('cases','seed')},
                'cases':[{k:v for k,v in case.items() if k != 'reference_sha256'} for case in value['cases']]}
    if setup(primary) != setup(repeated) or primary['seed'] == repeated['seed']:
        raise ValueError('Independent reference setup/seed differs')
    for root,manifest,source in ((reference,primary,True),(native,captured,False),(repeat,repeated,True)):
        images.validate_reference(root,manifest,source)
    product = Path(__file__).resolve().parents[2]/'Dynamic_CPP/Assets/Shaders/DefaultPassShader'
    if Path((native/'shader-root.txt').read_text(encoding='utf-8').strip()).resolve() != product:
        raise ValueError('Diagnostic shader roots cannot enter product assessment')
    if (native/'mis-disabled.txt').exists():
        raise ValueError('Disabled MIS diagnostic cannot enter assessment')
    rows = {row['id']:row for row in measured['cases']}
    if len(rows) != len(measured['cases']) or set(rows) != {case['id'] for case in primary['cases']}:
        raise ValueError('Measured case set differs')
    masks = {}
    for mode in ('sun','furnace'):
        a,b = (images.read(root/(mode+'-control_emission.f32')) for root in (reference,native))
        covered = [a[4*p+3]>.999 and b[4*p]>1 for p in range(4096)]
        masks[mode] = [p for p in range(4096) if 2<=p%64<62 and 2<=p//64<62 and
                       all(covered[p+dy*64+dx] for dy in range(-2,3) for dx in range(-2,3))]
        if len(masks[mode])<100:
            raise ValueError('Common fixed coverage failed')
    results = []
    diagnostics = {}
    for name in ('debug-normal','debug-view'):
        a,b = (images.read(root/(name+'.f32')) for root in (reference,native))
        error = max(abs(a[4*p+c]-b[4*p+c]) for p in masks['furnace'] for c in range(3))
        if error>.002:
            raise ValueError('Shared normal/view diagnostic differs')
        diagnostics[name] = dict(max_abs=error)
    for name,row in rows.items():
        a,b,r = (images.read(root/(name+'.f32')) for root in (reference,native,repeat))
        if hashlib.sha256((native/(name+'.f32')).read_bytes()).hexdigest() != row['native_sha256']:
            raise ValueError('Native image changed')
        indices = masks[name.split('-')[0]]
        energy = max(sum(a[4*p+c]**2 for p in indices for c in range(3)),1e-20)
        normalized = [abs(a[4*p+c]-b[4*p+c])/max(1,abs(a[4*p+c])) for p in indices for c in range(3)]
        values = dict(relative_rms=math.sqrt(sum((a[4*p+c]-b[4*p+c])**2 for p in indices for c in range(3))/energy),
                      p95_normalized=sorted(normalized)[int(.95*(len(normalized)-1))],max_normalized=max(normalized))
        noise = math.sqrt(sum((a[4*p+c]-r[4*p+c])**2 for p in indices for c in range(3))/energy)
        if (row['interior_pixels'] != len(indices) or any(not math.isfinite(row[k]) or abs(row[k]-v)>1e-12
                for k,v in values.items()) or abs(row['reference_repeat']['relative_rms']-noise)>1e-12):
            raise ValueError('Reported metrics differ from actual pixels')
        failed = [k for k,v in values.items() if v>POLICY[k]]
        if noise>POLICY['reference_noise_rms']:
            failed.append('reference_noise_rms')
        results.append(dict(id=name,accepted=not failed,failed_targets=failed,**values,reference_noise_rms=noise))
        print(name,'rms=%.4f%%'%(values['relative_rms']*100),'noise=%.4f%%'%(noise*100),'accepted='+str(not failed))
    local_controls = {}
    for mode in (('sun','furnace') if primary['suite'] in ('special','special-surface') else ()):
        a,b = (images.read(native/(mode+'-'+name+'.f32')) for name in ('special_subsurface_local','special_subsurface_off'))
        difference = max(abs(a[4*p+c]-b[4*p+c]) for p in range(4096) for c in range(3))
        interior = max(abs(a[4*p+c]-b[4*p+c]) for p in masks[mode] for c in range(3))
        alpha_difference = max(abs(a[4*p+3]-b[4*p+3]) for p in range(4096))
        local_controls[mode] = dict(max_abs=difference,interior_max_abs=interior,
                                   alpha_max_abs=alpha_difference,scope='full 64x64 image',
                                   accepted=difference<=.002 and alpha_difference==0)
    report = dict(schema='creator.material.special-image-assessment.v1',policy=POLICY,
                  reference_recipe_accepted=primary.get('special_reference_version',1)==2,
                  accepted=primary.get('special_reference_version',1)==2 and all(row['accepted'] for row in results)
                           and all(row['accepted'] for row in local_controls.values()),
                  comparison=str(args.comparison),scope=primary['special_transport'],
                  diagnostics=diagnostics,
                  geometry_sha256=primary['geometry_sha256'],zero_scale_controls=local_controls,
                  passed=sum(row['accepted'] for row in results),count=len(results),cases=results,
                  exclusions=primary['exclusions'])
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('MAT9_SPECIAL_JUDGED accepted='+str(report['accepted']))
    return 0 if report['accepted'] else 2


if __name__ == '__main__':
    sys.exit(main())
