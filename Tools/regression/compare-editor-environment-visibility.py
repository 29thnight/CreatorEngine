"""Check controlled product HDR captures; mesh lighting must survive background toggles."""
import argparse
from array import array
import json
import math
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
parser.add_argument('--neutral', action='store_true', help='Also check the gray/default/custom environment policy')
args = parser.parse_args()
root = args.directory

def attachment(capture, name):
    folder = root / capture
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8-sig'))
    entry = next(a for a in manifest['attachments'] if a['name'] == name)
    data = array('f')
    data.frombytes((folder / entry['file']).read_bytes())
    if sys.byteorder != 'little':
        data.byteswap()
    shape = (entry['height'], entry['width'], entry['channels'])
    assert len(data) == math.prod(shape), (capture, name, 'invalid size')
    assert all(math.isfinite(value) for value in data), (capture, name, 'nonfinite')
    return data, shape

on, shape = attachment('scene-on', 'preToneHdr')
depth, depth_shape = attachment('scene-on', 'depth')
assert shape[:2] == depth_shape[:2] and depth_shape[2] == 1
# The Scene grid can write depth after GBuffer. Its pixels are not mesh coverage.
normal, normal_shape = attachment('scene-on', 'normal')
assert shape == normal_shape
mesh = [i for i in range(len(depth)) if any(normal[i * shape[2] + c] for c in range(3))]
background = [i for i in range(len(depth)) if not any(normal[i * shape[2] + c] for c in range(3))]
assert mesh and background, 'Need mesh and background coverage'
result = {'meshPixels': len(mesh), 'backgroundPixels': len(background), 'comparisons': {}}

def max_delta(a, b, pixels, channels):
    return max(abs(a[pixel * channels + channel] - b[pixel * channels + channel])
               for pixel in pixels for channel in range(3))

for name in ['scene-hidden', 'scene-off', 'scene-restored']:
    other, other_shape = attachment(name, 'preToneHdr')
    assert other_shape == shape, 'Capture extent changed'
    other_normal, other_normal_shape = attachment(name, 'normal')
    assert other_normal_shape == normal_shape and other_normal == normal, 'Mesh coverage changed'
    mesh_delta = max_delta(other, on, mesh, shape[2])
    bg_delta = max_delta(other, on, background, shape[2])
    assert mesh_delta == 0.0, (name, 'Background toggle changed mesh lighting', mesh_delta)
    if name == 'scene-restored':
        assert bg_delta == 0.0, 'Background restoration changed cached environment'
    else:
        assert bg_delta > 0.01, (name, 'Background did not change')
    result['comparisons'][name] = {'meshMaxAbs': mesh_delta, 'backgroundMaxAbs': bg_delta}
game_on, game_shape = attachment('game-on', 'preToneHdr')
game_hidden, game_hidden_shape = attachment('game-scene-hidden', 'preToneHdr')
assert game_shape == game_hidden_shape, 'Game capture extent changed'
game_delta = max(abs(a - b) for a, b in zip(game_on, game_hidden))
assert game_delta == 0.0, ('Scene-only visibility leaked into Game', game_delta)
result['gameMaxAbs'] = game_delta
if args.neutral:
    result['neutral'] = {}
    for name in ['scene-default', 'scene-hidden', 'scene-off', 'game-default', 'scene-custom-off', 'scene-grid']:
        hdr, hdr_shape = attachment(name, 'preToneHdr')
        normals, normals_shape = attachment(name, 'normal')
        display, display_shape = attachment(name, 'display')
        assert hdr_shape == normals_shape == display_shape
        height, width, channels = hdr_shape
        bg = {i for i in range(height * width) if not any(normals[i * channels + c] for c in range(3))}
        assert bg, (name, 'No background')
        gray_error = max(abs(hdr[p * channels + c] - 0.18) for p in bg for c in range(3))
        assert gray_error < 0.001, (name, 'Background is not neutral linear gray', gray_error)
        # This fixture uses FXAA spanMax=8. It can mix colorful mesh pixels into
        # nearby background; sample farther away to test background/grid color.
        radius = 8
        interior = [p for p in bg if radius <= p % width < width - radius and radius <= p // width < height - radius
                    and all(p + dy * width + dx in bg for dy in range(-radius, radius + 1)
                            for dx in range(-radius, radius + 1))]
        assert interior, (name, 'No background interior')
        spreads = [max(display[p * channels:p * channels + 3]) - min(display[p * channels:p * channels + 3])
                   for p in interior]
        neutral_fraction = sum(v <= 2.0 / 255 for v in spreads) / len(spreads)
        assert neutral_fraction == 1.0, (name, 'Background/grid is tinted', neutral_fraction)
        minimum = min(min(display[p * channels:p * channels + 3]) for p in interior)
        assert minimum > 0.02, (name, 'Background is black', minimum)
        result['neutral'][name] = {'backgroundPixels': len(bg), 'linearGrayMaxError': gray_error,
                                  'neutralDisplayFraction': neutral_fraction, 'displayMin': minimum}
    default, default_shape = attachment('scene-default', 'preToneHdr')
    off, off_shape = attachment('scene-off', 'preToneHdr')
    assert default_shape == off_shape and default == off, 'Default differs from explicit background off'
    custom, custom_shape = attachment('scene-custom', 'preToneHdr')
    custom_off, custom_off_shape = attachment('scene-custom-off', 'preToneHdr')
    custom_normal, _ = attachment('scene-custom', 'normal')
    custom_off_normal, _ = attachment('scene-custom-off', 'normal')
    assert custom_shape == custom_off_shape and custom_normal == custom_off_normal
    custom_mesh = [p for p in range(custom_shape[0] * custom_shape[1])
                   if any(custom_normal[p * custom_shape[2] + c] for c in range(3))]
    assert custom_mesh
    custom_delta = max_delta(custom, custom_off, custom_mesh, custom_shape[2])
    assert custom_delta == 0.0, ('Custom environment toggle changed mesh IBL', custom_delta)
    result['customMeshMaxAbs'] = custom_delta
    grid_depth, grid_depth_shape = attachment('scene-grid', 'depth')
    grid_display, grid_shape = attachment('scene-grid', 'display')
    grid_normal, _ = attachment('scene-grid', 'normal')
    grid_pixels = [p for p in range(len(grid_depth)) if grid_depth[p] < 1.0
                   and not any(grid_normal[p * grid_shape[2] + c] for c in range(3))]
    assert len(grid_pixels) > 50, 'Empty Scene must contain visible floor grid lines'
    grid_spread = max(max(grid_display[p * grid_shape[2]:p * grid_shape[2] + 3])
                      - min(grid_display[p * grid_shape[2]:p * grid_shape[2] + 3]) for p in grid_pixels)
    assert grid_spread <= 2.0 / 255, ('Floor grid has colored lines', grid_spread)
    result['grid'] = {'visiblePixels': len(grid_pixels), 'displayMaxChannelSpread': grid_spread}
(root / 'visibility-pixels.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print('ENVIRONMENT_VISIBILITY_PIXELS_OK', json.dumps(result))
