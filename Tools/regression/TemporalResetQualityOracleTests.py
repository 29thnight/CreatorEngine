"""Corrupt copies of actual reset GPU evidence; never synthesize a PASS fixture."""
import argparse,array,hashlib,importlib.util,json,os,shutil
from pathlib import Path
from artifact_storage import require_capacity, prune_verified_mutation
SPEC=importlib.util.spec_from_file_location('reset',Path(__file__).resolve().parents[1]/'temporal-validation/Compare-TemporalResetQuality.py')
ORACLE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(ORACLE)

def clone(source,target):
    for p in source.rglob('*'):
        q=target/p.relative_to(source)
        if p.is_dir():q.mkdir(parents=True,exist_ok=True)
        else:
            q.parent.mkdir(parents=True,exist_ok=True)
            if p.suffix=='.json':shutil.copyfile(p,q)
            else:os.link(p,q)
def mutate_file(base,a,kind):
    p=base/a['file'];v=array.array('f');v.frombytes(p.read_bytes())
    if kind=='nan':v[0]=float('nan')
    elif kind=='truncate':v.pop()
    elif kind=='pixel':v[0]+=.1
    elif kind=='bias':
        for i in range(len(v)):
            if i%a['channels']<3:v[i]+=.5
    else:
        for i in range(len(v)):
            if a['channels']<4 or i%a['channels']<3:v[i]=0
    p.unlink();p.write_bytes(v.tobytes())

def main(source,target):
    require_capacity(target)
    target.mkdir(parents=True,exist_ok=False);assert ORACLE.compare(source)['passed']
    names=['reset-flag','reset-frame','ordinal','cut-revision','provider','quality','clock','fallback','generated','attachment-frame','missing-repeat','nan-hdr','truncated-display','constant-display','repeat-display-pixel','zero-source-hdr','zero-source-color','zero-source-depth','zero-sdk-hdr','biased-sdk-hdr']
    results=[]
    for name in names:
        root=target/name;clone(source,root);records=ORACLE.read(root/'records.json');record=records[-1] if name=='repeat-display-pixel' else records[0]
        base=root/record['name'];mp=base/'manifest.json';m=ORACLE.read(mp);h=m['historyRecovery']
        if name=='reset-flag':h['capturedReset']=False
        elif name=='reset-frame':h['resetRealFrameId']+=1
        elif name=='ordinal':h['ordinal']=8
        elif name=='cut-revision':h['cameraCutRevision']+=1
        elif name=='provider':m['upscaler']='none'
        elif name=='quality':h['upscaleQuality']=(h['upscaleQuality']+1)%5
        elif name=='clock':h['sdkFrameMilliseconds']=99
        elif name=='fallback':m['resolutionState']='native'
        elif name=='generated':m['frameKind']='generated';m['generatedOrdinal']=1
        elif name=='attachment-frame':m['attachments'][0]['realFrameId']+=1
        elif name=='missing-repeat':records.pop()
        else:
            if name=='zero-source-hdr':
                stages=m['diagnosticStages'];index=next(i for i,a in enumerate(stages) if a['name'].endswith('Temporal.Upscale'));mutate_file(base,stages[index-1],'zero')
            elif name in ('zero-sdk-hdr','biased-sdk-hdr'):
                kind='bias' if name=='biased-sdk-hdr' else 'zero'
                a=next(a for a in m['attachments'] if a['name']=='preToneHdr');mutate_file(base,a,kind)
                # Corrupt both SDK/output files. A nonconstant bias also tests
                # the independent input comparison instead of the empty gate.
                stage=next(a for a in m['diagnosticStages'] if a['name'].endswith('Temporal.Upscale'))
                if stage['file']!=a['file']:mutate_file(base,stage,kind)
                if name=='biased-sdk-hdr':
                    other=records[-1];otherbase=root/other['name'];othermp=otherbase/'manifest.json';otherm=ORACLE.read(othermp)
                    othera=next(a for a in otherm['attachments'] if a['name']=='preToneHdr');mutate_file(otherbase,othera,kind)
                    otherstage=next(a for a in otherm['diagnosticStages'] if a['name'].endswith('Temporal.Upscale'))
                    if otherstage['file']!=othera['file']:mutate_file(otherbase,otherstage,kind)
            else:
                attachment='baseColor' if name=='zero-source-color' else 'depth' if name=='zero-source-depth' else 'preToneHdr' if name=='nan-hdr' else 'display'
                a=next(a for a in m['attachments'] if a['name']==attachment)
                mutate_file(base,a,'nan' if name=='nan-hdr' else 'truncate' if name=='truncated-display' else 'pixel' if name=='repeat-display-pixel' else 'zero')
        mp.write_text(json.dumps(m));record['manifestSha256']=hashlib.sha256(mp.read_bytes()).hexdigest();(root/'records.json').write_text(json.dumps(records))
        try:r=ORACLE.compare(root);rejected=not r['passed'];reason=r.get('qualityFailures','unexpected acceptance')
        except (ValueError,KeyError,OSError,TypeError,IndexError) as e:rejected=True;reason=str(e)
        results.append(dict(mutation=name,rejected=rejected,reason=reason));assert rejected,name
        if name=='biased-sdk-hdr':assert isinstance(reason,list) and reason,'bias must fail the independent HDR quality threshold'
        prune_verified_mutation(root, source)
    assert ORACLE.compare(source)['passed']
    report=dict(passed=True,mutationCount=len(results),mutations=results);(target/'results.json').write_text(json.dumps(report,indent=2));print(json.dumps(dict(passed=True,mutationCount=len(results))))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--capture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();main(a.capture,a.output)
