"""Validate persisted MIS distributions and optionally prove unchanged lighting maps."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def read(path):
    data = path.read_bytes()
    if data[:8] not in (b'CEIBL001', b'CEIBL002', b'CEIBL003', b'CEIBL004', b'CEIBL005') or hashlib.sha256(data[40:]).digest() != data[8:40]:
        raise ValueError('Cook signature/checksum differs')
    size, brdf = struct.unpack_from('<II', data, 40)
    if size < 32 or size > 1024 or size & (size-1):
        raise ValueError('Invalid cube size')
    mips = min(size.bit_length(), 7)
    radiance_bytes = 16 if data[:8] in (b'CEIBL003', b'CEIBL004', b'CEIBL005') else 8
    lighting_bytes = (6*sum((size >> i)**2 for i in range(mips)) +
                      6*sum((size >> i)**2 for i in range(6)))*radiance_bytes + (6*min(size,64)**2+brdf**2)*8
    return data, size, (120 if data[:8] in (b'CEIBL004', b'CEIBL005') else 112)+lighting_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cook', type=Path)
    parser.add_argument('report', type=Path)
    parser.add_argument('--lighting-baseline', type=Path)
    parser.add_argument('--decoded-source', type=Path)
    args = parser.parse_args()
    data, size, offset = read(args.cook)
    sample_count=5120 if data[:8]==b'CEIBL005' else 1024
    source_bytes = 0
    if data[:8] in (b'CEIBL004', b'CEIBL005'):
        width,height=struct.unpack_from('<II',data,112)
        if not (0 < width <= 16384 and 0 < height <= 16384):
            raise ValueError('Invalid persisted source dimensions')
        source_bytes=width*height*16
    if data[:8] not in (b'CEIBL002', b'CEIBL003', b'CEIBL004', b'CEIBL005') or len(data) != offset + (6*size*size+6*size+2*sample_count)*16 + source_bytes:
        raise ValueError('Requires persisted full-resolution importance maps')
    rows = memoryview(data)[offset:offset+6*size*size*16].cast('f')
    marginal_offset = offset+6*size*size*16
    marginal = memoryview(data)[marginal_offset:marginal_offset+6*size*16].cast('f')
    samples = memoryview(data)[marginal_offset+6*size*16:marginal_offset+6*size*16+2*sample_count*16].cast('f')
    last_marginal = 0
    for y in range(6*size):
        before = 0
        for x in range(size):
            cell = rows[(y*size+x)*4:(y*size+x)*4+4]
            if not all(math.isfinite(v) and v >= 0 for v in cell) or cell[3] < before:
                raise ValueError('Invalid/non-monotone row CDF')
            before = cell[3]
        if not math.isfinite(marginal[y*4]) or marginal[y*4] < last_marginal:
            raise ValueError('Invalid/non-monotone marginal CDF')
        last_marginal = marginal[y*4]
    total = marginal[-4]
    max_pdf_error = 0
    for i in range(sample_count):
        x, y, z, pdf = samples[i*4:i*4+4]
        if not all(math.isfinite(v) for v in (x,y,z,pdf)) or pdf <= 0 or abs(x*x+y*y+z*z-1) > 1e-5:
            raise ValueError('Invalid cached direction/PDF')
        ax, ay, az = abs(x), abs(y), abs(z)
        if ax >= ay and ax >= az:
            face, u, v = (0 if x >= 0 else 1), (-z if x >= 0 else z)/ax, -y/ax
        elif ay >= az:
            face, u, v = (2 if y >= 0 else 3), x/ay, (z if y >= 0 else -z)/ay
        else:
            face, u, v = (4 if z >= 0 else 5), (x if z >= 0 else -x)/az, -y/az
        cx = min(size-1, max(0, int((u*.5+.5)*size)))
        cy = min(size-1, max(0, int((v*.5+.5)*size)))
        cell = rows[((face*size+cy)*size+cx)*4:((face*size+cy)*size+cx)*4+4]
        cu, cv = (cx+.5)*2/size-1, (cy+.5)*2/size-1
        expected = ((.2126*cell[0]+.7152*cell[1]+.0722*cell[2])*(1+cu*cu+cv*cv)**-1.5 /
                    total * (.25*size*size) * (1+u*u+v*v)**1.5) if total > 0 else 1/(4*math.pi)
        error = abs(pdf-expected)/max(expected, 1e-30)
        max_pdf_error = max(max_pdf_error, error)
    if max_pdf_error > .0005:
        raise ValueError('Cached PDF disagrees with shader cube lookup PDF')
    source_equal = None
    if args.decoded_source:
        decoded=args.decoded_source.read_bytes()
        source_equal=bool(source_bytes) and decoded[:8]==data[112:120] and decoded[8:]==data[-source_bytes:]
        if not source_equal:
            raise ValueError('Persisted source differs from decoded original')
    lighting_equal = None
    if args.lighting_baseline:
        baseline, baseline_size, baseline_offset = read(args.lighting_baseline)
        lighting_equal = baseline_size == size and baseline[120 if baseline[:8] in (b'CEIBL004', b'CEIBL005') else 112:baseline_offset] == data[120 if data[:8] in (b'CEIBL004', b'CEIBL005') else 112:offset]
        if not lighting_equal:
            raise ValueError('The four lighting maps changed')
    result = dict(schema='creator.environment-importance-verification.v1', cook=str(args.cook.resolve()),
                  sha256=hashlib.sha256(data).hexdigest(), cube_size=size, samples=sample_count, sample_banks=([1024,4096] if sample_count==5120 else [1024]),
                  rows=6*size, cached_pdf_max_relative_error=max_pdf_error, importance_persisted=True, source_persisted=bool(source_bytes), source_bytes_equal=source_equal,
                  lighting_baseline=str(args.lighting_baseline.resolve()) if args.lighting_baseline else None,
                  lighting_bytes_equal=lighting_equal, bytes=len(data))
    args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('ENVIRONMENT_IMPORTANCE_OK samples=%d lighting_equal=%s pdf_error=%g' % (sample_count,lighting_equal,max_pdf_error))


if __name__ == '__main__':
    main()
