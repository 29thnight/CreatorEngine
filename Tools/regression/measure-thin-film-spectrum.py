"""Independent 1nm CIE integration of the infinite Airy response.

Separates Fourier table/interpolation error from order-3 truncation. This is an
optical oracle, not a whole BSDF/image acceptance threshold.
"""
import cmath
import csv
import hashlib
import json
import math
import runpy
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
generator = runpy.run_path(str(ROOT / 'Tools/blender/generate-thin-film-sensitivity.py'))
MATRIX, FIXTURE, generated = (generator[name] for name in ('MATRIX','FIXTURE','generated'))


def clamp(x):
    return max(0., min(1., x))


def measure(output):
    generated()  # Validate pinned Fourier source before using it.
    cie = (FIXTURE / 'cie-1931.csv').read_bytes()
    if hashlib.sha256(cie).hexdigest() != 'cb709c73278d6d9d269ce2df87d4ea0eff4a103d260ba6fbf1ca1cb28d23ae79':
        raise ValueError('CIE 1931 identity differs')
    rows = [list(map(float, r)) for r in list(csv.reader(cie.decode().splitlines()))[1:]]
    if len(rows) != 473 or any(r[0] != 359+i for i, r in enumerate(rows)):
        raise ValueError('Expected 1nm CMFs from 359 to 831nm')
    weights = [[sum(a*b for a, b in zip(m, r[1:])) for m in MATRIX] for r in rows]
    totals = [sum(w[c] for w in weights) for c in range(3)]
    weights = [[w[c]/totals[c] for c in range(3)] for w in weights]
    table = [list(map(float, r)) for r in list(csv.reader((FIXTURE/'cie-fourier.csv').open()))[1:]]
    dc = [sum(a*b for a, b in zip(m, table[0][:3])) for m in MATRIX]
    sensitivities = [[complex(sum(a*b for a, b in zip(m, r[:3])), sum(a*b for a, b in zip(m, r[3:]))) / dc[c]
                      for c, m in enumerate(MATRIX)] for r in table]
    worst = {name: {'absolute': 0.} for name in ('table_vs_dense_order3', 'order3_vs_infinite')}
    count = 0
    for ior in (1., 1.1, 1.33, 1.5, 2., 3.):
        for thickness in (.1, 1., 100., 200., 400., 800., 1000.):
            for cosine in (.01, .05, .1, .25, .5, .75, 1.):
                sin2 = 1-cosine*cosine
                cf = math.sqrt(1-sin2/(ior*ior))
                opd = 2*ior*thickness*cf
                phases = [cmath.exp(2j*math.pi*opd/r[0]) for r in rows]
                dense = [[sum(w[c]*p**m for w, p in zip(weights, phases)) for c in range(3)] for m in (1, 2, 3)]
                lut = []
                for order in (1, 2, 3):
                    x = clamp(2*math.pi*opd*order/60000)*511
                    a = int(x); b = min(a+1, 511); t = x-a
                    lut.append([(sensitivities[a][c]*(1-t)+sensitivities[b][c]*t).conjugate() for c in range(3)])
                for substrate, f82 in ((complex(1.5,0), -1.), (complex(2.5,0), -1.), (complex(.3,3), .7)):
                    q = cmath.sqrt(substrate*substrate-sin2)
                    top = ((cosine-ior*cf)/(cosine+ior*cf), (ior*cosine-cf)/(ior*cosine+cf))
                    bottom = ((ior*cf-q)/(ior*cf+q), (substrate*substrate*cf-ior*q)/(substrate*substrate*cf+ior*q))
                    values = [[0.,0.,0.] for _ in range(3)]
                    for r12, r23 in zip(top,bottom):
                        r1, r2 = r12*r12, abs(r23)**2
                        if f82 >= 0:
                            f0 = abs((substrate-ior)/(substrate+ior))**2
                            b = (f0+(1-f0)*(6/7)**5-f82)*7/(6/7)**6
                            r2 = clamp(f0+(1-f0)*(1-cf)**5-b*cf*(1-cf)**6)
                        phase = (r23/abs(r23) if abs(r23) else 1)*(-1 if r12>=0 else 1)
                        transmission = 1-r1
                        multiple = transmission*transmission*r2/max(1-r1*r2,1e-20)
                        radius = math.sqrt(r1*r2)
                        exact = []
                        for p in phases:
                            z = radius*phase*p
                            exact.append(r1+multiple+2*(multiple-transmission)*(z/(1-z)).real)
                        for c in range(3):
                            for index, response in enumerate((lut, dense)):
                                values[index][c] += .5*(r1+multiple+sum(2*(multiple-transmission)*radius**m*(phase**m*response[m-1][c]).real for m in (1,2,3)))
                            values[2][c] += .5*sum(w[c]*r for w,r in zip(weights,exact))
                    for c in range(3):
                        v = [clamp(x[c]) for x in values]
                        if any(not math.isfinite(x) for x in v): raise ValueError('Non-finite optical response')
                        for name, a, b in (('table_vs_dense_order3',v[0],v[1]), ('order3_vs_infinite',v[1],v[2])):
                            error = abs(a-b)
                            if error > worst[name]['absolute']:
                                worst[name] = dict(absolute=error, film_ior=ior, thickness_nm=thickness, cosine=cosine,
                                    substrate_n=substrate.real, substrate_k=substrate.imag, f82=f82, channel=c, first=a, second=b)
                        count += 1
    report = dict(samples=count, wavelength_spacing_nm=1, wavelength_count=473,
                  color_space='linear Rec.709', reference='infinite Airy series, direct wavelength quadrature',
                  worst=worst, acceptance='measurement; grazing-angle/order-truncation limits remain explicit')
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    measure(Path(sys.argv[1]))
