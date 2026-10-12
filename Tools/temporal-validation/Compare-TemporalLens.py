"""Live perspective lens reset oracle: no synthetic cut, exact same-state reference."""
import argparse
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('recovery',Path(__file__).with_name('Compare-LiveSSGIRecovery.py'))
recovery=importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)
require=recovery.require
LENSES={'fov-45':dict(fov=45,near=.1,far=500), 'fov-90':dict(fov=90,near=.1,far=500),
        'near':dict(fov=60,near=.25,far=500), 'far':dict(fov=60,near=.1,far=100)}
BASE=dict(fov=60,near=.1,far=500)

def main(root,provider='dlss',quality='quality'):
    records=json.loads((root/'records.json').read_text(encoding='utf-8-sig'))
    names=['baseline-1','restored-1']+[f'{case}-{ordinal}{suffix}' for case in LENSES
        for ordinal in (1,8) for suffix in ('','-reference')]
    require(len(records)==18 and {x['name'] for x in records}==set(names),'incomplete lens matrix')
    captures={r['name']:recovery.read_capture(root,r,provider,quality) for r in records}
    base=captures['baseline-1']
    camera=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
    comparisons=[]
    for r in records:
        name=r['name'];m,images,hashes,_,_=captures[name]
        require(m['camera']['view']==camera,'lens changed camera pose')
        case=name.removesuffix('-reference').rsplit('-',1)[0]
        lens=LENSES.get(case,BASE)
        recovery.check_lens_projection(m,lens)
        require(r['ordinal']==int(name.removesuffix('-reference').rsplit('-',1)[1]),'wrong lens ordinal')
        action='camera.cut' if name=='baseline-1' or name.endswith('-reference') else 'camera.lens.probe'
        require(r['action']==action,'wrong lens trigger')
        require(m['draws']==base[0]['draws'] and m['viewId']==base[0]['viewId'] and m['sceneEpoch']==base[0]['sceneEpoch'],
                'lens test changed scene/view')
        if action=='camera.cut': continue
        ref,reference,ref_hashes,_,_=captures['baseline-1' if name=='restored-1' else name+'-reference']
        require(m['camera']==ref['camera'],'same-state reference camera differs')
        errors={n:recovery.compare(images,reference,n,1/255+1e-7 if n=='display' else 1e-6)
                for n in ('depth','baseColor','ssgi','reconstructed','display')}
        depth_change=max(abs(x-y) for x,y in zip(images['depth'][3],base[1]['depth'][3]))
        if name!='restored-1':
            require(depth_change>1e-6,'lens change never reached scene depth')
            noop=json.loads((root/(name+'-noop.json')).read_text(encoding='utf-8-sig'))
            require(noop['receipt']['before']==noop['receipt']['lens'],'no-op lens was not identical')
            require(all(abs(noop['receipt']['lens'][k]-lens[k]) < 1e-6 for k in lens), 'no-op tested the wrong lens')
            for state in (noop['before'],noop['after']):
                require(state['activeUpscaler']==provider and state['activeUpscaleQuality']==quality and
                        state['temporalAaApplied'] is True and state['fxaaApplied'] is False,
                        'no-op substituted a fallback')
            for key in ('historyRevision','viewId','sceneEpoch','requestedGeneration','observedGeneration','historyResetGeneration'):
                require(noop['before'][key]==noop['after'][key],'no-op changed history or identity')
            require(int(noop['after']['lastRealFrameId'])>int(noop['before']['lastRealFrameId'])+16 and
                    not noop['after']['reset'],'no-op was not observed across later frames')
        comparisons.append(dict(name=name,errors=errors,depthChange=depth_change,hashes=hashes,referenceHashes=ref_hashes))
    rejected=json.loads((root/'invalid-inputs.json').read_text(encoding='utf-8-sig'))
    require(len(rejected)==6 and len({tuple(x['arguments']) for x in rejected})==6 and
            all(x['response']['status']=='invalid_arguments' and x['response']['code']!='command.unknown'
                for x in rejected),'missing invalid lens rejections')
    require(all(x['unchanged']==rejected[0]['unchanged'] for x in rejected),'invalid lens input changed camera')
    return dict(passed=True,provider=provider,quality=quality,captures=18,comparisons=comparisons,noOpChecks=8,invalidInputs=6,
                scope='perspective FOV 45/90, near .25 and far 100; first/eighth reset vs cut reference; fixed SDK clock')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True)
    p.add_argument('--provider',choices=['dlss'],default='dlss');p.add_argument('--quality',required=True);a=p.parse_args()
    try:result=main(a.directory,a.provider,a.quality)
    except (ValueError,KeyError,OSError,TypeError,IndexError) as e:result=dict(passed=False,error=str(e))
    (a.directory/'pixels.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
    raise SystemExit(0 if result['passed'] else 1)
