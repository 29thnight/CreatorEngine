"""Generate six independent, constant material patches (stdlib only)."""
import json
from pathlib import Path
import struct
import zlib

out = Path(__file__).resolve().parent
positions, normals, uvs, indices = [], [], [], []
centers = [(-1.2, 0.65), (0, 0.65), (1.2, 0.65),
           (-1.2, -0.65), (0, -0.65), (1.2, -0.65)]
for i, (x, y) in enumerate(centers):
    positions.extend([(x-.45, y-.4, 0), (x+.45, y-.4, 0),
                      (x+.45, y+.4, 0), (x-.45, y+.4, 0)])
    normals.extend([(0, 0, 1)] * 4)
    uvs.extend([(0, 1), (1, 1), (1, 0), (0, 0)])
    indices.extend(i*4 + j for j in (0, 1, 2, 0, 2, 3))
parts = [struct.pack('<36H', *indices),
         b''.join(struct.pack('<3f', *p) for p in positions),
         b''.join(struct.pack('<3f', *p) for p in normals),
         b''.join(struct.pack('<2f', *p) for p in uvs)]
views, offset = [], 0
for i, part in enumerate(parts):
    views.append(dict(buffer=0, byteOffset=offset, byteLength=len(part),
                      target=34963 if i == 0 else 34962))
    offset += len(part)
(out / 'Lighting.bin').write_bytes(b''.join(parts))
accessors = [dict(bufferView=0, byteOffset=i*12, componentType=5123,
                  count=6, type='SCALAR', min=[i*4], max=[i*4+3]) for i in range(6)]
accessors.extend([
    dict(bufferView=1, componentType=5126, count=24, type='VEC3', min=[-1.65,-1.05,0], max=[1.65,1.05,0]),
    dict(bufferView=2, componentType=5126, count=24, type='VEC3'),
    dict(bufferView=3, componentType=5126, count=24, type='VEC2')])
materials = []
for i, name in enumerate(['AOQuarter', 'AOZeroStrength', 'AONeutral',
                           'EmissionConstant', 'EmissionTexture', 'EmissionOff']):
    m = dict(name=name, pbrMetallicRoughness=dict(
        baseColorFactor=[.6,.6,.6,1] if i < 3 else [0,0,0,1],
        metallicFactor=0, roughnessFactor=1))
    if i < 2:
        m['occlusionTexture'] = dict(index=0, strength=1 if i == 0 else 0)
    if i in (3, 4):
        m['emissiveFactor'] = [.25,.5,1]
        m['extensions'] = dict(KHR_materials_emissive_strength=dict(emissiveStrength=2))
    if i == 4:
        m['emissiveTexture'] = dict(index=1)
    materials.append(m)
doc = dict(asset=dict(version='2.0', generator='CreatorEngine PBR lighting regression'),
           extensionsUsed=['KHR_materials_emissive_strength'], scene=0,
           scenes=[dict(nodes=[0])], nodes=[dict(mesh=0,name='Lighting')],
           meshes=[dict(name='Lighting',primitives=[dict(
               attributes=dict(POSITION=6,NORMAL=7,TEXCOORD_0=8),indices=i,material=i) for i in range(6)])],
           materials=materials, textures=[dict(source=0),dict(source=1)],
           images=[dict(uri='Occlusion.png'),dict(uri='Emission.png')],
           accessors=accessors,bufferViews=views,buffers=[dict(uri='Lighting.bin',byteLength=offset)])
(out / 'Lighting.gltf').write_text(json.dumps(doc,indent=2)+'\n',encoding='utf-8')
def png(name, rgba):
    def chunk(tag, data):
        return struct.pack('>I', len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    raw = (b'\0'+bytes(rgba)*4)*4
    (out/name).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',4,4,8,6,0,0,0))+
                          chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
png('Occlusion.png',(64,255,255,255))
png('Emission.png',(128,64,32,255))
