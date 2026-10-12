"""Compare live quality/resize reset output against a same-state camera cut."""
import argparse
import importlib.util
import json
import math
from pathlib import Path

spec = importlib.util.spec_from_file_location('recovery', Path(__file__).with_name('Compare-LiveSSGIRecovery.py'))
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
require = recovery.require

def main(root, provider='dlss', quality='quality'):
    records = json.loads((root / 'records.json').read_text(encoding='utf-8-sig'))
    names = [f'{trigger}-{ordinal}{suffix}' for trigger in ('quality', 'resize-960', 'resize-1280')
             for ordinal in (1, 8) for suffix in ('', '-reference')]
    require(len(records) == len(names) and {r['name'] for r in records} == set(names), 'incomplete transition matrix')
    captures = {r['name']: recovery.read_capture(root, r, provider, quality) for r in records}
    output = []
    camera = captures[names[0]][0]['camera']['view']
    require(camera == [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1], 'fixture camera moved from authored pose')
    for r in records:
        m = captures[r['name']][0]
        require(m['camera']['view'] == camera, 'transition moved camera pose')
        projection=m['camera']['projection']
        require(abs(projection[5]-math.sqrt(3)) < 1e-6 and
                abs(projection[5]/projection[0]-m['displayWidth']/m['displayHeight']) < 1e-5,
                'transition changed vertical FOV or used render aspect for display projection')
        require(r['ordinal'] == int(r['name'].replace('-reference','').split('-')[-1]), 'wrong accepted ordinal')
        expected_action = 'camera.cut' if r['name'].endswith('-reference') else 'temporal.upscale' if r['name'].startswith('quality-') else 'window.resize'
        require(r['action'] == expected_action, 'wrong transition trigger')
        if r['name'].endswith('-reference'):
            continue
        m, images, hashes, surfaces, accumulation = captures[r['name']]
        ref, reference, ref_hashes, _, _ = captures[r['name']+'-reference']
        require(m['viewId'] == ref['viewId'] and m['sceneEpoch'] == ref['sceneEpoch'] and m['camera'] == ref['camera'], 'reference camera or view changed')
        require(m['draws'] == ref['draws'], 'reference scene changed')
        output.append(dict(name=r['name'], ordinal=r['ordinal'],
            renderExtent=[m['renderWidth'],m['renderHeight']], displayExtent=[m['displayWidth'],m['displayHeight']],
            errors={n:recovery.compare(images,reference,n,1/255+1e-7 if n=='display' else 1e-6)
                    for n in ('depth','baseColor','ssgi','reconstructed','display')},
            hashes=hashes, referenceHashes=ref_hashes, surfaces=surfaces, accumulation=accumulation))
    return dict(passed=True, provider=provider, quality=quality, captures=len(records), comparisons=output,
                scope='live quality/resize first and eighth accepted reset frames vs camera-cut; fixed SDK clock, static fixture, no physical display acceptance')

if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--directory',type=Path,required=True)
    p.add_argument('--provider',choices=['dlss'],default='dlss')
    p.add_argument('--quality',required=True)
    a=p.parse_args()
    try: result=main(a.directory,a.provider,a.quality)
    except (ValueError,KeyError,OSError,TypeError,IndexError) as e: result=dict(passed=False,error=str(e))
    (a.directory/'pixels.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))
    raise SystemExit(0 if result['passed'] else 1)
