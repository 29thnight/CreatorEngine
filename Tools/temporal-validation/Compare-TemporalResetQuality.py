"""First SDK reset submission: independent input/HDR interiors and repeatability.
The fixed-clock cube/floor fixture excludes two render pixels around boundaries.
Final display repeatability does not prove absolute tone mapping or scanout.
"""
import argparse, array, hashlib, importlib.util, json, math, statistics
from pathlib import Path

SPEC=importlib.util.spec_from_file_location('recovery',Path(__file__).with_name('Compare-LiveSSGIRecovery.py'))
RECOVERY=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(RECOVERY)
MAX_MEAN=.10
MAX_P95=.25
MIN_CONTRAST=.03
GUARD=2
FIXTURE=Path(__file__).parent/'fixtures/SSGIRecovery.gltf'

def require(ok,reason):
    if not ok: raise ValueError(reason)
def read(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def load(base,a):
    require(a['encoding']=='float32-le-row-major' and a['nonfinite']==0,'invalid readback descriptor')
    raw=(base/a['file']).read_bytes();v=array.array('f');v.frombytes(raw)
    require(len(v)==a['width']*a['height']*a['channels'] and all(math.isfinite(x) for x in v),'invalid GPU bytes')
    return a['width'],a['height'],a['channels'],v
def pixel(a,x,y):
    w,h,c,v=a;i=(y*w+x)*c;return v[i:i+c]
def median_rgb(values):return [statistics.median(p[c] for p in values) for c in range(3)]

def compare(root):
    inputs=read(root/'inputs.json');records=read(root/'records.json')
    require(inputs['schema']=='temporal.sdk.reset-quality.v1' and inputs['provider'] in ('fsr','dlss','xess'),'wrong reset fixture')
    require(digest(Path(inputs['fixturePath']))==inputs['fixtureSha256']==digest(FIXTURE),'authored cube/floor fixture changed')
    require(len(records)==2 and [r['name'] for r in records]==['reset-first','reset-repeat'] and all(r['ordinal']==1 and r['action']=='camera.cut' for r in records),'first-reset pair missing')
    results=[];captures=[];failures=[]
    for record in records:
        base=root/record['name'];m,images,hashes,_,_=RECOVERY.read_capture(root,record,inputs['provider'],inputs['quality'])
        require(digest(base/'manifest.json')==record['manifestSha256'],'reset manifest hash mismatch')
        h=m['historyRecovery'];require(h['previousSubmittedRealFrameId']==0 and m['sourceRealFrameId']==m['realFrameId']==h['resetRealFrameId'],'reset first-submission identity missing')
        require(m['generatedOrdinal']==0 and m['finite'] and m['skyBoxEnabled']==0,'unexpected scene/output')
        require(m['camera']['view']==[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],'reset camera pose changed')
        # Imported cube/floor material is independently authored, not inferred
        # from reconstructed output. Require the actual sealed draw and identity.
        require(len(m['draws'])==1 and m['draws'][0]['world']==[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],'fixture draw/transform changed')
        uniform=bytes(m['draws'][0]['lattice']['uniformBytes']);material=array.array('f');material.frombytes(uniform)
        require(max(abs(a-b) for a,b in zip(material[:4],[.8,.6,.3,1]))<1e-5,'sealed authored material mismatch')
        all_images={a['name']:load(base,a) for a in m['attachments']}
        require(all(a['frameKind']=='real' and a['realFrameId']==m['realFrameId'] and a['generatedOrdinal']==0 for a in m['attachments']),'mixed attachment frame identity')
        require(set(all_images)=={'baseColor','metalRough','normal','emissive','depth','preToneHdr','display'},'reset attachments missing')
        stages=m['diagnosticStages'];up=[i for i,a in enumerate(stages) if a['name'].endswith('Temporal.Upscale')]
        require(len(up)==1 and up[0]>0,'actual SDK stage missing')
        for a in stages:load(base,a)
        source=load(base,stages[up[0]-1]);sdk=load(base,stages[up[0]])
        depth=all_images['depth'];color=all_images['baseColor'];normal=all_images['normal'];out=all_images['preToneHdr']
        rw,rh=depth[:2];dw,dh=out[:2]
        require(source[:2]==(rw,rh) and sdk[:3]==out[:3] and all_images['display'][:2]==(dw,dh),'SDK input/output extent mismatch')
        require(all(a[:2]==((dw,dh) if n in ('preToneHdr','display') else (rw,rh)) for n,a in all_images.items()),'attachment extent mismatch')
        require(sdk[3]==out[3],'captured HDR differs from actual SDK stage')
        fg=[];bg=[];mask={}
        for y in range(GUARD,rh-GUARD):
            for x in range(GUARD,rw-GUARD):
                d=pixel(depth,x,y)[0];c=pixel(color,x,y);n=pixel(normal,x,y)
                owner=1 if d<.999 and max(abs(a-b) for a,b in zip(c[:3],[.8,.6,.3]))<.04 else 0 if d==1 and max(abs(v) for v in c[:3])<.001 else -1
                if owner<0:continue
                center=pixel(source,x,y)[:3];valid=True
                for dx,dy in [(-2,0),(2,0),(0,-2),(0,2),(-2,-2),(2,-2),(-2,2),(2,2)]:
                    q=pixel(depth,x+dx,y+dy)[0]
                    if (q<.999)!=bool(owner) or max(abs(a-b) for a,b in zip(pixel(color,x+dx,y+dy)[:3],c[:3]))>.04 or max(abs(a-b) for a,b in zip(pixel(normal,x+dx,y+dy)[:3],n[:3]))>.03 or max(abs(a-b) for a,b in zip(pixel(source,x+dx,y+dy)[:3],center))>.02:
                        valid=False;break
                if not valid:continue
                mask[x,y]=owner;(fg if owner else bg).append(center)
        require(len(fg)>=64 and len(bg)>=64,f'authored foreground/background interiors absent: foreground={len(fg)} background={len(bg)} minimum=64')
        contrast=max(abs(a-b) for a,b in zip(median_rgb(fg),median_rgb(bg)));require(contrast>=MIN_CONTRAST,'input HDR lacks foreground/background contrast')
        errors=[[],[]]
        # Reset sample zero has Halton jitter (0,-1/6). Only input-defined
        # geometric/material/smooth HDR interiors select the measured pixels.
        for y in range(dh):
            ry=int((y+.5)*rh/dh-1/6)
            for x in range(dw):
                rx=int((x+.5)*rw/dw)
                if (rx,ry) not in mask:continue
                error=max(abs(a-b) for a,b in zip(pixel(source,rx,ry)[:3],pixel(out,x,y)[:3]))/contrast
                errors[mask[rx,ry]].append(error)
        metrics=[]
        for owner,values in enumerate(errors):
            require(len(values)>=64,'empty reset quality mask');ordered=sorted(values);mean=statistics.mean(values);p95=ordered[int(.95*len(ordered))]
            if mean>MAX_MEAN or p95>MAX_P95:failures.append(dict(capture=record['name'],region='foreground' if owner else 'background',mean=mean,p95=p95))
            metrics.append(dict(region='foreground' if owner else 'background',inputPixels=len(fg) if owner else len(bg),pixels=len(values),mean=mean,p95=p95))
        results.append(dict(name=record['name'],resetRealFrameId=h['resetRealFrameId'],historyRevision=h['historyRevision'],cameraCutRevision=h['cameraCutRevision'],renderExtent=[rw,rh],displayExtent=[dw,dh],contrast=contrast,regions=metrics,attachmentHashes={a['name']:digest(base/a['file']) for a in m['attachments']},preUpscaleSha256=digest(base/stages[up[0]-1]['file'])))
        captures.append((m,images))
    a,b=captures
    require(a[0]['viewId']==b[0]['viewId'] and a[0]['sceneEpoch']==b[0]['sceneEpoch'] and a[0]['camera']==b[0]['camera'],'repeat changed view/scene/camera')
    require(b[0]['historyRecovery']['historyRevision']>a[0]['historyRecovery']['historyRevision'] and b[0]['sourceRealFrameId']>a[0]['sourceRealFrameId'],'repeat did not cause a new reset')
    repeat={n:RECOVERY.compare(a[1],b[1],n,1e-6) for n in ['ssgi','reconstructed','display']}
    return dict(schema='temporal.sdk.reset-quality.evidence.v1',passed=not failures,provider=inputs['provider'],quality=inputs['quality'],thresholds=dict(maxMean=MAX_MEAN,maxP95=MAX_P95,minContrast=MIN_CONTRAST,guardRenderPixels=GUARD,maxLocalHdrVariation=.02),captures=results,qualityFailures=failures,repeatMaxErrors=repeat,scope='actual camera-cut reset submission HDR interiors and repeated-reset GPU display; no first object appearance, silhouette or absolute tone-map acceptance')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    try:r=compare(args.directory)
    except (ValueError,KeyError,OSError,TypeError,IndexError) as e:r=dict(passed=False,error=str(e))
    args.output.write_text(json.dumps(r,indent=2));print(json.dumps(r));raise SystemExit(0 if r['passed'] else 1)
