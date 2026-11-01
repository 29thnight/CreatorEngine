"""Bounds-only migration proof must reject changed geometry and forged captures."""
import hashlib
import struct
import tempfile
import unittest
from pathlib import Path

from geometry_recook_identity import prove_bounds_recook


class RecookIdentityTests(unittest.TestCase):
    def fixture(self, root, bones=2, change=None, forge=False):
        metadata = bytearray(range(160))
        struct.pack_into("<I", metadata, 84, bones)
        count = bones + 1 if bones else 0
        bounds = struct.pack("<I", count) + struct.pack("<8f", -1, -2, -3, 1, 1, 2, 3, 1) * count
        new_metadata = metadata[:140] + bounds + metadata[140:]
        if change == "metadata":
            new_metadata[30] ^= 1
        if change == "bound":
            struct.pack_into("<f", new_metadata, 144, float("nan"))
        pages = b"compressed-geometry-page"
        old = b"CEGE" + struct.pack("<II", 9, len(metadata)) + metadata + pages
        new = bytearray(b"CEGE" + struct.pack("<II", 10, len(new_metadata)) + new_metadata + pages)
        if change == "page":
            new[-1] ^= 1
        manifests = []
        for index, blob in enumerate((old, new)):
            folder = root / str(index)
            folder.mkdir()
            (folder / "asset.cege").write_bytes(blob)
            manifests.append({"draws": [dict(assetId="asset", artifactSchema=9 + index,
                                            artifactBytes=len(blob), contentSha256=list(hashlib.sha256(blob).digest()))]})
        if forge:
            manifests[1]["draws"][0]["contentSha256"][0] ^= 1
        return manifests

    def test_valid_skinned_and_static(self):
        for bones in (0, 2):
            with self.subTest(bones=bones), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                left, right = self.fixture(root, bones)
                replacements, proofs = prove_bounds_recook(left, right, root)
                self.assertEqual(len(replacements), 1)
                self.assertEqual(proofs[0]["addedBounds"], bones + 1 if bones else 0)

    def test_changed_inputs_rejected(self):
        for change in ("metadata", "page", "bound"):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                left, right = self.fixture(root, change=change)
                with self.assertRaises(ValueError):
                    prove_bounds_recook(left, right, root)

    def test_forged_capture_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            left, right = self.fixture(root, forge=True)
            with self.assertRaises(KeyError):
                prove_bounds_recook(left, right, root)

    def test_wrong_capture_extent_and_asset_rejected(self):
        for field, value in (("artifactBytes", 1), ("assetId", "other"), ("artifactSchema", 11)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                left, right = self.fixture(root)
                right["draws"][0][field] = value
                with self.assertRaises(ValueError):
                    prove_bounds_recook(left, right, root)

    def test_multiple_revisions_of_same_asset_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            left, right = self.fixture(root)
            blob = bytearray((root / "0/asset.cege").read_bytes())
            blob[-1] ^= 1
            folder = root / "third"
            folder.mkdir()
            (folder / "asset.cege").write_bytes(blob)
            changed = dict(left["draws"][0], contentSha256=list(hashlib.sha256(blob).digest()))
            left["draws"].append(changed)
            with self.assertRaises(ValueError):
                prove_bounds_recook(left, right, root)


if __name__ == "__main__":
    unittest.main()
