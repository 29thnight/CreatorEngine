"""Independent perspective interior/disocclusion oracle for executed SDK sequences.
Explicit 60 Hz SDK packets; capture readback pacing is not physical display pacing.
Thresholds are declared here before measuring GPU results.
"""
import argparse, array, hashlib, json, math, statistics
from pathlib import Path

MAX_MEAN_GHOST = .10
MAX_P95_GHOST = .25
MIN_CONTRAST = .03
GUARD_RENDER_PIXELS = 2

def require(ok, message):
    if not ok: raise ValueError(message)

def read_json(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def load_image(base, desc):
    path = base / desc['file']
    require(desc['encoding'] == 'float32-le-row-major', 'unexpected image encoding')
    raw = path.read_bytes(); values = array.array('f'); values.frombytes(raw)
    require(len(values) == desc['width'] * desc['height'] * desc['channels'], 'truncated image')
    require(all(math.isfinite(v) for v in values), 'nonfinite GPU pixels')
    return dict(values=values, width=desc['width'], height=desc['height'], channels=desc['channels'], sha256=hashlib.sha256(raw).hexdigest())
def pixel(image, x, y):
    offset = (y * image['width'] + x) * image['channels']
    return image['values'][offset:offset+image['channels']]
def rectangle(pose, width, height, jitter=(0,0)):
    x,y,z = pose['position']; scale = height / (2*z)
    return ((x-1)*scale+width/2+jitter[0], (y-1)*-scale+height/2+jitter[1],
            (x+1)*scale+width/2+jitter[0], (y+1)*-scale+height/2+jitter[1])
def inside(x,y,r,guard=0):
    left,bottom,right,top=r
    return left+guard <= x+.5 < right-guard and top+guard <= y+.5 < bottom-guard

def region(width,height,*rectangles):
    left=max(0,math.floor(min(r[0] for r in rectangles)))
    right=min(width,math.ceil(max(r[2] for r in rectangles)))
    top=max(0,math.floor(min(r[3] for r in rectangles)))
    bottom=min(height,math.ceil(max(r[1] for r in rectangles)))
    return range(left,right),range(top,bottom)

def background_proof(color, depth, x, y):
    require(max(abs(v-.125) for v in pixel(color,x,y)[:3])<.04, 'disocclusion source is not the authored background')
    require(abs(pixel(depth,x,y)[0]-(20/19.9-2/19.9/6))<2e-5, 'disocclusion background depth wrong')

def compare(root, expected_quality=None, require_baseline=False, require_startup=False, require_exact_warmup=False):
    fixture=read_json(root/'fixture.json')
    startup=fixture['schema']=='temporal.sdk.startup.v1'
    version2=startup or fixture['schema']=='temporal.sdk.sequence.v2'
    require((version2 or fixture['schema']=='temporal.sdk.sequence.v1') and fixture['singleSubmissionPerMeasuredStep']==1, 'wrong executed fixture')
    require(not require_baseline or (version2 and not startup), 'verified warmed SDK baseline required')
    require(not require_startup or startup, 'SDK startup evidence required')
    source=root/fixture['input']['path']; require(digest(source)==fixture['input']['sha256'], 'authored sequence hash mismatch')
    inputs=read_json(source); require(inputs['camera']['projection']=='perspective' and inputs['camera']['verticalFovDegrees']==90, 'wrong camera')
    require(not require_exact_warmup or ('exactWarmupSubmissions' in inputs and 'warmupSource' in inputs), 'exact warmup contract required')
    require(inputs['provider'] in ('fsr','dlss','xess') and inputs['quality'] in range(5), 'invalid SDK quality')
    if expected_quality is not None: require(inputs['quality']==expected_quality, 'requested quality mismatch')
    require(abs(inputs['sdkFrameMilliseconds']-1000/60)<.001, 'authored SDK clock mismatch')
    require(inputs['camera']==fixture['camera'] and inputs['camera']['position']==[0,0,0] and inputs['camera']['near']==.1 and inputs['camera']['far']==20 and inputs['foregroundHalfSize']==1 and inputs['backgroundZ']==6, 'authored camera/geometry mismatch')
    require(len(inputs['steps'])==(18 if version2 else 17) and len(fixture['captures'])==16, 'incomplete sequence')
    require(inputs['steps'][0]['position']==[-2,0,4], 'wrong baseline')
    for i,pose in enumerate(inputs['steps']):
        pose_index=max(0,i-1) if version2 else i
        require(pose['stepId']==i+1 and pose['visible']==(pose_index<=8) and pose['position']==[-2+.5*min(pose_index,8),0,4], 'authored motion changed')
    for asset in inputs['inputAssets']:
        require(digest(root/asset['path'])==asset['sha256'], 'authored asset hash mismatch')
    report=dict(schema='temporal.sdk.startup.evidence.v1' if startup else 'temporal.sdk.sequence.evidence.v2' if version2 else 'temporal.sdk.sequence.evidence.v1',passed=False,provider=inputs['provider'],quality=inputs['quality'],displayExtent=inputs['extent'],
        thresholds=dict(maxMeanGhost=MAX_MEAN_GHOST,maxP95Ghost=MAX_P95_GHOST,minContrast=MIN_CONTRAST,guardRenderPixels=GUARD_RENDER_PIXELS),qualityFailures=[],frames=[])
    previous=None; identities=None
    if version2:
        minimum=1 if startup else 32
        require(inputs['warmupMinAcceptedSubmissions']==minimum and len(fixture['warmup'])>=minimum, 'SDK baseline warmup missing')
        if startup:
            require(inputs['historyObservationMode']=='first-observed-non-reset', 'startup observation mode missing')
            require(len(fixture['warmup'])<32, 'startup acquired warmed observation count')
        warmup=fixture['warmup']
        require(all(a['realFrameId']<b['realFrameId'] and a['sourceFrameId']<b['sourceFrameId'] for a,b in zip(warmup,warmup[1:])), 'warmup repeats or regresses')
        if 'exactWarmupSubmissions' in inputs:
            exact=inputs["exactWarmupSubmissions"]
            require(isinstance(exact,int) and (exact==1 if startup else 32<=exact<=128), "invalid exact warmup bound")
            require(inputs['exactWarmupSubmissions']==exact and len(warmup)==exact and
                inputs['warmupSource']=='render-thread-submission-ledger', 'exact submitted warmup missing')
    reference_display=None
    if version2:
        reference_record=fixture['captures'][-1];reference_base=root/reference_record['path']
        require(digest(reference_base/'manifest.json')==reference_record['manifestSha256'], 'background reference manifest hash mismatch')
        reference_manifest=read_json(reference_base/'manifest.json')
        reference_display=load_image(reference_base,next(a for a in reference_manifest['attachments'] if a['name']=='display'))
        require((reference_display['width'],reference_display['height'])==tuple(inputs['extent']), 'background reference extent mismatch')
        report['displayReference']=dict(path=reference_record['path'],sha256=reference_display['sha256'],mode='same-pixel-final-background-recovery')
    records=([fixture['baseline']] if version2 else [])+fixture['captures']
    for index,record in enumerate(records,1):
        logical_index=index-1 if version2 else index
        base=root/record['path']; require(digest(base/'manifest.json')==record['manifestSha256'], 'capture manifest hash mismatch')
        m=read_json(base/'manifest.json'); t=m['temporalMotion']; s=m['fixtureSubmission']
        require(m['captureMode']=='temporal-sdk-sequence-v1' and m['upscaler']==inputs['provider'] and
            m['frameKind']=='real' and m['resolutionState']=='reconstructed' and not m['temporalNativeGateActive'] and
            m['goldenEligible']==0 and m['frameGenerator']=='none' and m['spatialMode']=='off' and not m['deepDvcApplied'], 'SDK provenance mismatch or fallback')
        require(m['validationCount']==0 and m['finite'] and t['sdkQuality'] and t['upscaleQuality']==inputs['quality'] and
            abs(t['sdkFrameMilliseconds']-1000/60)<.001 and not t['historyReset'], 'clock/reset/validation mismatch')
        require(record['stepId']==index+1==s['stepId']==t['fixtureStepId'] and s['sessionId']==fixture['sessionId'] and
            s['valid'] and s['sourceVerified'] and not s['coalesced'] and not s['historyReset'] and
            s['predecessorStepId']==index==t['fixturePredecessorStepId'] and
            s['inputSha256']==s['predecessorInputSha256']==fixture['input']['sha256'] and
            s['realFrameId']==m['realFrameId'] and s['sourceFrameId']==m['frameId'], 'exact submitted pose proof missing')
        require(m['generatedOrdinal']==0 and m['backend']=='dx12' and m['source']=='product-live' and
            m['fixtureSessionId']==t['fixtureSessionId']==fixture['sessionId'] and
            m['fixtureStepId']==t['fixtureStepId']==index+1 and m['fixturePredecessorStepId']==index and
            t['previousSubmittedFixtureSessionId']==fixture['sessionId'] and t['previousSubmittedFixtureStepId']==index and
            t['previousSubmittedFixtureSourceVerified'] and
            all(value==fixture['input']['sha256'] for value in (m['fixtureInputSha256'],m['fixturePredecessorInputSha256'],
                t['fixtureInputSha256'],t['fixturePredecessorInputSha256'],t['previousSubmittedFixtureInputSha256'])) and
            s['historyGeneration']==t['historyGeneration']==t['previousSubmittedHistoryGeneration'] and
            s['historyRevision']==m['historyRevision']==t['previousSubmittedHistoryRevision'] and
            s['viewId']==m['viewId']==t['previousSubmittedViewId'] and
            s['sceneEpoch']==m['sceneEpoch']==t['previousSubmittedSceneEpoch'], 'mixed source/view/history or submitted input')
        require(s['predecessorRealFrameId']==t['previousSubmittedRealFrameId'] and
            s['predecessorSourceFrameId']==t['previousSubmittedSourceFrameId'], 'incoming submission identity mismatch')
        if previous:
            require(t['previousSubmittedRealFrameId']==previous['realFrameId'] and
                t['previousSubmittedSourceFrameId']==previous['frameId'], 'intervening held pose advanced history')
        identity=(m['viewId'],m['sceneEpoch'],m['historyRevision'],t['historyGeneration'])
        if identities: require(identities==identity, 'sequence history identity changed')
        if version2 and logical_index==0:
            require(all((w['viewId'],w['sceneEpoch'],w['historyRevision'],w['historyGeneration'])==identity for w in warmup), 'warmup mixed history')
            require(t['previousSubmittedRealFrameId']>=warmup[-1]['realFrameId'] and t['previousSubmittedSourceFrameId']>=warmup[-1]['sourceFrameId'], 'baseline precedes observed warmup')
        if version2 and logical_index==0:
            report['historyObservationMode']='first-observed-non-reset' if startup else 'warmed'
            report['baselineAfterFirstObservedRealFrames']=m['realFrameId']-warmup[0]['realFrameId']
            report['baselineAfterLastObservedRealFrames']=m['realFrameId']-warmup[-1]['realFrameId']
            if startup: require(0<report['baselineAfterFirstObservedRealFrames']<32, 'startup baseline arrived after warmed interval')
        identities=identity;previous=m
        images={a['name']:load_image(base,a) for a in m['attachments']}
        require(len(images)==12, 'motion/output readback missing')
        output=images['preToneHdr']; display=images['display']; depth=images['depth']; color=images['baseColor']; motion=images['temporalMotionRG']
        rw,rh=depth['width'],depth['height']; dw,dh=output['width'],output['height']
        require((dw,dh)==tuple(inputs['extent']) and ((rw==dw and rh==dh) if inputs['quality']==0 else (rw<dw and rh<dh)) and (display['width'],display['height'])==(dw,dh), 'render/display dimensions wrong')
        if 'renderExtent' in report: require(report['renderExtent']==[rw,rh], 'render extent changed during sequence')
        report['renderExtent']=[rw,rh]
        for name,image in images.items():
            require((image['width'],image['height'])==((dw,dh) if name in ('preToneHdr','display') else (rw,rh)), 'attachment extent mismatch')
        stages=m['diagnosticStages']; upscale=[i for i,a in enumerate(stages) if a['name'].endswith('Temporal.Upscale')]
        require(len(upscale)==1 and upscale[0]>0, 'actual upscale stage missing')
        hdr_input=load_image(base,stages[upscale[0]-1]); require((hdr_input['width'],hdr_input['height'])==(rw,rh), 'SDK input HDR extent wrong')
        jitter=(t['jitterX'],t['jitterY']); require(all(math.isfinite(v) and abs(v)<=.5 for v in jitter), 'invalid jitter')
        if 'exactWarmupSubmissions' in inputs:
            def halton(n,base):
                value=0.;factor=1.
                while n:
                    factor/=base;value+=factor*(n%base);n//=base
                return value-.5
            # One reset sample, exactly N non-reset baseline submissions,
            # then one submitted baseline capture and each measured pose.
            sample=inputs['exactWarmupSubmissions']+2+logical_index
            require(abs(jitter[0]-halton(sample,2))<1e-6 and abs(jitter[1]-halton(sample,3))<1e-6,
                'SDK jitter phase drifted from the exact warmup boundary')
            require(abs(t['previousJitterX']-halton(sample-1,2))<1e-6 and
                abs(t['previousJitterY']-halton(sample-1,3))<1e-6, 'SDK previous jitter phase drifted')
        current=inputs['steps'][index]; prior=inputs['steps'][index-1]
        raster=rectangle(current,rw,rh,jitter); expected=rectangle(current,dw,dh); old=rectangle(prior,dw,dh)
        fg=[]; bg=[]; owners=0; vectors=0
        xs,ys=region(rw,rh,raster,(rw//5,rh*2/3,rw//4,rh//3))
        for y in ys:
            for x in xs:
                if current['visible'] and inside(x,y,raster,GUARD_RENDER_PIXELS):
                    require(max(abs(a-b) for a,b in zip(pixel(color,x,y)[:3],[.75,.125,.25]))<.04, 'foreground ownership pixel wrong')
                    require(abs(pixel(depth,x,y)[0]-(20/19.9-2/19.9/4))<2e-5, 'foreground depth wrong')
                    fg.append(pixel(hdr_input,x,y)[:3]); owners+=1
                    mv=pixel(motion,x,y); expected_mv=[-.5*rh/8,0] if 1<=logical_index<=8 else [0,0]
                    require(max(abs(a-b) for a,b in zip(mv,expected_mv))<.08, 'real motion vector wrong');vectors+=1
                elif rw//5<x<rw//4 and rh//3<y<2*rh//3:
                    require(max(abs(v-.125) for v in pixel(color,x,y)[:3])<.04, 'background ownership pixel wrong')
                    bg.append(pixel(hdr_input,x,y)[:3])
        require(bg and (not current['visible'] or owners>=16), 'empty ownership region')
        bg_rgb=[statistics.median(p[c] for p in bg) for c in range(3)]
        if fg:
            fg_rgb=[statistics.median(p[c] for p in fg) for c in range(3)]
            contrast=max(abs(a-b) for a,b in zip(fg_rgb,bg_rgb));require(contrast>=MIN_CONTRAST, 'HDR scene lacks contrast')
            report['lastContrast']=contrast
        contrast=report.get('lastContrast',0);require(contrast>=MIN_CONTRAST,'no preceding foreground contrast')
        display_background=[pixel(display,x,y)[:3] for y in range(dh//3,2*dh//3) for x in range(dw//5+1,dw//4)]
        display_bg=[statistics.median(p[c] for p in display_background) for c in range(3)]
        if current['visible']:
            xs,ys=region(dw,dh,expected)
            display_foreground=[pixel(display,x,y)[:3] for y in ys for x in xs if inside(x,y,expected,GUARD_RENDER_PIXELS*dh/rh)]
            display_fg=[statistics.median(p[c] for p in display_foreground) for c in range(3)]
            final_contrast=max(abs(a-b) for a,b in zip(display_fg,display_bg))
            require(final_contrast>=MIN_CONTRAST, 'final display lacks moving foreground contrast')
            report['lastDisplayContrast']=final_contrast
        final_contrast=report['lastDisplayContrast']
        display_errors=[]
        errors=[]; foreground_errors=[]; guard=GUARD_RENDER_PIXELS*dh/rh
        xs,ys=region(dw,dh,expected,old)
        for y in ys:
            for x in xs:
                exposed=prior['visible'] and inside(x,y,old,guard) and (not current['visible'] or not inside(x,y,expected,-guard))
                interior=current['visible'] and inside(x,y,expected,guard)
                if not exposed and not interior: continue
                rx=min(rw-1,max(0,int((x+.5)*rw/dw+jitter[0])));ry=min(rh-1,max(0,int((y+.5)*rh/dh+jitter[1])))
                truth=pixel(hdr_input,rx,ry)[:3];observed=pixel(output,x,y)[:3]
                error=max(abs(a-b) for a,b in zip(truth,observed))/contrast
                if exposed:
                    background_proof(color,depth,rx,ry)
                    errors.append(error)
                    display_errors.append(max(abs(a-b) for a,b in zip(pixel(display,x,y)[:3],pixel(reference_display,x,y)[:3] if reference_display is not None else display_bg))/final_contrast)
                if interior: foreground_errors.append(error)
        # Disappearance frames 10..16 inspect the last foreground footprint.
        if logical_index>9:
            xs,ys=region(dw,dh,rectangle(inputs['steps'][9 if version2 else 8],dw,dh))
            for y in ys:
                for x in xs:
                    if inside(x,y,rectangle(inputs['steps'][9 if version2 else 8],dw,dh),guard):
                        rx=min(rw-1,max(0,int((x+.5)*rw/dw+jitter[0])));ry=min(rh-1,max(0,int((y+.5)*rh/dh+jitter[1])))
                        background_proof(color,depth,rx,ry)
                        errors.append(max(abs(a-b) for a,b in zip(pixel(hdr_input,rx,ry)[:3],pixel(output,x,y)[:3]))/contrast)
                        display_errors.append(max(abs(a-b) for a,b in zip(pixel(display,x,y)[:3],pixel(reference_display,x,y)[:3] if reference_display is not None else display_bg))/final_contrast)
        if version2 and logical_index==0:
            errors=foreground_errors
            xs,ys=region(dw,dh,expected)
            display_errors=[max(abs(a-b) for a,b in zip(pixel(display,x,y)[:3],display_fg))/final_contrast for y in ys for x in xs if inside(x,y,expected,guard)]
        require(len(errors)>=16,'empty disocclusion mask')
        ordered=sorted(errors);mean=statistics.mean(errors);p95=ordered[min(len(ordered)-1,int(.95*len(ordered)))]
        if mean>MAX_MEAN_GHOST or p95>MAX_P95_GHOST:
            report['qualityFailures'].append(dict(step=logical_index,output='hdr',mean=mean,p95=p95))
        display_mean=statistics.mean(display_errors);display_ordered=sorted(display_errors)
        display_p95=display_ordered[min(len(display_ordered)-1,int(.95*len(display_ordered)))]
        if display_mean>MAX_MEAN_GHOST or display_p95>MAX_P95_GHOST:
            report['qualityFailures'].append(dict(step=logical_index,output='display',mean=display_mean,p95=display_p95))
        if foreground_errors and statistics.mean(foreground_errors)>MAX_P95_GHOST:
            report['qualityFailures'].append(dict(step=logical_index,output='foreground',mean=statistics.mean(foreground_errors)))
        require(max(display['values'][0::4])-min(display['values'][0::4])>.005 or logical_index>8, 'constant final display')
        frame_report=dict(step=logical_index,realFrameId=m['realFrameId'],predecessorRealFrameId=t['previousSubmittedRealFrameId'],
            exposedPixels=len(errors),foregroundPixels=owners,motionProbes=vectors,meanGhost=mean,p95Ghost=p95,meanDisplayGhost=display_mean,p95DisplayGhost=display_p95,
            attachmentHashes={name:image['sha256'] for name,image in images.items()})
        if version2 and logical_index==0: report['baseline']=frame_report;report['warmupObservedSubmissions']=len(warmup)
        else: report['frames'].append(frame_report)
    report['passed']=not report['qualityFailures'];return report

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--capture',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--quality',choices=['native-aa','quality','balanced','performance','ultra-performance']);parser.add_argument('--require-baseline',action='store_true');parser.add_argument('--require-startup',action='store_true');parser.add_argument('--require-exact-warmup',action='store_true');args=parser.parse_args()
    try: report=compare(args.capture, ['native-aa','quality','balanced','performance','ultra-performance'].index(args.quality) if args.quality else None, args.require_baseline,args.require_startup,args.require_exact_warmup)
    except (ValueError,KeyError,OSError,IndexError,TypeError) as error: report=dict(passed=False,error=str(error))
    args.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='frames'}));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
