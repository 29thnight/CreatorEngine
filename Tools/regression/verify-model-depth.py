"""Compare captured depth with an independent CPU projection of source GLB triangles.

Uses captured world/pose/camera inputs, not captured depth, to choose probes.
Alpha geometry and triangle boundaries are reported separately, not certified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

import numpy as np


def meshlet_positions(positions):
    """Independent QMC1 v2 default position-grid model, before skinning.

    The contract uses a mesh-wide 16-bit grid, separate float32 multiply/add,
    and retains raw channels if any decoded value violates the 1e-4 budget or
    source bounds. See MeshletCompression.h and MeshletGeometryAccess.slang.
    """
    result = positions.astype(np.float32).copy()
    for axis in range(3):
        values = result[:,axis].copy()
        lo, hi = values.min(), values.max()
        step = np.float32((float(hi)-float(lo))/65535)
        if not np.isfinite(step) or step < np.finfo(np.float32).tiny:
            continue
        # Codes are nonnegative; floor(x+.5) implements C++ round ties away.
        codes = np.clip(np.floor((values.astype(np.float64)-float(lo))/float(step)+.5),0,65535)
        decoded = lo + codes.astype(np.float32)*step
        if (np.isfinite(decoded).all() and (decoded>=lo).all() and
                (decoded<=hi).all() and np.abs(decoded-values).max()<=np.float32(.0001)):
            result[:,axis] = decoded
    return result.astype(np.float64)


def verify(model, capture, output):
    raw = model.read_bytes()
    assert raw[:4] == b'glTF'
    size = struct.unpack_from('<I', raw, 12)[0]
    gltf = json.loads(raw[20:20 + size])
    binary = raw[28 + size:]

    def accessor(index):
        a = gltf['accessors'][index]
        assert 'sparse' not in a
        v = gltf['bufferViews'][a['bufferView']]
        dtype = np.dtype({5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']])
        components = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        result = np.ndarray((a['count'], components), dtype=dtype, buffer=binary,
                            offset=v.get('byteOffset',0)+a.get('byteOffset',0),
                            strides=(v.get('byteStride',components*dtype.itemsize),dtype.itemsize)).copy()
        if a.get('normalized'):
            result = result.astype(np.float64)/np.iinfo(dtype).max
        return result

    # Imported nodes are breadth first; skeleton includes every joint ancestor.
    nodes = gltf['nodes']
    parents = {child:i for i,n in enumerate(nodes) for child in n.get('children',[])}
    order = list(gltf['scenes'][gltf.get('scene',0)]['nodes'])
    for n in order:
        order.extend(nodes[n].get('children',[]))
    members = set()
    joints = gltf.get('skins',[{'joints':[]}])[0]['joints']
    for joint in joints:
        while joint not in members:
            members.add(joint)
            if joint not in parents:
                break
            joint = parents[joint]
    bone_order = [n for n in order if n in members]
    remap = np.array([bone_order.index(n) for n in joints])
    sidecar = model.with_suffix(model.suffix+'.meta').read_text(encoding='utf-8-sig')
    mesh_ids = {}
    for section in sidecar.split('  - kind: mesh')[1:]:
        section = section.split('  - kind:')[0]
        mesh_ids[re.search(r'assetId: (\S+)',section)[1]] = int(re.search(r'binding: mesh/(\d+)',section)[1])
    primitives = [p for mesh in gltf['meshes'] for p in mesh['primitives']]
    manifest = json.loads((capture/'manifest.json').read_text(encoding='utf-8-sig'))
    assert manifest['source'] == 'product-live' and manifest['validationCount'] == 0
    assert len(manifest['draws']) == len(primitives) > 0
    width, height = manifest['width'], manifest['height']
    vp = np.array(manifest['camera']['view']).reshape(4,4) @ np.array(manifest['camera']['projection']).reshape(4,4)
    expected = np.ones((height,width),dtype=np.float64)
    boundary_depth = np.full_like(expected,np.inf)
    edge_margin = 1.0/16.0
    margin = np.zeros_like(expected)
    owner = np.full((height,width),-1,dtype=np.int32)
    alpha = []
    material_policies = []
    triangle_count = 0
    for draw_index, draw in enumerate(manifest['draws']):
        primitive = primitives[mesh_ids[draw['meshId']]]
        material = gltf['materials'][primitive['material']]
        flags = draw['coverageFlags']
        assert flags & 1 and not flags & ~15 and not flags & 4, 'Unsupported or blended coverage policy'
        alpha.append(bool(flags & (2 | 4)))
        material_policies.append(dict(sourceDoubleSided=bool(material.get('doubleSided',False)),
                                      capturedDoubleSided=bool(flags & 8),
                                      sourceAlphaMode=material.get('alphaMode','OPAQUE'),
                                      capturedMasked=bool(flags & 2)))
        attrs = primitive['attributes']
        pos = accessor(attrs['POSITION']).astype(np.float64)
        pos[:,2] *= -1  # importer right-handed -> left-handed convention
        if draw.get('route') == 'lattice':
            routes = manifest['geometryRoutes']
            assert routes['source'] == 'joined-command-recording-after-native-submission'
            assert len(routes['selected']) == len(manifest['draws'])
            assert all(r['meshShader'] for r in routes['selected']), 'Mixed geometry routes need a separate oracle'
            decoded = meshlet_positions(pos)
            material_policies[-1].update(positionCodec='QMC1-v2-default-mesh-wide-16-bit',
                                        maxSourcePositionError=float(np.abs(decoded-pos).max()))
            pos = decoded
        pos = np.column_stack((pos,np.ones(len(pos))))
        if draw['pose']:
            packed = np.frombuffer(bytes(draw['pose']),dtype='<f4').reshape(-1,3,4).astype(np.float64)
            assert len(packed) == len(bone_order)
            ids = remap[accessor(attrs['JOINTS_0']).astype(int)]
            weights = accessor(attrs['WEIGHTS_0']).astype(np.float64)
            weights /= weights.sum(axis=1,keepdims=True)
            transformed = np.einsum('vkij,vj->vki',packed[ids],pos)
            pos[:,:3] = (transformed*weights[:,:,None]).sum(axis=1)
        clip = pos @ np.array(draw['world']).reshape(4,4) @ vp
        ndc = clip[:,:3]/clip[:,3,None]
        screen = np.column_stack(((ndc[:,0]+1)*width/2,(1-ndc[:,1])*height/2,ndc[:,2]))
        for indices in accessor(primitive['indices']).reshape(-1,3):
            triangle_count += 1
            t = screen[indices]
            if np.any(clip[indices,3] <= 0) or np.any(t[:,2]<0) or np.any(t[:,2]>1):
                raise ValueError('Near/far clipping requires a separate oracle')
            lo = np.maximum(np.floor(t[:,:2].min(axis=0)).astype(int),0)
            hi = np.minimum(np.ceil(t[:,:2].max(axis=0)).astype(int),[width-1,height-1])
            if np.any(lo>hi):
                continue
            x,y = np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
            a,b,c = t
            den = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-12:
                continue
            # Import reverses source index winding; the product rejects negative
            # screen area after that reversal for single-sided materials.
            if den > 0 and not (flags & 8):
                continue
            u = ((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/den
            v = ((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/den
            w = 1-u-v
            depth = u*a[2]+v*b[2]+w*c[2]
            region = np.s_[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
            inside = np.minimum(np.minimum(u,v),w)
            nearer = (inside>=0)&(depth<expected[region])
            expected[region][nearer] = depth[nearer]
            # Barycentric distance alone accepts tiny triangles near silhouettes.
            # Use pixel distance from all edges, independent of observed depth.
            edge_distance = np.minimum(np.minimum(u*abs(den)/np.linalg.norm(b[:2]-c[:2]),
                                                 v*abs(den)/np.linalg.norm(c[:2]-a[:2])),
                                       w*abs(den)/np.linalg.norm(a[:2]-b[:2]))
            # A foreground edge can cover a pixel after subpixel rounding even
            # when the unrounded ray hits a different, interior triangle behind it.
            boundary = np.abs(edge_distance)<edge_margin
            boundary_depth[region][boundary] = np.minimum(boundary_depth[region][boundary],depth[boundary])
            margin[region][nearer] = edge_distance[nearer]
            owner[region][nearer] = draw_index
    actual = np.fromfile(capture/'depth.f32',dtype='<f4').reshape(height,width)
    assert np.isfinite(actual).all() and np.all((actual>=0)&(actual<=1))
    # D3D raster coordinates have 8 fractional bits. Sixteen subpixel steps
    # exclude coverage ties without discarding almost every dense-model triangle.
    probes = (owner>=0)&(margin>=edge_margin)
    for i,is_alpha in enumerate(alpha):
        if is_alpha:
            probes &= owner!=i
    error = np.abs(actual.astype(np.float64)-expected)
    tolerance = 2e-6  # fixed before reading GPU values; includes float/geometry quantization
    boundary_excluded = int(np.sum(probes&(boundary_depth<=expected+tolerance)))
    probes &= boundary_depth>expected+tolerance
    count = int(probes.sum())
    report = dict(passed=count>=20 and not np.any(error[probes]>tolerance),
                  sourceSha256=hashlib.sha256(raw).hexdigest(),capture=str(capture.resolve()),
                  width=width,height=height,triangles=triangle_count,interiorOpaqueProbes=count,
                  tolerance=tolerance,minimumEdgeDistancePixels=edge_margin,
                  foregroundBoundaryExcludedPixels=boundary_excluded,
                  maxAbsoluteError=float(error[probes].max()) if count else None,
                  failingPixels=int(np.sum(error[probes]>tolerance)),
                  failures=[dict(x=int(x),y=int(y),expected=float(expected[y,x]),actual=float(actual[y,x]),
                                 mesh=manifest['draws'][owner[y,x]]['meshId'])
                            for y,x in np.argwhere(probes&(error>tolerance))[:50]],
                  exactFloatMatches=int(np.sum(actual[probes]==expected[probes].astype(np.float32))),
                  geometryCoveredPixels=int(np.sum(owner>=0)),gpuCoveredPixels=int(np.sum(actual<1)),
                  scope='source GLB geometry with default meshlet position codec + captured world/skin/camera/coverage; opaque triangle interiors only',
                  exclusions='alpha coverage, triangle boundaries, clipping, LOD simplification not certified',
                  perDraw=[dict(mesh=d['meshId'],alpha=alpha[i],probes=int(np.sum(probes&(owner==i))),
                                materialPolicy=material_policies[i]) for i,d in enumerate(manifest['draws'])])
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))
    return report['passed']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',type=Path,required=True)
    parser.add_argument('--capture',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    raise SystemExit(0 if verify(args.model,args.capture,args.output) else 1)
