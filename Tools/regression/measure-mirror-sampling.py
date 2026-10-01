"""Isolate cube filtering and raster interpolation on the frozen MAT-9 mirror.

NumPy diagnostic only. Uses the original shared triangle soup, camera and
source pixels; it cannot replace native captures or change acceptance masks.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import numpy as np

SPEC = importlib.util.spec_from_file_location('integrals', Path(__file__).with_name('measure-environment-integrals.py'))
math = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(math)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'reference', 'before', 'after', 'report'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.reference/'manifest.json').read_text(encoding='utf-8'))
    if manifest['camera']['eye'] != [0, 0, 3] or manifest['resolution'] != [64, 64]:
        raise ValueError('Requires the pinned camera/geometry diagnostic')
    raw = args.source.read_bytes()
    width, height = struct.unpack_from('<II', raw)
    source = np.frombuffer(raw, dtype='<f4', offset=8).reshape(height, width, 4)
    geometry = (args.reference/'sphere.bin').read_bytes()
    if hashlib.sha256(geometry).hexdigest() != manifest['geometry_sha256']:
        raise ValueError('Geometry identity differs')
    image = lambda root, name: np.fromfile(root/(name+'.f32'), dtype='<f4').reshape(64, 64, 4)
    covered = (image(args.reference, 'forest-control_emission')[..., 3] > .999)
    for root in (args.before, args.after):
        covered &= image(root, 'forest-control_emission')[..., 0] > 1
    mask = np.zeros_like(covered)
    mask[2:-2, 2:-2] = True
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            mask &= np.roll(covered, (dy, dx), (0, 1))
    y, x = np.where(mask)
    ray = math.unit(np.stack(((x+.5-32)/32*np.tan(manifest['camera']['vertical_fov']/2),
                             (y+.5-32)/32*np.tan(manifest['camera']['vertical_fov']/2),
                             -np.ones(len(x))), axis=-1))
    vertices = np.frombuffer(geometry, dtype='<f4', offset=4).reshape(-1, 3, 12).astype(np.float64)
    a = vertices[:, 0, :3]
    e1, e2 = vertices[:, 1, :3]-a, vertices[:, 2, :3]-a
    p = np.cross(ray[:, None, :], e2[None, :, :])
    determinant = np.sum(e1[None, :, :]*p, axis=-1)
    inverse = np.divide(1, determinant, out=np.zeros_like(determinant), where=abs(determinant)>1e-10)
    offset = np.array(manifest['camera']['eye'])-a
    u = np.sum(offset[None, :, :]*p, axis=-1)*inverse
    q = np.cross(offset, e1)
    v = np.sum(ray[:, None, :]*q[None, :, :], axis=-1)*inverse
    distance = np.sum(e2*q, axis=-1)[None, :]*inverse
    distance[(u<0) | (v<0) | (u+v>1) | (distance<0) | (abs(determinant)<1e-10)] = np.inf
    triangle = distance.argmin(axis=1)
    row = np.arange(len(x))
    if not np.isfinite(distance[row, triangle]).all():
        raise ValueError('Interior pixel ray missed the shared geometry')
    barycentric = np.stack((1-u[row, triangle]-v[row, triangle], u[row, triangle], v[row, triangle]), axis=-1)
    normal = math.unit(np.sum(barycentric[:, :, None]*vertices[triangle, :, 3:6], axis=1))
    view = -ray
    sample = lambda n, v: math.sample_source(source, math.unit(2*np.sum(n*v, axis=1)[:, None]*n-v))*.35
    reference = image(args.reference, 'forest-control_mirror')[mask, :3]
    result = dict(schema='creator.material.mirror-sampling-diagnostic.v1', interior_pixels=int(mask.sum()),
                  reference=str(args.reference.resolve()), before=str(args.before.resolve()), after=str(args.after.resolve()),
                  source_sha256=hashlib.sha256(raw).hexdigest(), geometry_sha256=manifest['geometry_sha256'],
                  pixel_center_source_vs_blender=math.metrics(reference, sample(normal, view)))
    for name, root in (('before', args.before), ('after', args.after)):
        native_normal = math.unit(image(root, 'debug-normal')[mask, :3]*2-1)
        native_view = math.unit(image(root, 'debug-view')[mask, :3]*2-1)
        result[name] = dict(native_mirror_vs_blender=math.metrics(reference, image(root, 'forest-control_mirror')[mask, :3]),
                            source_at_native_point_vs_blender=math.metrics(reference, sample(native_normal, native_view)),
                            normal_vs_pixel_center=math.metrics(normal, native_normal),
                            view_vs_pixel_center=math.metrics(view, native_view))
    args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('MIRROR_SAMPLING_MEASURED pixels=%d before=%.6f%% after=%.6f%% analytic=%.6f%%' % (
        mask.sum(), 100*result['before']['native_mirror_vs_blender']['relative_rms'],
        100*result['after']['native_mirror_vs_blender']['relative_rms'],
        100*result['pixel_center_source_vs_blender']['relative_rms']))


if __name__ == '__main__':
    main()
