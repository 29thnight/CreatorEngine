"""TR2 SDK route/render-display oracle. Native motion oracle remains strict."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path

_spec = importlib.util.spec_from_file_location('motion', Path(__file__).with_name('Compare-TemporalMotion.py'))
motion = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(motion)
require = motion.require


def halton(n, base):
    value, factor = 0., 1.
    while n:
        factor /= base
        value += factor * (n % base)
        n //= base
    return value - .5


def capture(base, reference, fixture, sprite):
    root = base / reference['path']
    raw = (root / 'manifest.json').read_bytes()
    require(hashlib.sha256(raw).hexdigest() == reference['manifestSha256'], 'manifest hash differs')
    m = json.loads(raw)
    extent, display = fixture['extent'], fixture['displayExtent']
    require(m['source'] == 'product-live' and m['backend'] == 'dx12' and
            m['captureMode'] == 'temporal-sdk-sequence-v1' and m['motionEvidence'] == 'exact-tagged-fixture', 'wrong capture mode')
    require(m['upscaler'] == fixture['provider'] and m['resolutionState'] == 'reconstructed' and
            m['frameGenerator'] == 'none' and m['spatialMode'] == 'off' and not m['deepDvcApplied'] and
            m['temporalNativeGateActive'] is False and m['frameKind'] == 'real' and m['generatedOrdinal'] == 0 and
            m['validationCount'] == 0, 'SDK output was substituted or invalid')
    require([m['renderWidth'], m['renderHeight']] == extent and [m['displayWidth'], m['displayHeight']] == display, 'wrong input/output extent')
    t = m['temporalMotion']
    require(t['sdkQuality'] and t['upscaleQuality'] == fixture['quality'] and
            abs(t['sdkFrameMilliseconds'] - 1000/60) < .001 and not t['historyReset'] and
            t['units'] == 'render-pixels' and t['direction'] == 'current-to-previous' and
            not t['jitterIncluded'], 'wrong temporal convention or selected quality')
    images, hashes = {}, {}
    for name, channels in [('baseColor', 4), ('depth', 1), ('temporalDepth', 1), ('temporalMotionRG', 2), ('temporalResponsive', 1)]:
        images[name], hashes[name] = motion.load_attachment(root, m, name, channels, extent)
    for name in ('preToneHdr', 'display'):
        images[name], hashes[name] = motion.load_attachment(root, m, name, 4, display)
    if sprite:
        images['postSpriteHdr'], hashes['postSpriteHdr'] = motion.load_sprite_stage(root, m, extent)
    # Associate the actual pre-upscale HDR input using recorded graph order.
    stages = m['diagnosticStages']
    indices = [i for i, s in enumerate(stages) if s['name'].endswith('-Temporal.Upscale')]
    require(len(indices) == 1 and indices[0] > 0, 'missing actual temporal upscale stage')
    stage = stages[indices[0]-1]
    require([stage['width'], stage['height']] == extent and stage['channels'] == 4, 'pre-upscale HDR is not render-sized')
    owner = next(s for s in stages if s['name'] == stage['readbackStage'])
    desc = dict(owner, name='sdkInput', realFrameId=m['realFrameId'], frameKind='real', generatedOrdinal=0)
    synthetic = {'realFrameId':m['realFrameId'], 'attachments':[desc]}
    images['sdkInput'], hashes['sdkInput'] = motion.load_attachment(root, synthetic, 'sdkInput', 4, extent)
    return m, images, hashes


def camera_proof(m, camera, display):
    near, far = float(camera['near']), float(camera['far'])
    aspect = display[0] / display[1]
    require(camera['projection'] == 'perspective' and camera['verticalFovDegrees'] == 90 and
            abs(camera['aspectRatio']-aspect) < 1e-6, 'wrong authored projection')
    expected = [1/aspect,0,0,0, 0,1,0,0, 0,0,far/(far-near),1, 0,0,-near*far/(far-near),0]
    require(max(abs(a-b) for a,b in zip(m['camera']['projection'],expected)) < 2e-5, 'projection does not use display aspect')
    require(max(abs(a-b) for a,b in zip(m['camera']['view'],[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1])) < 1e-6, 'wrong fixture camera')


def interior_quality_probes(case, camera, extent, probes, jitter, margin=3.):
    # A hard alpha/silhouette edge is intentionally antialiased by reconstruction.
    # Use only source-defined interiors, three render pixels from those contours;
    # output colors never influence selection. Input ownership keeps every probe.
    from collections import Counter
    indices = case['geometry']['indices']
    edges = Counter(tuple(sorted((tri[a], tri[(a+1)%3])))
                    for start in range(0, len(indices), 3)
                    for tri in [indices[start:start+3]] for a in range(3))
    outlines = [edge for edge, count in edges.items() if count == 1]
    selected = []
    for probe in probes:
        if probe.get('covered') is False:
            continue
        vertices = motion.posed_vertices(case, 'current', probe['instance'])
        screen = [[v+jitter[a] for a,v in enumerate(motion.project(point,camera,extent))] for point in vertices]
        center = [v+.5 for v in probe['pixel']]
        distance = min(motion.segment_distance(center, screen[a], screen[b]) for a,b in outlines)
        if min(distance, probe.get('alphaCoverageMarginPixels', float('inf'))) >= margin:
            selected.append(probe)
    expected = {p['instance'] for p in probes if p.get('covered') is not False}
    require(all(sum(p['instance']==instance for p in selected)>=3 for instance in expected),
            'insufficient reconstruction-interior probes for each authored instance')
    return selected


def output_proof(images, probes, extent, display, jitter):
    # Interior probes are authored triangle locations, never selected by output color.
    errors, contrasts = [], []
    for p in probes:
        if p.get('covered') is False:
            continue
        x,y = p['pixel']
        dx = min(display[0]-1,max(0,int((x+.5-jitter[0])*display[0]/extent[0])))
        dy = min(display[1]-1,max(0,int((y+.5-jitter[1])*display[1]/extent[1])))
        a = images['sdkInput'][4*(y*extent[0]+x):4*(y*extent[0]+x)+3]
        b = images['preToneHdr'][4*(dy*display[0]+dx):4*(dy*display[0]+dx)+3]
        scale = max(.03,max(a))
        errors.append(max(abs(v-w) for v,w in zip(a,b))/scale)
        contrasts.append(max(b)-min(b))
    require(errors and max(errors) <= .10, 'SDK interior color error exceeds 10 percent')
    require(max(contrasts) >= .03, 'SDK output is constant/empty')
    return {'maximumRelativeInteriorError':max(errors),'probes':len(errors)}


def compare(base):
    f = json.loads((base/'fixture.json').read_bytes())
    require(f['schema']=='temporal.resolution.routes.v1' and f['singleSubmissionPerMeasuredStep'], 'wrong route fixture')
    require(f['historyStart']=='fresh-sdk-context-per-route','SDK route initial state is not independent')
    require(set(c['route'] for c in f['cases'])==motion.ROUTES and len(f['cases'])==7,'all seven routes required')
    extent, display = f['extent'], f['displayExtent']
    require(all(type(v) is int and v>0 for v in extent+display) and all(a<=b for a,b in zip(extent,display)), 'invalid extents')
    require((extent==display) if f['quality']==0 else (extent[0]<display[0] and extent[1]<display[1]), 'quality input extent contract differs')
    for asset in f['inputAssets']:
        motion.artifact(base,asset)
    cases=[]
    for case in f['cases']:
        authored=json.loads(motion.artifact(base,case['input'])[0])
        for key in ('route','allowMeshlets','sessionId','geometry','previous','current','ownership','camera','extent','displayExtent','provider','quality','companionView','historyStart'):
            require(authored[key]==(case[key] if key in case else f[key]), 'authored route identity differs: '+key)
        current, images, hashes = capture(base,case['captures']['current'],f,case['route']=='sprite')
        control, disabled, _ = capture(base,case['captures']['control'],f,case['route']=='sprite')
        for m, phase in ((current,36),(control,37)):
            camera_proof(m,f['camera'],display)
            t=m['temporalMotion']
            require(abs(t['jitterX']-halton(phase,2))<1e-6 and abs(t['jitterY']-halton(phase,3))<1e-6 and
                    abs(t['previousJitterX']-halton(phase-1,2))<1e-6 and abs(t['previousJitterY']-halton(phase-1,3))<1e-6,'independent jitter phase drifted')
        motion.submission_proof(current,case,case['currentStepId'],case['baselineStepId'])
        motion.submission_proof(control,case,case['controlStepId'],case['currentStepId'])
        motion.measured_step_continuity(current,control)
        motion.geometry_route_proof(current,control,case)
        jitter=(current['temporalMotion']['jitterX'],current['temporalMotion']['jitterY'])
        probes=motion.geometry_probes(case,f['camera'],extent,jitter,True)
        if case['route']=='alpha':
            probes=motion.alpha_coverage(base,case,probes,f['inputAssets'],True)
        ownership=motion.ownership_result(case,probes,images,disabled,extent)
        require(all(p['passed'] for p in ownership),'route input/motion ownership rejected: '+case['route'])
        if f['companionView']:
            for name, step in (('current',case['currentStepId']),('control',case['controlStepId'])):
                other=case['companion'][name]
                require(other['sourceVerified'] and other['viewId']!=current['viewId'] and other['stepId']==step and
                        0<other['previousRealFrameId']<other['realFrameId'], 'companion SDK view did not own its history')
        interior=interior_quality_probes(case,f['camera'],extent,probes,jitter)
        quality=output_proof(images,interior,extent,display,jitter)
        quality['minimumContourDistanceRenderPixels']=3.
        quality['excludedBoundaryProbes']=sum(p.get('covered') is not False for p in probes)-len(interior)
        cases.append({'route':case['route'],'passed':True,'probes':ownership,'quality':quality,'hashes':hashes})
    return {'passed':True,'provider':f['provider'],'quality':f['quality'],'renderExtent':extent,'displayExtent':display,
            'companionView':f['companionView'],'cases':cases}


if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--capture',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    try:
        result=compare(args.capture)
    except (ValueError,KeyError,OSError,IndexError,StopIteration,motion.MissingProof) as error:
        result={'passed':False,'error':str(error)}
    args.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='cases'}))
    raise SystemExit(0 if result['passed'] else 1)
