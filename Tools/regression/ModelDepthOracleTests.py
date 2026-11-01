"""Analytic triangle and corruption canaries for the model depth oracle."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np

spec = importlib.util.spec_from_file_location('depth_oracle', Path(__file__).with_name('verify-model-depth.py'))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class ModelDepthOracleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.model = self.root/'triangle.glb'
        binary = struct.pack('<9f3I', -.8,-.8,-.5, .8,-.8,-.5, 0,.8,-.5, 0,1,2)
        doc = dict(asset={'version':'2.0'}, scenes=[{'nodes':[0]}], nodes=[{'mesh':0}],
                   meshes=[{'primitives':[{'attributes':{'POSITION':0},'indices':1,'material':0}]}],
                   materials=[{}], buffers=[{'byteLength':len(binary)}],
                   bufferViews=[{'buffer':0,'byteOffset':0,'byteLength':36},
                                {'buffer':0,'byteOffset':36,'byteLength':12}],
                   accessors=[{'bufferView':0,'componentType':5126,'count':3,'type':'VEC3'},
                              {'bufferView':1,'componentType':5125,'count':3,'type':'SCALAR'}])
        self.write_glb(doc,binary)
        self.model.with_suffix('.glb.meta').write_text('  - kind: mesh\n    assetId: test\n    binding: mesh/0\n')
        identity = np.eye(4).reshape(-1).tolist()
        self.manifest = dict(source='product-live',validationCount=0,width=64,height=64,
                             camera={'view':identity,'projection':identity},
                             draws=[dict(meshId='test',pose=[],world=identity,coverageFlags=1)])
        self.write_manifest()
        y,x = np.mgrid[0:64,0:64]
        nx,ny = (x+.5)/32-1,1-(y+.5)/32
        self.pixels = np.where((ny>=-.8)&(np.abs(nx)<=(.8-ny)/2),.5,1).astype('<f4')
        self.pixels.tofile(self.root/'depth.f32')

    def write_glb(self,doc,binary):
        self.doc = doc
        payload = json.dumps(doc).encode()
        payload += b' '*(-len(payload)%4)
        self.model.write_bytes(struct.pack('<III',0x46546c67,2,28+len(payload)+len(binary))+
                               struct.pack('<II',len(payload),0x4e4f534a)+payload+
                               struct.pack('<II',len(binary),0x004e4942)+binary)

    def tearDown(self):
        self.temp.cleanup()

    def write_manifest(self):
        (self.root/'manifest.json').write_text(json.dumps(self.manifest))

    def run_oracle(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return oracle.verify(self.model,self.root,self.root/'result.json')

    def test_analytic_triangle(self):
        self.assertTrue(self.run_oracle())
        result = json.loads((self.root/'result.json').read_text())
        self.assertGreater(result['interiorOpaqueProbes'],1000)
        self.assertLess(result['maxAbsoluteError'],1e-12)

    def test_meshlet_grid_rounding_and_raw_fallback(self):
        # x uses a 1/65535 grid; y exceeds the 1e-4 error budget and stays raw.
        positions = np.array([[0,0,2],[.5,.5,2],[1,1000,2]],dtype=np.float32)
        decoded = oracle.meshlet_positions(positions)
        expected_x = np.float32(32768)*np.float32(1/65535)
        self.assertEqual(decoded[1,0],float(expected_x))
        np.testing.assert_array_equal(decoded[:,1:],positions[:,1:])
        self.assertLess(abs(decoded[1,0]-.5),1e-4)

    def test_wrong_depth_is_rejected_without_changing_probe_count(self):
        self.assertTrue(self.run_oracle())
        count = json.loads((self.root/'result.json').read_text())['interiorOpaqueProbes']
        self.pixels[32,32] = .7
        self.pixels.tofile(self.root/'depth.f32')
        self.assertFalse(self.run_oracle())
        result = json.loads((self.root/'result.json').read_text())
        self.assertEqual(result['interiorOpaqueProbes'],count)
        self.assertEqual(result['failingPixels'],1)

    def test_missing_geometry_depth_is_rejected(self):
        np.ones_like(self.pixels).tofile(self.root/'depth.f32')
        self.assertFalse(self.run_oracle())

    def test_camera_depth_change_is_rejected(self):
        self.manifest['camera']['view'] = self.manifest['camera']['view'].copy()
        self.manifest['camera']['view'][14] = .1
        self.write_manifest()
        self.assertFalse(self.run_oracle())

    def test_foreground_subpixel_edge_is_not_a_background_interior_probe(self):
        # Its left edge lies 0.0005 pixels to the right of pixel center (32,32).
        # Rounding to 1/256 pixels covers that pixel with the foreground triangle.
        x = .015625 + .0005/32
        y = -.015625
        binary = struct.pack('<18f6I', -.8,-.8,-.5, .8,-.8,-.5, 0,.8,-.5,
                             x,y-.01,-.25, x+.01,y,-.25, x,y+.01,-.25, 0,1,2,3,4,5)
        self.doc['buffers'][0]['byteLength'] = len(binary)
        self.doc['bufferViews'][0]['byteLength'] = 72
        self.doc['bufferViews'][1].update(byteOffset=72,byteLength=24)
        for accessor in self.doc['accessors']:
            accessor['count'] = 6
        self.write_glb(self.doc,binary)
        self.pixels[32,32] = .25
        self.pixels.tofile(self.root/'depth.f32')
        self.assertTrue(self.run_oracle())
        result = json.loads((self.root/'result.json').read_text())
        self.assertEqual(result['foregroundBoundaryExcludedPixels'],1)


if __name__ == '__main__':
    unittest.main()
