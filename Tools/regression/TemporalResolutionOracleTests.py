import importlib.util, math, unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('resolution',Path(__file__).resolve().parents[1]/'temporal-validation/Compare-TemporalResolution.py')
oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
class PerspectiveTests(unittest.TestCase):
    def setUp(self):
        self.camera={'projection':'perspective','position':[0,0,0],'verticalFovDegrees':90,'aspectRatio':961/541,'near':.1,'far':20,'depthConvention':'forward-z','width':8,'height':8}
        self.case={'route':'static','geometry':{'vertices':[{'position':p} for p in ([-1,-1,0],[1,-1,0],[1,1,0],[-1,1,0])],'indices':[0,2,1,0,3,2]},'previous':{'worldTranslation':[0,0,4]},'current':{'worldTranslation':[.25,.125,4]}}
    def test_odd_extent_uses_display_aspect(self):
        point=oracle.motion.project([1,0,4],self.camera,[321,197])
        self.assertAlmostEqual(point[0],321/2+321/(8*(961/541)))
        self.assertNotAlmostEqual(point[0],321/2+197/8)
    def test_jitter_does_not_enter_motion(self):
        jitter=(oracle.halton(36,2),oracle.halton(36,3))
        probes=oracle.motion.geometry_probes(self.case,self.camera,[321,197],jitter,True)
        for p in probes:
            self.assertAlmostEqual(p['expectedPixels'][0],-.25*321/(8*(961/541)),places=6)
            self.assertAlmostEqual(p['expectedPixels'][1],.125*197/8,places=6)
            self.assertAlmostEqual(p['expectedDepth'],20/19.9-2/(19.9*4),places=6)
    def test_native_oracle_still_rejects_perspective(self):
        with self.assertRaises(ValueError):oracle.motion.geometry_probes(self.case,self.camera,[321,197])
    def test_camera_cannot_use_render_aspect(self):
        aspect=321/197
        projection=[1/aspect,0,0,0,0,1,0,0,0,0,20/19.9,1,0,0,-2/19.9,0]
        manifest={'camera':{'view':[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],'projection':projection}}
        with self.assertRaises(ValueError):oracle.camera_proof(manifest,self.camera,[961,541])
    def test_alpha_contour_distance_uses_source_uv_jacobian(self):
        probe={'uv':[.51,.5],'uvDx':[.53,.5],'uvDy':[.51,.52]}
        boundary=[([.5,0],[.5,1])]
        self.assertAlmostEqual(oracle.motion.alpha_coverage_distance(probe,boundary),.5)
        probe={'uv':[.6,.5],'uvDx':[.62,.5],'uvDy':[.6,.52]}
        self.assertAlmostEqual(oracle.motion.alpha_coverage_distance(probe,boundary),5.)
    def test_alpha_edge_remains_an_input_probe(self):
        probes=oracle.motion.geometry_probes(self.case,self.camera,[321,197],(0,0),True)
        for p in probes:p['alphaCoverageMarginPixels']=6.
        probes[0]['alphaCoverageMarginPixels']=.47
        selected=oracle.interior_quality_probes(self.case,self.camera,[321,197],probes,(0,0))
        self.assertNotIn(probes[0],selected)
        self.assertEqual(len(probes),6)
        self.assertEqual(len(selected),5)
    def test_missing_interiors_cannot_pass(self):
        probes=oracle.motion.geometry_probes(self.case,self.camera,[321,197],(0,0),True)
        for p in probes:p['alphaCoverageMarginPixels']=.47
        with self.assertRaises(ValueError):
            oracle.interior_quality_probes(self.case,self.camera,[321,197],probes,(0,0))
    def test_interior_color_threshold_is_unchanged(self):
        images={'sdkInput':[1.,.25,.125,1.]*400,'preToneHdr':[.5,.25,.125,1.]*400}
        probes=[{'pixel':p} for p in ([5,5],[10,10],[15,15])]
        with self.assertRaisesRegex(ValueError,'10 percent'):
            oracle.output_proof(images,probes,[20,20],[20,20],(0,0))
if __name__=='__main__':unittest.main()
