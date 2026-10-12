"""Mutation regressions of existing executed GPU evidence; does not generate GPU evidence."""
import argparse, array, hashlib, importlib.util, json, os, shutil
from pathlib import Path
from artifact_storage import require_capacity, prune_verified_mutation
SPEC=importlib.util.spec_from_file_location('oracle',Path(__file__).resolve().parents[1]/'temporal-validation/Compare-TemporalSdkSequence.py')
ORACLE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(ORACLE)

def clone(source,target):
    for p in source.rglob('*'):
        q=target/p.relative_to(source)
        if p.is_dir(): q.mkdir(parents=True,exist_ok=True)
        else:
            q.parent.mkdir(parents=True,exist_ok=True)
            if p.suffix=='.json': shutil.copyfile(p,q)
            else: os.link(p,q)
def rewrite(path,data):
    # Detach the mutation path before writing a hard-linked binary.
    path.unlink();path.write_bytes(data)
def read_schema(root): return json.loads((root/'fixture.json').read_text())['schema']

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--capture',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mutation',action='append',help='Run named mutations only; report retains the explicit scope')
    args=parser.parse_args()
    require_capacity(args.output)
    args.output.mkdir(parents=True,exist_ok=False)
    original=ORACLE.compare(args.capture);assert original['passed']
    mutations=['quality','provider','clock','reset','predecessor','session','constant-hdr','constant-display','zero-motion','bad-depth','bad-color','nan-hdr','truncated-display']
    if read_schema(args.capture) in ('temporal.sdk.sequence.v2','temporal.sdk.startup.v1'):
        mutations+=['warmup-short','warmup-repeat','warmup-history','warmup-after-baseline','baseline-missing','baseline-hdr','baseline-motion','recovery-display','recovery-hdr','recovery-color']
    if read_schema(args.capture)=='temporal.sdk.startup.v1': mutations+=['startup-late-baseline','startup-warmed-count']
    inputs=json.loads((args.capture/'input-assets/sdk-sequence.json').read_text())
    exact_warmup='exactWarmupSubmissions' in inputs
    if exact_warmup: mutations+=['exact-warmup-short','exact-warmup-metadata','jitter-phase','previous-jitter-phase']
    if args.mutation:
        assert set(args.mutation)<=set(mutations), 'unknown mutation or incompatible capture'
        mutations=list(dict.fromkeys(args.mutation))
    results=[]
    for name in mutations:
        target=args.output/name;clone(args.capture,target)
        fixture=json.loads((target/'fixture.json').read_text());record=fixture['baseline'] if name in ('baseline-hdr','baseline-motion') else fixture['captures'][-1] if name.startswith('recovery-') else fixture['captures'][1];base=target/record['path'];mp=base/'manifest.json';m=json.loads(mp.read_text())
        if name=='exact-warmup-short':fixture['warmup']=fixture['warmup'][1:]
        elif name=='exact-warmup-metadata':
            ip=target/fixture['input']['path']; data=json.loads(ip.read_text())
            del data['exactWarmupSubmissions'];del data['warmupSource']
            ip.write_text(json.dumps(data));fixture['input']['sha256']=hashlib.sha256(ip.read_bytes()).hexdigest()
        elif name=='jitter-phase':m['temporalMotion']['jitterX']+=.0001
        elif name=='previous-jitter-phase':m['temporalMotion']['previousJitterX']+=.0001
        elif name=='startup-late-baseline':fixture['warmup'][0]['realFrameId']-=100;fixture['warmup'][0]['sourceFrameId']-=100
        elif name=='startup-warmed-count':fixture['warmup']*=32
        elif name=='warmup-short':fixture['warmup']=[] if read_schema(args.capture)=='temporal.sdk.startup.v1' else fixture['warmup'][:31]
        elif name=='warmup-repeat':fixture['warmup'].append(fixture['warmup'][0])
        elif name=='warmup-history':fixture['warmup'][0]['historyRevision']+=1
        elif name=='warmup-after-baseline':fixture['warmup'][-1]['realFrameId']+=1000000;fixture['warmup'][-1]['sourceFrameId']+=1000000
        elif name=='baseline-missing':del fixture['baseline']
        elif name=='quality':m['temporalMotion']['upscaleQuality']=(m['temporalMotion']['upscaleQuality']+1)%5
        elif name=='provider':m['upscaler']='none'
        elif name=='clock':m['temporalMotion']['sdkFrameMilliseconds']=99
        elif name=='reset':m['temporalMotion']['historyReset']=True
        elif name=='predecessor':
            m['temporalMotion']['previousSubmittedRealFrameId']-=1;m['fixtureSubmission']['predecessorRealFrameId']-=1
        elif name=='session':m['fixtureSubmission']['sessionId']+=1
        else:
            attachment={'recovery-display':'display','recovery-hdr':'preToneHdr','recovery-color':'baseColor','baseline-hdr':'preToneHdr','baseline-motion':'temporalMotionRG','constant-hdr':'preToneHdr','constant-display':'display','zero-motion':'temporalMotionRG','bad-depth':'depth','bad-color':'baseColor','nan-hdr':'preToneHdr','truncated-display':'display'}[name]
            a=next(a for a in m['attachments'] if a['name']==attachment);p=base/a['file'];data=array.array('f');data.frombytes(p.read_bytes())
            if name=='baseline-motion':
                for i in range(len(data)): data[i]=1
            elif name.startswith('recovery-') or name=='baseline-hdr' or name.startswith('constant') or name in ('zero-motion','bad-depth','bad-color'):
                for i in range(len(data)):
                    if a['channels']<4 or i%a['channels']<3:data[i]=0
            elif name=='nan-hdr':data[0]=float('nan')
            elif name=='truncated-display':data.pop()
            rewrite(p,data.tobytes())
        mp.write_text(json.dumps(m));record['manifestSha256']=hashlib.sha256(mp.read_bytes()).hexdigest();(target/'fixture.json').write_text(json.dumps(fixture))
        try:r=ORACLE.compare(target,require_exact_warmup=exact_warmup);rejected=not r['passed'];error='unexpected acceptance'
        except (ValueError,KeyError,OSError,IndexError,TypeError) as ex:rejected=True;error=str(ex)
        results.append(dict(mutation=name,rejected=rejected,error=error));assert rejected,name
        # Unexpected acceptance keeps its full pixels for investigation.
        prune_verified_mutation(target, args.capture)
    # Hard-linked mutations must leave their source evidence usable.
    assert ORACLE.compare(args.capture)['passed']
    report=dict(passed=True,mutationCount=len(results),mutations=results)
    (args.output/'results.json').write_text(json.dumps(report,indent=2));print(json.dumps(dict(passed=True,mutationCount=len(results))))
if __name__=='__main__':main()
