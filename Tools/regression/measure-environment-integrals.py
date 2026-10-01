"""Separate decoded source, cube resampling and diffuse quadrature errors.

Requires NumPy. Source files are top-down RGBA32F from decode-environment-exr.py.
This is a diagnostic, not a substitute for the frozen Blender image gates.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import numpy as np


def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def face_direction(face, uv):
    u, v = uv[..., 0], uv[..., 1]
    one = np.ones_like(u)
    return unit(np.stack(((one, -v, -u), (-one, -v, u), (u, one, v),
                          (u, -one, -v), (u, -v, one), (-u, -v, -one))[face], axis=-1))


def direction_uv(d):
    x, y, z = d.T
    ax, ay, az = np.abs(d).T
    ix = (ax >= ay) & (ax >= az)
    iy = ~ix & (ay >= az)
    face = np.where(ix, np.where(x >= 0, 0, 1),
                    np.where(iy, np.where(y >= 0, 2, 3), np.where(z >= 0, 4, 5)))
    a = np.maximum.reduce((ax, ay, az))
    u = np.where(ix, np.where(x >= 0, -z, z), np.where(iy, x, np.where(z >= 0, x, -x))) / a
    v = np.where(iy, np.where(y >= 0, z, -z), -y) / a
    return face, np.stack((u, v), axis=-1)


def sample_cube(cube, d):
    size = cube.shape[1]
    face, uv = direction_uv(d)
    xy = (uv * .5 + .5) * size - .5
    low = np.floor(xy).astype(int)
    fraction = xy-low
    result = np.zeros((len(d), 3))
    for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        cell = low + (dx, dy)
        f, x, y = face.copy(), cell[:, 0].copy(), cell[:, 1].copy()
        outside = (x < 0) | (x >= size) | (y < 0) | (y >= size)
        for old_face in range(6):
            mask = outside & (face == old_face)
            direction = face_direction(old_face, (cell[mask]+.5)*2/size-1)
            nf, nuv = direction_uv(direction)
            ncell = np.clip(((nuv*.5+.5)*size).astype(int), 0, size-1)
            f[mask], x[mask], y[mask] = nf, ncell[:, 0], ncell[:, 1]
        w = (fraction[:, 0] if dx else 1-fraction[:, 0]) * (fraction[:, 1] if dy else 1-fraction[:, 1])
        result += cube[f, y, x, :3] * w[:, None]
    return result


def sample_source(source, d):
    height, width = source.shape[:2]
    uv = np.stack((np.arctan2(d[:, 2], d[:, 0])/(2*math.pi)+.5,
                   -np.arcsin(np.clip(d[:, 1], -1, 1))/math.pi+.5), axis=-1)
    xy = uv * (width, height) - .5
    low = np.floor(xy).astype(int)
    fraction = xy-low
    result = np.zeros((len(d), 3))
    for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        w = (fraction[:, 0] if dx else 1-fraction[:, 0]) * (fraction[:, 1] if dy else 1-fraction[:, 1])
        result += source[np.clip(low[:, 1]+dy, 0, height-1), (low[:, 0]+dx) % width, :3] * w[:, None]
    return result


def read_cook(path):
    data = path.read_bytes()
    if data[:8] not in (b'CEIBL001', b'CEIBL002', b'CEIBL003', b'CEIBL004', b'CEIBL005') or hashlib.sha256(data[40:]).digest() != data[8:40]:
        raise ValueError('Invalid cooked environment identity/checksum')
    size, brdf = struct.unpack_from('<II', data, 40)
    offset = 120 if data[:8] in (b'CEIBL004', b'CEIBL005') else 112
    maps = []
    for index, (side, mips, faces) in enumerate(((size, min(size.bit_length(), 7), 6),
                              (min(size, 64), 1, 6), (size, 6, 6), (brdf, 1, 1))):
        dtype = '<f4' if data[:8] in (b'CEIBL003', b'CEIBL004', b'CEIBL005') and index in (0, 2) else '<f2'
        pixel_bytes = np.dtype(dtype).itemsize*4
        first = []
        for face in range(faces):
            for mip in range(mips):
                n = side >> mip
                pixels = np.frombuffer(data, dtype=dtype, count=n*n*4, offset=offset).reshape(n, n, 4)
                if mip == 0:
                    first.append(pixels.astype(np.float64))
                offset += n*n*pixel_bytes
        maps.append(np.stack(first))
    return maps, data


def metrics(reference, actual):
    error = np.abs(actual-reference)
    normalized = error/np.maximum(1, np.abs(reference))
    return dict(relative_rms=float(np.linalg.norm(error)/max(np.linalg.norm(reference), 1e-20)),
                p95_normalized=float(np.quantile(normalized, .95)), max_normalized=float(normalized.max()),
                reference_mean=reference.mean(axis=0).tolist(), actual_mean=actual.mean(axis=0).tolist())


def irradiance(normals, directions, radiance, area):
    weighted = radiance * area[:, None] / math.pi
    result = []
    for start in range(0, len(normals), 32):
        result.append(np.maximum(normals[start:start+32] @ directions.T, 0) @ weighted)
    return np.concatenate(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('decoded', type=Path)
    parser.add_argument('cook', type=Path)
    parser.add_argument('reference', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('report', type=Path)
    parser.add_argument('--mode', required=True, choices=('forest', 'autumn'))
    args = parser.parse_args()
    data = args.decoded.read_bytes()
    width, height = struct.unpack_from('<II', data)
    source = np.frombuffer(data, dtype='<f4', offset=8).reshape(height, width, 4).astype(np.float64)
    maps, cooked = read_cook(args.cook)
    cube, diffuse = maps[:2]
    side = cube.shape[1]
    image = lambda root, name: np.fromfile(root/(name+'.f32'), dtype='<f4').reshape(64, 64, 4)
    emission = args.mode+'-control_emission'
    covered = (image(args.reference, emission)[..., 3] > .999) & (image(args.native, emission)[..., 0] > 1)
    interior = np.zeros_like(covered)
    interior[2:-2, 2:-2] = True
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            interior &= np.roll(covered, (dy, dx), (0, 1))
    normals = unit(image(args.native, 'debug-normal')[interior, :3]*2-1)
    view = unit(image(args.native, 'debug-view')[interior, :3]*2-1)
    reflected = unit(2*np.sum(normals*view, axis=1)[:, None]*normals-view)
    source_mirror = sample_source(source, reflected)*.35
    cube_mirror = sample_cube(cube, reflected)*.35
    native_mirror = image(args.native, args.mode+'-control_mirror')[interior, :3]
    blender_mirror = image(args.reference, args.mode+'-control_mirror')[interior, :3]
    # Dense lat-long texel quadrature. Cosine varies slowly within source texels;
    # exact solid angles retain the source's tiny HDR emitters without clipping.
    phi = ((np.arange(width)+.5)/width-.5)*2*math.pi
    theta = (np.arange(height)+.5)/height*math.pi
    directions = np.stack(np.broadcast_arrays(np.sin(theta)[:, None]*np.cos(phi),
                           np.cos(theta)[:, None], np.sin(theta)[:, None]*np.sin(phi)), axis=-1).reshape(-1, 3)
    area = np.broadcast_to((np.cos(np.arange(height)/height*math.pi)-
                            np.cos((np.arange(height)+1)/height*math.pi))[:, None]*2*math.pi/width,
                           (height, width)).reshape(-1)
    source_diffuse = irradiance(normals, directions, source[..., :3].reshape(-1, 3), area)*.35
    axis = (np.arange(side)+.5)*2/side-1
    uv = np.stack(np.meshgrid(axis, axis), axis=-1).reshape(-1, 2)
    directions = np.concatenate([face_direction(f, uv) for f in range(6)])
    bounds = np.arange(side+1)*2/side-1
    x, y = np.meshgrid(bounds, bounds)
    omega = np.arctan2(x*y, np.sqrt(1+x*x+y*y))
    area = np.tile((omega[1:, 1:]-omega[:-1, 1:]-omega[1:, :-1]+omega[:-1, :-1]).reshape(-1), 6)
    cube_diffuse = irradiance(normals, directions, cube[..., :3].reshape(-1, 3), area)*.35
    baked_diffuse = sample_cube(diffuse, normals)*.35
    native_white = image(args.native, args.mode+'-control_white')[interior, :3]
    blender_white = image(args.reference, args.mode+'-control_white')[interior, :3]
    # Conversion at cube centers should be source bilinear samples rounded to half.
    cube_centers = sample_source(source, directions)
    result = dict(schema='creator.environment.integrals-diagnostic.v1', mode=args.mode,
                  decoded=str(args.decoded.resolve()), decoded_sha256=hashlib.sha256(data).hexdigest(),
                  source_sha256=cooked[48:80].hex(), cook=str(args.cook.resolve()),
                  cook_sha256=hashlib.sha256(cooked).hexdigest(), interior_pixels=int(interior.sum()),
                  source_dimensions=[width, height], cube_size=side,
                  quadrature='all source texel centers and cube texel centers, exact solid angles; E/pi',
                  cube_centers_vs_source=metrics(cube_centers, cube[..., :3].reshape(-1, 3)),
                  mirror_cube_resampling=metrics(source_mirror, cube_mirror),
                  mirror_native_vs_cpu_cube=metrics(cube_mirror, native_mirror),
                  mirror_blender_vs_cpu_source=metrics(source_mirror, blender_mirror),
                  diffuse_cube_vs_source=metrics(source_diffuse, cube_diffuse),
                  diffuse_bake_vs_dense_cube=metrics(cube_diffuse, baked_diffuse),
                  diffuse_native_vs_bake=metrics(baked_diffuse, native_white),
                  diffuse_blender_vs_dense_source=metrics(source_diffuse, blender_white))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    for name, value in result.items():
        if isinstance(value, dict) and 'relative_rms' in value:
            print(name, 'rms_percent=%.6f' % (100*value['relative_rms']))


if __name__ == '__main__':
    main()
