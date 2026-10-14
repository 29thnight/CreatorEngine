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
# Current product contract: two commutative raster producers, one final resolve.
# Numeric access values are RGAccessMode (Read=1, Write=2, AtomicMin64=4).
hybrid_wiring = False
if not args.empty:
    def pass_named(name):
        rows = [row for row in graph['passes'] if row['name'] == name and not row['culled']]
        assert len(rows) == 1, f'Expected one live hybrid pass: {name}'
        return rows[0]
    init = pass_named('METEOR.Scene.Visibility.Initialize')
    classify = pass_named('METEOR.Scene.Visibility.Classify')
    hw = pass_named('METEOR.Scene.Visibility.HW')
    sw = pass_named('METEOR.Scene.Visibility.SW')
    merge = pass_named('METEOR.Scene.Visibility.Resolve')
    tiles = pass_named('METEOR.Scene.Material.CountTiles')
    resources = {row['name']: row['id'] for row in graph['resources']}
    keys = resources['METEOR.Scene.Visibility.Keys']
    visibility = resources['METEOR.Scene.Visibility']
    depth = resources['GBuffer.Depth']
    assert 'METEOR.Scene.Visibility.HW' not in resources, 'Retired HW-only target remains'
    for writer in (hw, sw):
        assert init['compiledIndex'] < classify['compiledIndex'] < writer['compiledIndex'] < merge['compiledIndex']
        reductions = [u for u in writer['usages'] if u['resource'] == keys and u['access'] == 4]
        assert len(reductions) == 1, 'Both raster paths must contribute to the shared atomic version'
        assert not any(u['resource'] in (visibility, depth) for u in writer['usages']), 'Raster writer has obsolete attachment dependency'
    assert not any({edge['producer'], edge['consumer']} == {hw['authoredIndex'], sw['authoredIndex']}
                   for edge in graph['versionEdges']), 'False ordering between commutative raster writers'
    assert any(u['resource'] == keys and u['access'] == 1 for u in merge['usages'])
    for target in (visibility, depth):
        assert any(u['resource'] == target and u['access'] == 2 for u in merge['usages']), 'Resolve owns final outputs'
    assert merge['compiledIndex'] < tiles['compiledIndex']
    assert any(u['resource'] == visibility and u['access'] == 1 for u in tiles['usages'])
    hybrid_wiring = True
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
verdict = dict(passed=True, hybridWiringValidated=hybrid_wiring, frameId=manifest['frameId'], coveredPixels=len(covered),
               litGeometryPixels=lit, recordedMeshletDispatches=len(selected),
               validationProblems=manifest['validationCount'], performanceValidated=False, emptyScene=args.empty, meteorHdrClear=True, deferredPasses=0, meteorLitGeometryPixels=meteor_lit)
(capture.parent / ('empty-capture-oracle.json' if args.empty else 'capture-oracle.json')).write_text(json.dumps(verdict, indent=2), encoding='utf-8')
print(json.dumps(verdict))
