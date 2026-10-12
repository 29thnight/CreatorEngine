"""Check actual METEOR command recording and captured product pixels; no A/B route."""
import argparse
from array import array
import json
import math
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
parser.add_argument('--empty', action='store_true')
args = parser.parse_args()
capture = args.directory
manifest = json.loads((capture / 'manifest.json').read_text(encoding='utf-8-sig'))
routes = manifest['geometryRoutes']
assert manifest['source'] == 'product-live' and manifest['backend'] == 'dx12'
assert manifest['validationCount'] == 0 and manifest['finite']
graph = manifest['compiledGraph']
assert not any('Deferred' in row['name'] for row in graph['passes']), 'Removed Deferred pass recorded'
clears = [row for row in graph['passes'] if row['name'] == 'METEOR.Scene.HdrClear']
assert len(clears) == 1 and not clears[0]['culled'], 'METEOR HDR clear must execute once'
assert len(clears[0]['usages']) == 1 and clears[0]['usages'][0]['inferredWrite']
hdr = next(row for row in graph['resources'] if row['name'] == 'METEOR.Scene.HdrColor')
assert clears[0]['usages'][0]['resource'] == hdr['id']
assert bool(routes['emptyScene']) == args.empty
assert routes['pass'] == 'METEOR.Scene.GBuffer'
assert routes['source'] == 'joined-command-recording-after-native-submission'
assert routes['sourceFrameId'] == manifest['frameId']
selected = routes['selected']
assert (not selected if args.empty else bool(selected))
assert all(row['meshShader'] and row['indirect'] for row in selected)
assert routes['recordedMeshletDispatchCount'] == len(selected)
pixels = {}
for attachment in manifest['attachments']:
    data = array('f')
    data.frombytes((capture / attachment['file']).read_bytes())
    if sys.byteorder != 'little':
        data.byteswap()
    assert len(data) == manifest['width'] * manifest['height'] * attachment['channels']
    assert all(math.isfinite(value) for value in data)
    pixels[attachment['name']] = data
covered = [i for i, value in enumerate(pixels['depth']) if 0 <= value < 0.99999]
stage = next(row for row in manifest['diagnosticStages'] if row['name'].endswith('-METEOR.Scene.Color'))
meteor_hdr = array('f')
meteor_hdr.frombytes((capture / stage['file']).read_bytes())
if sys.byteorder != 'little':
    meteor_hdr.byteswap()
assert len(meteor_hdr) == manifest['width'] * manifest['height'] * 4
assert all(math.isfinite(value) for value in meteor_hdr)
if args.empty:
    assert not covered, 'Empty METEOR scene contains geometry depth'
    assert manifest['skyBoxEnabled'] == 0
    # SkyBox later paints its neutral background; inspect METEOR before that consumer.
    assert all(value == 0 for value in meteor_hdr), 'Empty METEOR HDR was not initialized to black'
else:
    assert covered, 'Captured METEOR geometry has no covered depth pixels'
lit = sum(any(pixels['preToneHdr'][i * 4 + c] > 1e-6 for c in range(3)) for i in covered)
assert args.empty or lit, 'Captured geometry has no finite lit HDR pixels'
meteor_lit = sum(any(meteor_hdr[i * 4 + c] > 1e-6 for c in range(3)) for i in covered)
assert args.empty or meteor_lit, 'METEOR produced no lit geometry before downstream composition'
verdict = dict(passed=True, frameId=manifest['frameId'], coveredPixels=len(covered),
               litGeometryPixels=lit, recordedMeshletDispatches=len(selected),
               validationProblems=manifest['validationCount'], performanceValidated=False, emptyScene=args.empty, meteorHdrClear=True, deferredPasses=0, meteorLitGeometryPixels=meteor_lit)
(capture.parent / ('empty-capture-oracle.json' if args.empty else 'capture-oracle.json')).write_text(json.dumps(verdict, indent=2), encoding='utf-8')
print(json.dumps(verdict))
