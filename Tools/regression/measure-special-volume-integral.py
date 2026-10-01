"""Independent single-scattering quadrature for the fixed MAT-9 homogeneous sphere.

Uses shared triangle intersections and Beer attenuation on camera/light rays.
Requires NumPy; no engine reference implementation or shader formulas are imported.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import numpy as np

SPEC = importlib.util.spec_from_file_location('images', Path(__file__).with_name('compare-material-blender-images.py'))
images = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(images)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.reference/'manifest.json').read_text(encoding='utf-8'))
    captured = json.loads((args.native/'reference-manifest.json').read_text(encoding='utf-8'))
    if manifest != captured or manifest.get('suite') not in ('special','special-volume'):
        raise ValueError('Requires matching fixed Special capture')
    images.validate_reference(args.reference,manifest)
    images.validate_reference(args.native,captured,False)
    expected_input = {'Volume Only':1,'Volume.Color':[.5,.5,.5,1], 'Volume.Density':.18,'Volume.Anisotropy':0}
    if next(case['inputs'] for case in manifest['cases'] if case['id']=='sun-special_volume_scattering') != expected_input:
        raise ValueError('Fixed isotropic homogeneous coefficients changed')
    geometry = np.frombuffer((args.reference/'sphere.bin').read_bytes()[4:],dtype='<f4').reshape(-1,12)
    triangles = geometry[:,:3].astype(float).reshape(-1,3,3)
    v0,ab,ac = triangles[:,0],triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]
    def read(path):
        return np.asarray(images.read(path)).reshape(4096,4)
    a,b = (read(root/'sun-control_emission.f32') for root in (args.reference,args.native))
    coverage = ((a[:,3]>.999)&(b[:,0]>1)).reshape(64,64)
    pixels = np.array([p for p in range(4096) if 2<=p%64<62 and 2<=p//64<62 and
                       coverage[p//64-2:p//64+3,p%64-2:p%64+3].all()])
    if len(pixels)<100:
        raise ValueError('Common fixed coverage failed')
    def intersections(origins,directions):
        p = np.cross(directions[:,None,:],ac[None,:,:])
        determinant = np.einsum('tj,ptj->pt',ab,p)
        safe = np.where(abs(determinant)>1e-12,determinant,np.inf)
        offset = origins[:,None,:]-v0[None,:,:]
        u = np.einsum('ptj,ptj->pt',offset,p)/safe
        q = np.cross(offset,ab[None,:,:])
        v = np.einsum('pj,ptj->pt',directions,q)/safe
        t = np.einsum('tj,ptj->pt',ac,q)/safe
        valid = (abs(determinant)>1e-12)&(u>=-1e-7)&(v>=-1e-7)&(u+v<=1.0000001)&(t>1e-8)
        return np.where(valid,t,np.nan)
    rays = np.column_stack((((pixels%64+.5)/64*2-1)*math.tan(math.pi/8),
                            ((pixels//64+.5)/64*2-1)*math.tan(math.pi/8),np.full(len(pixels),-1)))
    rays /= np.linalg.norm(rays,axis=1)[:,None]
    origins = np.tile([0.,0.,3.],(len(pixels),1))
    t = intersections(origins,rays)
    near,far = np.nanmin(t,axis=1),np.nanmax(t,axis=1)
    light = np.asarray(manifest['sun']['to_light'])
    def integrate(count):
        x,w = np.polynomial.legendre.leggauss(count)
        distance = near[:,None]+(far-near)[:,None]*(x[None,:]+1)/2
        points = origins[:,None,:]+distance[:,:,None]*rays[:,None,:]
        exits = intersections(points.reshape(-1,3),np.tile(light,(len(pixels)*count,1)))
        light_distance = np.nanmax(exits,axis=1).reshape(distance.shape)
        expected = ((far-near)/2*np.sum(w[None,:]*np.exp(-.18*((distance-near[:,None])+light_distance)),axis=1)
                    *.09/(4*math.pi))[:,None]*np.ones((1,3))
        if not np.isfinite(expected).all():
            raise ValueError('Closed boundary intersections failed')
        return expected
    low,high = integrate(96),integrate(192)
    report = dict(schema='creator.material.single-volume-quadrature.v1',
                  purpose='independent fixed directional-light single scattering; no multiple-scattering claim',
                  reference=str(args.reference),native_capture=str(args.native),pixels=len(pixels),
                  geometry_sha256=manifest['geometry_sha256'],quadrature_points=[96,192],
                  convergence_relative_rms=float(np.linalg.norm(low-high)/np.linalg.norm(high)))
    for name,root in (('native',args.native),('cycles',args.reference)):
        value = read(root/'sun-special_volume_scattering.f32')[pixels,:3]
        report[name] = dict(relative_rms=float(np.linalg.norm(value-high)/np.linalg.norm(high)),
                            mean_rgb=value.mean(axis=0).tolist())
        print(name,'single-scattering RMS %.6f%%'%(report[name]['relative_rms']*100))
    report['quadrature_mean_rgb'] = high.mean(axis=0).tolist()
    args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
