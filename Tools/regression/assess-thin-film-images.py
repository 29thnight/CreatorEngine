"""Judge the fixed film sweep and its paired film-off controls.

The quality targets are declared before generating the sweep. A failure is an
actionable acceptance result, not a failed capture; it never loosens the targets.
This is a scene-linear image target, not a perceptual/JND or full-scene FPS claim.
"""
import argparse
import importlib.util
import hashlib
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'blender'))
from thin_film_cases import definitions

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
    measurement = json.loads(args.comparison.read_text(encoding='utf-8'))
    reference, native = (Path(measurement[k]) for k in ('reference', 'native'))
    repeat = Path(measurement['reference_repeat']) if measurement['reference_repeat'] else None
    manifest = json.loads((reference/'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('suite') != 'thin-film' or not measurement['control_passed'] or repeat is None:
        raise ValueError('Requires the fixed film suite, valid controls and independent seed repeat')
    images.validate_reference(reference, manifest)
    repeat_manifest = json.loads((repeat/'manifest.json').read_text(encoding='utf-8'))
    images.validate_reference(repeat, repeat_manifest)
    def setup(value):
        return {**{k:v for k,v in value.items() if k not in ('seed','cases')},
                'cases': [{k:v for k,v in c.items() if k != 'reference_sha256'} for c in value['cases']]}
    if setup(manifest) != setup(repeat_manifest) or manifest['seed'] == repeat_manifest['seed']:
        raise ValueError('Reference repeat identity/settings differ')
    fixed = {mode+'-'+name: json.loads(json.dumps(values)) for mode in ('sun','furnace') for name,values in definitions()}
    if {c['id']:c['inputs'] for c in manifest['cases']} != fixed:
        raise ValueError('Film sweep parameter identity differs')
    if json.loads((native/'reference-manifest.json').read_text(encoding='utf-8')) != manifest:
        raise ValueError('Native reference identity differs')
    images.validate_reference(native, manifest, reference_images=False)
    expected = {c['id'] for c in manifest['cases']}
    rows = {c['id']: c for c in measurement['cases']}
    if set(rows) != expected or len(rows) != len(measurement['cases']):
        raise ValueError('Comparison case identity differs')
    for name,row in rows.items():
        if hashlib.sha256((native/(name+'.f32')).read_bytes()).hexdigest() != row.get('native_sha256'):
            raise ValueError('Native image differs from measured pixels: '+name)
    # Use the same common mask for every film/control and every angle band.
    masks = {}
    for mode in ('sun', 'furnace'):
        a, b = (images.read(root/(mode+'-control_emission.f32')) for root in (reference,native))
        covered = [a[4*p+3] > .999 and b[4*p] > 1 for p in range(4096)]
        masks[mode] = [p for p in range(4096) if 2 <= p%64 < 62 and 2 <= p//64 < 62 and
                       all(covered[p+dy*64+dx] for dy in range(-2,3) for dx in range(-2,3))]
        if len(masks[mode]) < 100 or max(abs(a[4*p+c]-b[4*p+c])/max(1,abs(a[4*p+c]))
                for p in masks[mode] for c in range(3)) > .002:
            raise ValueError('Matched emission/coverage control differs: '+mode)
    normal, view = (images.read(native/(name+'.f32')) for name in ('debug-normal','debug-view'))
    diagnostics = {}
    for name in ('debug-normal','debug-view'):
        a,b = (images.read(root/(name+'.f32')) for root in (reference,native))
        error = max(abs(a[4*p+c]-b[4*p+c]) for p in masks['sun'] for c in range(3))
        if error > .002:
            raise ValueError('Matched geometry/view diagnostic differs: '+name)
        diagnostics[name] = dict(max_abs=error)
    cosines = [sum((2*normal[4*p+c]-1)*(2*view[4*p+c]-1) for c in range(3)) for p in range(4096)]
    cases = []
    for name, row in rows.items():
        if '-film_' not in name:
            continue
        mode = name.split('-')[0]
        off = name.replace('-film_', '-off_')
        if (off not in rows or rows[off]['interior_pixels'] != row['interior_pixels'] or
                row['interior_pixels'] != len(masks[mode])):
            raise ValueError('Missing or mismatched film-off control: '+name)
        a, b, a0, b0 = (images.read(path) for path in
                       (reference/(name+'.f32'), native/(name+'.f32'),
                        reference/(off+'.f32'), native/(off+'.f32')))
        # Recompute the acceptance metrics, rejecting a stale or edited report.
        pixels = masks[mode]
        energy = max(sum(a[4*p+c]**2 for p in pixels for c in range(3)),1e-20)
        difference = [a[4*p+c]-b[4*p+c] for p in pixels for c in range(3)]
        normalized = sorted(abs(a[4*p+c]-b[4*p+c])/max(1,abs(a[4*p+c])) for p in pixels for c in range(3))
        measured = dict(relative_rms=math.sqrt(sum(d*d for d in difference)/energy),
                        p95_normalized=normalized[int(.95*(len(normalized)-1))],max_normalized=normalized[-1])
        repeated = images.read(repeat/(name+'.f32'))
        noise = math.sqrt(sum((a[4*p+c]-repeated[4*p+c])**2 for p in pixels for c in range(3))/energy)
        if any(not math.isfinite(row[k]) or abs(row[k]-v)>1e-12 for k,v in measured.items()) or (
                not math.isfinite(row['reference_repeat']['relative_rms']) or
                abs(row['reference_repeat']['relative_rms']-noise)>1e-12):
            raise ValueError('Comparison metrics differ from actual pixels: '+name)
        # Off is a required same-model control, with the same policy. Recompute
        # its metrics as well: a good film result cannot hide a bad base closure.
        off_energy = max(sum(a0[4*p+c]**2 for p in pixels for c in range(3)),1e-20)
        off_difference = [a0[4*p+c]-b0[4*p+c] for p in pixels for c in range(3)]
        off_normalized = sorted(abs(a0[4*p+c]-b0[4*p+c])/max(1,abs(a0[4*p+c])) for p in pixels for c in range(3))
        off_measured = dict(relative_rms=math.sqrt(sum(d*d for d in off_difference)/off_energy),
            p95_normalized=off_normalized[int(.95*(len(off_normalized)-1))],max_normalized=off_normalized[-1])
        off_repeat = images.read(repeat/(off+'.f32'))
        off_noise = math.sqrt(sum((a0[4*p+c]-off_repeat[4*p+c])**2 for p in pixels for c in range(3))/off_energy)
        if any(not math.isfinite(rows[off][k]) or abs(rows[off][k]-v)>1e-12 for k,v in off_measured.items()) or (
                not math.isfinite(rows[off]['reference_repeat']['relative_rms']) or
                abs(rows[off]['reference_repeat']['relative_rms']-off_noise)>1e-12):
            raise ValueError('Off-control metrics differ from actual pixels: '+off)
        off_failures = [key for key in ('relative_rms','p95_normalized','max_normalized') if off_measured[key] > POLICY[key]]
        if off_noise > POLICY['reference_repeat_relative_rms']:
            off_failures.append('reference_noise')
        failures = [key for key in ('relative_rms','p95_normalized','max_normalized') if row[key] > POLICY[key]]
        if row['reference_repeat']['relative_rms'] > POLICY['reference_repeat_relative_rms']:
            failures.append('reference_noise')
        bands = []
        for lower, upper in ((0,.25),(.25,.5),(.5,.75),(.75,1.0001)):
            pixels = [p for p in masks[mode] if lower <= cosines[p] < upper]
            if not pixels:
                bands.append(dict(cosine=[lower,min(upper,1)],pixels=0,measured=False))
                continue
            differences = [(b[4*p+c]-a[4*p+c]) for p in pixels for c in range(3)]
            paired = [(b[4*p+c]-b0[4*p+c])-(a[4*p+c]-a0[4*p+c]) for p in pixels for c in range(3)]
            energy = sum(a[4*p+c]**2 for p in pixels for c in range(3))
            bands.append(dict(cosine=[lower,min(upper,1)],pixels=len(pixels),measured=True,
                relative_rms=math.sqrt(sum(d*d for d in differences)/max(energy,1e-20)),
                film_effect_residual_rms=math.sqrt(sum(d*d for d in paired)/max(energy,1e-20))))
        cases.append(dict(id=name,accepted=not failures,failed_targets=failures,
            relative_rms=row['relative_rms'],p95_normalized=row['p95_normalized'],max_normalized=row['max_normalized'],
            reference_noise_rms=row['reference_repeat']['relative_rms'],film_off_rms=off_measured['relative_rms'],
            film_off_accepted=not off_failures,film_off_failed_targets=off_failures,
            film_off_p95_normalized=off_measured['p95_normalized'],film_off_max_normalized=off_measured['max_normalized'],
            film_off_reference_noise_rms=off_noise,
            cosine_bands=bands))
    if len(cases) != 16:
        raise ValueError('Expected eight film/off pairs under both lights')
    report = dict(schema='creator.material.thin-film-acceptance.v2',policy=POLICY,
        target_kind='scene-linear engineering target; no perceptual or FPS equivalence claim',
        comparison=str(args.comparison),accepted=all(c['accepted'] and c['film_off_accepted'] for c in cases),
        film_accepted_count=sum(c['accepted'] for c in cases),off_accepted_count=sum(c['film_off_accepted'] for c in cases),
        scope='8 film/off pairs; sun and white furnace; shared eroded interior; extreme grazing evaluated by optical oracle',
        geometry_sha256=manifest['geometry_sha256'], diagnostics=diagnostics,
        image_cosine_range=[min(cosines[p] for p in masks['sun']),max(cosines[p] for p in masks['sun'])],
        cases=cases)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    for c in cases:
        print(c['id'], 'rms=%.4f%%'%(100*c['relative_rms']), 'off=%.4f%%'%(100*c['film_off_rms']),
              'accepted='+str(c['accepted']), 'failures='+','.join(c['failed_targets']))
    print('MAT9_FILM_JUDGED accepted='+str(report['accepted']))
    return 0 if report['accepted'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
