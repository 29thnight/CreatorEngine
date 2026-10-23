"""Compare isolated and two-view SDK route captures with identical authored poses."""
import argparse, array, json, math
from pathlib import Path
try:
    import numpy as np
except ImportError:
    np = None

def require(ok, reason):
    if not ok: raise ValueError(reason)

def read(path): return json.loads(path.read_text(encoding='utf-8-sig'))

def image(root, manifest, name):
    if name=='postBloom':
        desc=next(s for s in manifest['diagnosticStages'] if s['name']=='post-bloom')
        require([desc['width'],desc['height']]==[manifest['displayWidth']//2,manifest['displayHeight']//2]
                and desc['channels']==4,'bloom output is not the declared half-display HDR extent')
    elif name=='sdkInput':
        stages=manifest['diagnosticStages']
        upscale=[i for i,s in enumerate(stages) if s['name'].endswith('-Temporal.Upscale')]
        require(len(upscale)==1 and upscale[0]>0,'missing actual SDK input stage')
        stage=stages[upscale[0]-1]
        desc=next(s for s in stages if s['name']==stage['readbackStage'])
        require([desc['width'],desc['height']]==[manifest['renderWidth'],manifest['renderHeight']]
                and desc['channels']==4,'SDK input is not render-sized HDR')
    else:
        desc=next(x for x in manifest['attachments'] if x['name']==name)
    raw=(root/desc['file']).read_bytes()
    if np is not None:
        values=np.frombuffer(raw,dtype='<f4')
        finite=bool(np.isfinite(values).all())
    else:
        values=array.array('f');values.frombytes(raw)
        finite=all(math.isfinite(v) for v in values)
    require(desc['encoding']=='float32-le-row-major' and len(values)==desc['width']*desc['height']*desc['channels'] and finite,'invalid GPU attachment')
    return (desc['width'],desc['height'],desc['channels']),values

def compare(isolated,interleaved,repeat=False):
    a,b=read(isolated/'fixture.json'),read(interleaved/'fixture.json')
    require(not a['companionView'] and b['companionView']==(not repeat),
            'expected independent solo/solo runs' if repeat else 'expected independent solo/dual runs')
    for key in ('schema','provider','quality','camera','extent','displayExtent','assetSourceSha256','inputAssets','historyStart'):
        require(a[key]==b[key],'different comparison input: '+key)
    require(a['schema']=='temporal.resolution.routes.v1' and len(a['cases'])==len(b['cases'])==7,'missing full routes')
    result=[]
    source='repeated solo run' if repeat else 'companion view'
    for ca,cb in zip(a['cases'],b['cases']):
        for key in ('route','geometry','previous','current','ownership','allowMeshlets'):
            require(ca[key]==cb[key],'different route source: '+key)
        for phase in ('current','control'):
            ra=isolated/ca['captures'][phase]['path'];rb=interleaved/cb['captures'][phase]['path']
            ma,mb=read(ra/'manifest.json'),read(rb/'manifest.json')
            require(ma['postTuning']==mb['postTuning'],'different post-chain settings')
            for key in ('jitterX','jitterY','previousJitterX','previousJitterY'):
                require(abs(ma['temporalMotion'][key]-mb['temporalMotion'][key])<1e-6,'different jitter packet')
            for name in ('baseColor','depth','temporalDepth','temporalMotionRG','temporalResponsive',
                         'temporalReactive','temporalTransparency','sdkInput','preToneHdr','postBloom','display'):
                shape,va=image(ra,ma,name);shape_b,vb=image(rb,mb,name)
                require(shape==shape_b,'view output extent changed')
                # float64 subtraction matches the scalar Python-float comparison exactly.
                error=float(np.max(np.abs(np.subtract(va,vb,dtype=np.float64)))) if np is not None else max(abs(x-y) for x,y in zip(va,vb))
                require(error<=1e-6,f'{source} changed {ca["route"]}/{phase}/{name}: {error}')
                result.append({'route':ca['route'],'phase':phase,'attachment':name,'maxError':error,'samples':len(va)})
    return {'passed':True,'provider':a['provider'],'quality':a['quality'],
            'comparison':'solo-repeat' if repeat else 'solo-interleaved','comparisons':result}

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--isolated',type=Path,required=True);ap.add_argument('--interleaved',type=Path,required=True);ap.add_argument('--repeat',action='store_true');ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    try: result=compare(args.isolated,args.interleaved,args.repeat)
    except (ValueError,KeyError,OSError,StopIteration) as e: result={'passed':False,'error':str(e)}
    args.output.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in result.items() if k!='comparisons'}))
    raise SystemExit(0 if result['passed'] else 1)
