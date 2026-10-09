"""Authored, UNEXECUTED unit fixture for the independent oracle, not GPU evidence."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "motion_oracle", ROOT / "Tools/temporal-validation/Compare-TemporalMotion.py")
ORACLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ORACLE)


class TemporalMotionOracleTests(unittest.TestCase):
    def setUp(self):
        self.fixture = json.loads((ROOT / "Tools/regression/fixtures/temporal-motion/analytic-probes.json").read_text())

    def test_authored_translation_goldens(self):
        # Closed-form 256 / 16 = 16 px/world-unit, with screen Y inverted.
        goldens = [[-4, 2], [-8, -4], [6, 3], [8, 4], [-3, -6], [-2, 1]]
        for probe, golden in zip(self.fixture["probes"], goldens):
            pixel, actual = ORACLE.expected_motion(probe, self.fixture)
            self.assertEqual(pixel, [component + .5 for component in probe["pixel"]])
            self.assertEqual(actual, golden)

    def test_camera_translation_is_included(self):
        probe = self.fixture["probes"][0]
        probe["previous"] = copy.deepcopy(probe["current"])
        self.fixture["previousCamera"]["position"] = [-.125, 0, 0]
        self.assertEqual(ORACLE.expected_motion(probe, self.fixture)[1], [2, 0])

    def test_pinhole_depth_changes_magnitude(self):
        camera = {"projection": "perspective", "position": [0, 0, 0], "verticalFovDegrees": 90}
        near = ORACLE.project([1, 0, 4], camera, [256, 256])
        far = ORACLE.project([1, 0, 8], camera, [256, 256])
        self.assertAlmostEqual(near[0] - 128, 32)
        self.assertAlmostEqual(far[0] - 128, 16)

    def test_zeroed_bone_motion_changes_expected_vector(self):
        probe = self.fixture["probes"][1]
        probe["current"]["skinTranslations"] = probe["previous"]["skinTranslations"]
        self.assertNotEqual(ORACLE.expected_motion(probe, self.fixture)[1], [-8, -4])

    def test_wrong_bone_weight_is_rejected(self):
        endpoint = self.fixture["probes"][1]["current"]
        endpoint["skinTranslations"][0]["weight"] = .5
        with self.assertRaises(ValueError):
            ORACLE.world_point(endpoint)

    def test_pixel_axes_and_endpoint_reversal(self):
        probe = self.fixture["probes"][0]
        probe["previous"], probe["current"] = probe["current"], probe["previous"]
        self.assertEqual(ORACLE.expected_motion(probe, self.fixture)[1], [4, -2])


class ProductOwnershipOracleTests(unittest.TestCase):
    """Synthetic contract tests only. These cannot create executed GPU evidence."""

    def setUp(self):
        self.extent = [256, 256]
        self.camera = {"projection": "orthographic", "position": [0, 0, 0],
                       "width": 8, "height": 8, "near": .1, "far": 20,
                       "depthConvention": "forward-z"}
        self.case = {"route": "static", "geometry": {
            "vertices": [{"position": [-1, -1, 0], "weights": [.9, .1]},
                         {"position": [1, -1, 0], "weights": [.9, .1]},
                         {"position": [1, 1, 0], "weights": [.1, .9]},
                         {"position": [-1, 1, 0], "weights": [.1, .9]}],
            "indices": [0, 2, 1, 0, 3, 2]},
            "previous": {"worldTranslation": [0, 0, 4]},
            "current": {"worldTranslation": [.25, .125, 4]},
            "ownership": {"attachment": "baseColor", "expectedColor": [.8, .1, .2],
                          "colorTolerance": .04, "minimumContrast": .1}}

    def test_triangle_raster_derives_pixel_center_and_forward_depth(self):
        for probe in ORACLE.geometry_probes(self.case, self.camera, self.extent):
            self.assertAlmostEqual(probe["expectedPixels"][0], -8)
            self.assertAlmostEqual(probe["expectedPixels"][1], 4)
            self.assertAlmostEqual(probe["expectedDepth"], (4 - .1) / (20 - .1))

    def test_nonuniform_skin_weights_prevent_rigid_fallback(self):
        self.case["route"] = "skinned"
        self.case["previous"]["boneTranslations"] = [[0, 0, 0], [0, 0, 0]]
        self.case["current"] = {"worldTranslation": [0, 0, 4],
                                "boneTranslations": [[.125, -.0625, 0], [.625, -.3125, 0]]}
        probes = ORACLE.geometry_probes(self.case, self.camera, self.extent)
        self.assertGreater(max(p["expectedPixels"][0] for p in probes) -
                           min(p["expectedPixels"][0] for p in probes), .5)

    def test_bad_weights_and_tiny_motion_are_rejected(self):
        self.case["current"]["boneTranslations"] = [[0, 0, 0], [1, 0, 0]]
        self.case["geometry"]["vertices"][0]["weights"] = [.2, .2]
        with self.assertRaises(ValueError):
            ORACLE.posed_vertices(self.case, "current", 0)
        self.case["current"] = copy.deepcopy(self.case["previous"])
        with self.assertRaises(ValueError):
            ORACLE.geometry_probes(self.case, self.camera, self.extent)

    def test_actual_control_disappearance_is_required(self):
        probes = ORACLE.geometry_probes(self.case, self.camera, self.extent)
        n = self.extent[0] * self.extent[1]
        positive = {"baseColor": [0.] * (4 * n), "preToneHdr": [0.] * (4 * n),
                    "depth": [1.] * n, "temporalDepth": [1.] * n,
                    "temporalMotionRG": [0.] * (2 * n), "temporalResponsive": [0.] * n}
        control = copy.deepcopy(positive)
        for probe in probes:
            x, y = probe["pixel"]
            offset = y * self.extent[0] + x
            positive["baseColor"][4 * offset:4 * offset + 3] = [.8, .1, .2]
            positive["depth"][offset] = probe["expectedDepth"]
            positive["temporalDepth"][offset] = probe["expectedDepth"]
            positive["temporalMotionRG"][2 * offset:2 * offset + 2] = probe["expectedPixels"]
        good = ORACLE.ownership_result(self.case, probes, positive, control, self.extent)
        self.assertTrue(all(p["passed"] for p in good))
        still_visible = ORACLE.ownership_result(self.case, probes, positive, positive, self.extent)
        self.assertFalse(any(p["passed"] for p in still_visible))
        fallback = copy.deepcopy(positive)
        fallback["temporalMotionRG"] = [0.] * (2 * n)
        self.assertFalse(any(p["passed"] for p in
                             ORACLE.ownership_result(self.case, probes, fallback, control, self.extent)))

    def test_sprite_cannot_claim_gbuffer_ownership(self):
        self.case["route"] = "sprite"
        with self.assertRaises(ValueError):
            ORACLE.ownership_result(self.case, [], {}, {}, self.extent)

    def test_missing_route_is_not_acceptance(self):
        self.assertEqual(ORACLE.ROUTES,
                         {"static", "skinned", "instanced", "meshlet", "alpha", "decal", "sprite"})
        self.assertNotEqual(ORACLE.PRODUCT_SCHEMA, "temporal.motion.analytic-fixture.v1")

    def submission(self):
        digest = "a" * 64
        case = {"sessionId": 7, "route": "static", "allowMeshlets": False, "input": {"sha256": digest}}
        manifest = {"realFrameId": 109, "frameId": 209, "viewId": 3, "sceneEpoch": 4,
                    "historyRevision": 6, "fixtureSessionId": 7, "fixtureStepId": 2,
                    "fixturePredecessorStepId": 1, "fixtureInputSha256": digest,
                    "fixturePredecessorInputSha256": digest, "fixtureCoalesced": False,
                    "fixtureAllowMeshlets": False,
                    "temporalMotion": {"fixtureSessionId": 7, "fixtureStepId": 2,
                        "fixturePredecessorStepId": 1, "previousSubmittedFixtureSessionId": 7,
                        "previousSubmittedFixtureStepId": 1, "previousSubmittedRealFrameId": 106,
                        "previousSubmittedSourceFrameId": 206, "previousSubmittedViewId": 3,
                        "previousSubmittedSceneEpoch": 4, "previousSubmittedHistoryRevision": 6,
                        "previousSubmittedHistoryGeneration": 8, "historyGeneration": 8,
                        "fixtureInputSha256": digest, "fixturePredecessorInputSha256": digest,
                        "previousSubmittedFixtureInputSha256": digest,
                        "previousSubmittedFixtureSourceVerified": True,
                        "fixtureAllowMeshlets": False, "previousSubmittedFixtureAllowMeshlets": False},
                    "fixtureSubmission": {"valid": True, "sourceVerified": True,
                        "submissionProof": "native-submit-succeeded-and-temporal-history-committed",
                        "historyReset": False, "coalesced": False, "sessionId": 7, "stepId": 2,
                        "requestedPredecessorStepId": 1, "predecessorStepId": 1,
                        "realFrameId": 109, "sourceFrameId": 209, "predecessorRealFrameId": 106,
                        "predecessorSourceFrameId": 206, "viewId": 3, "sceneEpoch": 4,
                        "historyGeneration": 8, "historyRevision": 6, "inputSha256": digest,
                        "predecessorInputSha256": digest, "requestedPredecessorInputSha256": digest,
                        "allowMeshlets": False}}
        return manifest, case

    def test_exact_submitted_predecessor_need_not_be_adjacent(self):
        manifest, case = self.submission()
        ORACLE.submission_proof(manifest, case, 2, 1)

    def test_stale_coalesced_wrong_hash_or_wrong_step_cannot_pass(self):
        for field, bad in (("predecessorRealFrameId", 108), ("stepId", 3),
                           ("predecessorInputSha256", "b" * 64), ("coalesced", True),
                           ("sourceVerified", False), ("sourceFrameId", 210)):
            with self.subTest(field=field):
                manifest, case = self.submission()
                manifest["fixtureSubmission"][field] = bad
                with self.assertRaises(ValueError):
                    ORACLE.submission_proof(manifest, case, 2, 1)

    def test_alpha_decodes_hashed_source_not_route_name(self):
        self.case["route"] = "alpha"
        for vertex, uv in zip(self.case["geometry"]["vertices"], ([0, 1], [1, 1], [1, 0], [0, 0])):
            vertex["uv"] = uv
        # Test-generated bytes are never a product capture or execution record.
        pixels = bytes(v for y in range(16) for x in range(16)
                       for v in (255, 255, 255, 255 if x >= 8 else 0))
        header = struct.pack("<12I2Q", int.from_bytes(b"CECT", "little"),
                             2, 64, 2, 2, 2, 16, 16, 1, 1, 1, 40, 104, 104 + len(pixels))
        payload = header + struct.pack("<II4Q", 16, 16, 64, 1024, 104, 1024) + pixels
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            (base / "alpha.cetex").write_bytes(payload)
            reference = {"path": "alpha.cetex", "sha256": hashlib.sha256(payload).hexdigest()}
            self.case["ownership"]["alphaTexture"] = reference
            probes = ORACLE.geometry_probes(self.case, self.camera, self.extent)
            probes = ORACLE.alpha_coverage(base, self.case, probes, [reference])
            self.assertEqual({p["covered"] for p in probes}, {True, False})
            (base / "alpha.cetex").write_bytes(payload[:-1] + b"\x00")
            with self.assertRaises(ValueError):
                ORACLE.alpha_coverage(base, self.case, probes, [reference])

    def test_missing_gpu_capture_is_incomplete(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            asset = b"unit-test-only-input"
            (base / "input.bin").write_bytes(asset)
            fixture = {"schema": ORACLE.PRODUCT_SCHEMA, "extent": self.extent,
                       "camera": self.camera, "inputAssets": [{"path": "input.bin",
                           "sha256": hashlib.sha256(asset).hexdigest()}],
                       "cases": [dict(self.case, route=route) for route in sorted(ORACLE.ROUTES)]}
            path = base / "fixture.json"
            path.write_text(json.dumps(fixture))
            result = ORACLE.compare(base, path)
            self.assertEqual(result["acceptanceStatus"], "INCOMPLETE")
            self.assertFalse(result["routeCoverageValidated"])

    def test_recorded_labels_without_independent_instances_are_rejected(self):
        def manifest(frame, entries):
            return {"frameId": frame, "viewId": 3, "sceneEpoch": 4, "geometryRoutes": {
                "source": "joined-command-recording-after-native-submission", "pass": "LX.Scene.GBuffer",
                "sourceFrameId": frame, "viewId": 3, "sceneEpoch": 4, "selected": entries}}
        entry = {"geometryKey": 10, "temporalObjectId": 20, "temporalInstanceId": 30,
                 "meshShader": False}
        self.case["route"] = "instanced"
        self.case["current"]["instanceTranslations"] = [[-1, 0, 4], [1, 0, 4]]
        with self.assertRaises(ValueError):
            ORACLE.geometry_route_proof(manifest(100, [entry, entry]), manifest(102, []), self.case)
        second = dict(entry, temporalInstanceId=31)
        ORACLE.geometry_route_proof(manifest(100, [entry, second]), manifest(102, []), self.case)

    def test_decal_control_must_keep_same_receiver(self):
        def manifest(frame, object_id):
            return {"frameId": frame, "viewId": 3, "sceneEpoch": 4, "geometryRoutes": {
                "source": "joined-command-recording-after-native-submission", "pass": "LX.Scene.GBuffer",
                "sourceFrameId": frame, "viewId": 3, "sceneEpoch": 4, "selected": [
                    {"geometryKey": 10, "temporalObjectId": object_id, "temporalInstanceId": 0,
                     "meshShader": False}]}}
        self.case["route"] = "decal"
        ORACLE.geometry_route_proof(manifest(100, 20), manifest(102, 20), self.case)
        with self.assertRaises(ValueError):
            ORACLE.geometry_route_proof(manifest(100, 20), manifest(102, 21), self.case)

    def test_actual_camera_must_match_authored_camera(self):
        manifest = {"camera": {
            "view": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
            "projection": [.25, 0, 0, 0, 0, .25, 0, 0, 0, 0, 1 / 19.9, 0, 0, 0, -.1 / 19.9, 1]},
            "temporalMotion": {"jitterX": 0, "jitterY": 0, "previousJitterX": 0, "previousJitterY": 0}}
        ORACLE.camera_proof(manifest, self.camera)
        manifest["camera"]["view"][12] = 1
        with self.assertRaises(ValueError):
            ORACLE.camera_proof(manifest, self.camera)

    def test_sealed_meshlet_choice_cannot_be_substituted(self):
        manifest, case = self.submission()
        manifest["fixtureSubmission"]["allowMeshlets"] = True
        with self.assertRaises(ValueError):
            ORACLE.submission_proof(manifest, case, 2, 1)

    def test_sprite_uses_exact_immediate_stage_and_shared_control_owner(self):
        owner = {"name": "hdr-3-Lighting", "file": "hdr-3-Lighting.f32",
                 "readbackStage": "hdr-3-Lighting", "sharedReadback": False,
                 "resource": 4, "version": 1, "kind": 0, "graphEpoch": 2,
                 "encoding": "float32-le-row-major", "width": 1, "height": 1,
                 "channels": 4, "nonfinite": 0}
        sprite = dict(owner, name="hdr-4-Sprite", sharedReadback=True)
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            (base / owner["file"]).write_bytes(struct.pack("<4f", .75, .125, .25, 1))
            manifest = {"diagnosticStages": [owner, sprite]}
            pixels, _ = ORACLE.load_sprite_stage(base, manifest, [1, 1])
            self.assertEqual(pixels, [.75, .125, .25, 1])
            sprite["file"] = "preToneHdr.f32"
            with self.assertRaises(ValueError):
                ORACLE.load_sprite_stage(base, manifest, [1, 1])
            manifest["diagnosticStages"] = [owner]
            with self.assertRaises(ORACLE.MissingProof):
                ORACLE.load_sprite_stage(base, manifest, [1, 1])


if __name__ == "__main__":
    unittest.main()
