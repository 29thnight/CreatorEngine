"""Independent checks of live reset identities and GPU history/display bytes."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def read_capture(root, record, provider='none', quality='quality'):
    path = root / record['name']
    manifest_path = path / 'manifest.json'
    m = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    h = m['historyRecovery']
    ordinal = record['ordinal']
    require(m['source'] == 'product-live' and m['backend'] == 'dx12' and
            m['captureMode'] == 'temporal-history-recovery-v1' and
            m['frameKind'] == 'real' and m['upscaler'] == provider and
            m['frameGenerator'] == 'none' and m['spatialMode'] == 'off' and not m['deepDvcApplied'] and
            m['resolutionState'] == ('native' if provider == 'none' else 'reconstructed') and
            m['temporalNativeGateActive'] is False and
            type(m['goldenEligible']) in (bool, int) and m['goldenEligible'] == 0,
            'capture changed the temporal mode or mixed golden/SDK evidence')
    if provider != 'none':
        require(h['upscaleQuality'] == ['native-aa','quality','balanced','performance','ultra-performance'].index(quality),
                'captured SDK quality differs from the requested test case')
        require(h['fixedSdkClock'] is True and abs(h['sdkFrameMilliseconds'] - 1000/60) < 1e-4,
                'SDK recovery comparison requires explicitly matched SDK clock')
        for which in ('before', 'after'):
            snapshot = record[which]
            expected_quality = record['fromQuality'] if which == 'before' and record['action'] == 'temporal.upscale' else quality
            require(snapshot['activeUpscaler'] == provider and snapshot['activeUpscaleQuality'] == expected_quality and
                    snapshot['temporalAaApplied'] is True and snapshot['fxaaApplied'] is False,
                    'provider/quality changed or fallback/FXAA substituted')
        require((m['renderWidth'], m['renderHeight']) == (m['displayWidth'], m['displayHeight'])
                if quality == 'native-aa' else
                m['renderWidth'] < m['displayWidth'] and m['renderHeight'] < m['displayHeight'],
                'wrong native-AA/upscale extents')
    require(m['validationCount'] == 0 and m['draws'], 'invalid or empty live scene')
    require(h['ordinal'] == ordinal and h['acceptedBefore'] == ordinal - 1 and
            h['capturedReset'] is (ordinal == 1) and h['viewId'] == m['viewId'] and
            h['sceneEpoch'] == m['sceneEpoch'] and h['historyRevision'] > int(record['before']['historyRevision']) and
            m['viewId'] == int(record['before']['viewId']) and m['sceneEpoch'] == int(record['before']['sceneEpoch']) and
            h['historyRevision'] == int(record['after']['historyRevision']) and
            m['frameId'] > m['requestedAfterFrameId'], 'reset/capture identity mismatch')
    if ordinal == 1:
        require(h['resetRealFrameId'] == m['sourceRealFrameId'], 'first frame was missed')
    else:
        require(h['resetRealFrameId'] <= h['previousSubmittedRealFrameId'] < m['sourceRealFrameId'],
                'eighth frame has no accepted reset predecessor')
    if record['action'] == 'camera.cut':
        require(h['cameraCutRevision'] == int(record['receipt']['cameraCutRevision']), 'wrong live cut')
    elif record['action'] == 'temporal.upscale':
        require(record['arming']['ready'] is True and record['arming']['complete'] is False and
                record['arming']['failed'] is False, 'quality trigger preceded RT capture admission')
        require(record['fromQuality'] != quality and
                int(record['after']['observedGeneration']) == int(record['receipt']['receiptGeneration']) and
                int(record['after']['requestedGeneration']) == int(record['receipt']['receiptGeneration']),
                'quality change was superseded or not applied')
    elif record['action'] == 'camera.lens.probe':
        require(record['arming']['ready'] is True and record['arming']['complete'] is False and
                record['arming']['failed'] is False, 'lens trigger preceded RT capture admission')
        before_lens, lens = record['receipt']['before'], record['receipt']['lens']
        require(any(before_lens[k] != lens[k] for k in ('fov', 'near', 'far')), 'lens did not change')
        require(int(before_lens['cameraCutRevision']) == int(lens['cameraCutRevision']) ==
                h['cameraCutRevision'] == int(record['before']['lastHistoryReset']['cameraCutRevision']),
                'lens probe synthesized a camera cut')
        for key in ('requestedGeneration', 'observedGeneration', 'historyResetGeneration', 'renderExtent', 'displayExtent'):
            require(record['before'][key] == record['after'][key], 'lens reset mixed another control or resize')
        args = record['arguments']
        require(len(args) == 4 and all(abs(float(args[i+1])-lens[k]) < 1e-6
                for i,k in enumerate(('fov','near','far'))), 'lens receipt differs from request')
        check_lens_projection(m, lens)
    elif record['action'] == 'window.resize':
        receipt = record['receipt']
        require(receipt['clamped'] is False, 'resize was clamped')
        require(m['displayWidth'] != record['before']['displayExtent']['width'] or
                m['displayHeight'] != record['before']['displayExtent']['height'], 'viewport did not resize')
    else:
        require(False, 'unsupported recovery trigger')
    attachments = {a['name']: a for a in m['attachments']}
    stages = {a['name']: a for a in m['diagnosticStages']}
    stage = stages['ssgi-history']
    require(stage['width'] == max(1, m['renderWidth'] // 2) and
            stage['height'] == max(1, m['renderHeight'] // 2), 'SSGI extent mismatch')
    images = {}
    hashes = {'manifest': hashlib.sha256(manifest_path.read_bytes()).hexdigest()}
    for name, a in [('ssgi', stage), ('display', attachments['display']),
                    ('depth', attachments['depth']), ('baseColor', attachments['baseColor']),
                    ('reconstructed', attachments['preToneHdr'])]:
        raw = (path / a['file']).read_bytes()
        channels = 4 if name == 'ssgi' else a['channels']
        require(a['nonfinite'] == 0 and len(raw) == a['width'] * a['height'] * channels * 4,
                'missing/truncated/nonfinite attachment')
        values = array.array('f')
        values.frombytes(raw)
        if sys.byteorder != 'little':
            values.byteswap()
        require(all(math.isfinite(x) for x in values), 'nonfinite GPU pixels')
        images[name] = (a['width'], a['height'], channels, values)
        hashes[name] = hashlib.sha256(raw).hexdigest()
    require(images['depth'][:2] == (m['renderWidth'], m['renderHeight']) and
            images['baseColor'][:2] == (m['renderWidth'], m['renderHeight']), 'input readback is not render-sized')
    require(images['display'][:2] == (m['displayWidth'], m['displayHeight']) and
            images['reconstructed'][:2] == (m['displayWidth'], m['displayHeight']), 'output readback is not display-sized')
    for output_name in ('reconstructed', 'display'):
        rgb = [x for i, x in enumerate(images[output_name][3]) if i % 4 != 3]
        require(max(rgb) - min(rgb) > 0.001, 'constant/empty ' + output_name + ' output')
    alpha = images['ssgi'][3][3::4]
    surface = [x for x in alpha if x > 0]
    require(surface and (all(x == 1 for x in surface) if ordinal == 1 else sum(surface)/len(surface) >= 4),
            'SSGI did not restart/accumulate on real surface pixels')
    require(max(images['ssgi'][3][c] for c in range(len(alpha)*4) if c % 4 != 3) > 0.000001,
            'SSGI radiance is empty')
    require(any(x < 0.999 for x in images['depth'][3]), 'geometry never reached the GPU')
    return m, images, hashes, len(surface), sum(surface)/len(surface)


def check_lens_projection(manifest, lens):
    fov, near, far = (lens[k] for k in ('fov', 'near', 'far'))
    require(all(math.isfinite(x) for x in (fov, near, far)) and
            1 < fov < 179 and 0 < near < far, 'invalid perspective lens')
    p = manifest['camera']['projection']
    y = 1 / math.tan(math.radians(fov) / 2)
    aspect = manifest['displayWidth'] / manifest['displayHeight']
    require(abs(p[5]-y) < 1e-6 and abs(p[0]-y/aspect) < 1e-6 and
            abs(p[10]-far/(far-near)) < 1e-6 and abs(p[14]+near*far/(far-near)) < 1e-6 and
            p[11] == 1 and p[15] == 0, 'submitted projection does not match the requested perspective lens')


def compare(a, b, name, tolerance):
    require(a[name][:3] == b[name][:3], 'comparison extent/format differs')
    error = max(abs(x-y) for x, y in zip(a[name][3], b[name][3]))
    require(error <= tolerance, f'{name} recovery pixel error {error} exceeds {tolerance}')
    return error


def main(root, provider='none', quality='quality'):
    records = json.loads((root / 'records.json').read_text(encoding='utf-8-sig'))
    require(len(records) == 13 and len({r['name'] for r in records}) == 13, 'missing live captures')
    captures = {r['name']: read_capture(root, r, provider, quality) for r in records}
    output = []
    for r in records:
        name = r['name']
        if name == 'old-pose' or 'reference' in name:
            continue
        reference = f"cut-reference-{r['ordinal']}" if r['action'] == 'camera.cut' else name + '-reference'
        m, images, hashes, surfaces, accumulation = captures[name]
        ref_m, ref_images, ref_hashes, _, _ = captures[reference]
        require(m['viewId'] == ref_m['viewId'] and m['sceneEpoch'] == ref_m['sceneEpoch'], 'reference view changed')
        output.append(dict(name=name, ssgiMaxError=compare(images, ref_images, 'ssgi', 1e-6),
                           displayMaxError=compare(images, ref_images, 'display', 1/255 + 1e-7),
                           reconstructedMaxError=compare(images, ref_images, 'reconstructed', 1e-6),
                           surfaces=surfaces, accumulation=accumulation,
                           hashes=hashes, referenceHashes=ref_hashes))
    old = captures['old-pose'][1]
    moved = captures['cut-1'][1]
    motion_difference = max(abs(x-y) for x, y in zip(old['depth'][3], moved['depth'][3]))
    require(old['depth'][:3] == moved['depth'][:3] and motion_difference > 0.001,
            'live camera motion did not change geometry pixels')
    output_motion = {}
    for name in ('reconstructed', 'display'):
        require(old[name][:3] == moved[name][:3], 'moved output extent changed')
        difference = max(abs(x-y) for i, (x,y) in enumerate(zip(old[name][3], moved[name][3])) if i % 4 != 3)
        require(difference > 0.001, 'live camera motion never reached ' + name + ' pixels')
        output_motion[name] = difference
    return dict(passed=True, cameraRgbMaxDifference=output_motion, provider=provider, quality=quality,
                scope='live reset recovery of SSGI, reconstructed HDR and final GPU display; no physical scanout or general image quality',
                cameraDepthMaxDifference=motion_difference, captures=13, comparisons=output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--provider', choices=['none', 'fsr', 'xess'], default='none')
    parser.add_argument('--quality', default='quality')
    args = parser.parse_args()
    root = args.directory
    try:
        result = main(root, args.provider, args.quality)
    except (ValueError, KeyError, OSError) as error:
        result = dict(passed=False, error=str(error))
    (root / 'pixels.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))
    sys.exit(0 if result['passed'] else 1)
