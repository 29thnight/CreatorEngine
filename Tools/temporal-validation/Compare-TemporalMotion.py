#!/usr/bin/env python3
"""Independent analytic motion probes. Authored, UNEXECUTED source.

This compares real capture bytes with an axis-aligned pinhole/orthographic
oracle. It does not import the engine's motion, matrix or shader helpers.
Numeric agreement alone cannot prove which geometry route owned a pixel.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def vector(value, length=3):
    if not isinstance(value, list) or len(value) != length:
        raise ValueError(f"expected a {length}-component vector")
    result = [float(component) for component in value]
    if not all(math.isfinite(component) for component in result):
        raise ValueError("nonfinite fixture input")
    return result


def world_point(endpoint):
    point = vector(endpoint["localPoint"])
    skin = endpoint.get("skinTranslations", [])
    if skin:
        weights = [float(bone["weight"]) for bone in skin]
        if any(not math.isfinite(w) or w < 0 for w in weights) or abs(sum(weights) - 1) > 1e-9:
            raise ValueError("fixture bone weights must be finite and sum to one")
        shifts = [vector(bone["translation"]) for bone in skin]
        point = [point[axis] + sum(w * shift[axis] for w, shift in zip(weights, shifts))
                 for axis in range(3)]
    for name in ("instanceTranslation", "worldTranslation"):
        shift = vector(endpoint.get(name, [0, 0, 0]))
        point = [point[axis] + shift[axis] for axis in range(3)]
    return point


def project(point, camera, extent):
    # Deliberately independent scalar geometry: no shader reprojection matrix,
    # raster depth inversion, previous-clip helper or captured motion is used.
    eye = vector(camera["position"])
    x, y, z = [point[axis] - eye[axis] for axis in range(3)]
    width, height = extent
    if camera["projection"] == "orthographic":
        ndc_x = 2 * x / float(camera["width"])
        ndc_y = 2 * y / float(camera["height"])
    elif camera["projection"] == "perspective":
        if z <= 0:
            raise ValueError("fixture point is behind the pinhole")
        tangent = math.tan(math.radians(float(camera["verticalFovDegrees"])) / 2)
        ndc_x = x / (z * tangent * width / height)
        ndc_y = y / (z * tangent)
    else:
        raise ValueError("unknown fixture projection")
    if not all(math.isfinite(value) for value in (ndc_x, ndc_y)):
        raise ValueError("invalid fixture projection")
    return [(ndc_x + 1) * width / 2, (1 - ndc_y) * height / 2]


def expected_motion(probe, fixture):
    previous = project(world_point(probe["previous"]), fixture["previousCamera"], fixture["extent"])
    current = project(world_point(probe["current"]), fixture["currentCamera"], fixture["extent"])
    return current, [previous[axis] - current[axis] for axis in range(2)]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_attachment(root, manifest, name, channels, extent):
    matching = [item for item in manifest["attachments"] if item["name"] == name]
    require(len(matching) == 1, f"missing or duplicate {name} attachment")
    item = matching[0]
    require(item["encoding"] == "float32-le-row-major" and item["channels"] == channels,
            f"wrong {name} encoding")
    require([item["width"], item["height"]] == extent and item["nonfinite"] == 0,
            f"wrong {name} extent/nonfinite evidence")
    require(item["realFrameId"] == manifest["realFrameId"] and item["frameKind"] == "real" and
            item["generatedOrdinal"] == 0, f"{name} frame identity mismatch")
    path = (root / item["file"]).resolve()
    require(path.parent == root, "capture attachment escapes capture directory")
    payload = path.read_bytes()
    require(len(payload) == extent[0] * extent[1] * channels * 4, f"wrong {name} byte count")
    values = [value[0] for value in struct.iter_unpack("<f", payload)]
    require(all(math.isfinite(value) for value in values), f"nonfinite {name} bytes")
    return values, hashlib.sha256(payload).hexdigest()


def compare(capture, fixture_path):
    root = capture.resolve()
    manifest_bytes = (root / "manifest.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    fixture_bytes = fixture_path.read_bytes()
    fixture = json.loads(fixture_bytes)
    require(fixture["schema"] == "temporal.motion.analytic-fixture.v1", "unknown fixture schema")
    extent = fixture["extent"]
    require(len(extent) == 2 and all(isinstance(value, int) and value > 0 for value in extent),
            "invalid fixture extent")
    require(manifest["source"] == "product-live" and manifest["captureMode"] == "temporal-motion-v1",
            "requires an actual product motion capture")
    require(manifest["temporalProvenanceSchemaVersion"] == 2 and manifest["frameKind"] == "real" and
            manifest["generatedOrdinal"] == 0 and manifest["temporalNativeGateActive"] is True,
            "requires native real-frame provenance")
    require(manifest["upscaler"] == "none" and manifest["frameGenerator"] == "none" and
            manifest["spatialMode"] == "off" and manifest["deepDvcApplied"] is False,
            "image reconstruction/scaling/enhancement must be excluded")
    require(manifest["validationCount"] == 0, "capture contains GPU validation errors")
    require(manifest["realFrameId"] > 0 and manifest["resolutionState"] == "native" and
            [manifest["renderWidth"], manifest["renderHeight"]] == extent and
            [manifest["displayWidth"], manifest["displayHeight"]] == extent,
            "capture is not an identified native-resolution frame")
    metadata = manifest["temporalMotion"]
    require(metadata["source"] == "final-production-temporal-inputs" and metadata["schemaVersion"] == 1,
            "missing final production motion source")
    require(metadata["historyReset"] is False and metadata["realFrameId"] == manifest["realFrameId"] and
            metadata["historyGeneration"] > 0, "reset/stale/unidentified motion frame")
    require(metadata["direction"] == "current-to-previous" and metadata["units"] == "render-pixels" and
            metadata["origin"] == "top-left" and metadata["jitterIncluded"] is False,
            "motion convention mismatch")
    require([metadata["renderWidth"], metadata["renderHeight"]] == extent, "fixture resolution mismatch")
    motion, motion_hash = load_attachment(root, manifest, "temporalMotionRG", 2, extent)
    responsive, _ = load_attachment(root, manifest, "temporalResponsive", 1, extent)
    probes = []
    names = set()
    for probe in fixture["probes"]:
        require(probe["name"] not in names, "duplicate fixture probe name")
        names.add(probe["name"])
        x, y = probe["pixel"]
        require(isinstance(x, int) and isinstance(y, int) and 0 <= x < extent[0] and 0 <= y < extent[1],
                "probe pixel is outside capture")
        current, expected = expected_motion(probe, fixture)
        require(abs(current[0] - x - .5) < 1e-7 and abs(current[1] - y - .5) < 1e-7,
                "authored current endpoint does not project to the probed pixel center")
        require(max(abs(value) for value in expected) >= 1, "zero/tiny motion cannot prove sign and scale")
        tolerance = float(probe.get("tolerancePixels", .0625))
        require(math.isfinite(tolerance) and 0 < tolerance <= .125, "unbounded motion tolerance")
        offset = y * extent[0] + x
        observed = motion[2 * offset:2 * offset + 2]
        errors = [abs(observed[axis] - expected[axis]) for axis in range(2)]
        valid = abs(responsive[offset]) <= .001
        probes.append({"name": probe["name"], "authoredRoute": probe["route"], "pixel": [x, y],
                       "expectedPixels": expected, "observedPixels": observed, "absoluteErrors": errors,
                       "responsive": responsive[offset], "passed": valid and max(errors) <= tolerance})
    require(probes, "no analytic probes supplied")
    return {"schema": "temporal.motion.numeric-evidence.v1", "realFrameId": manifest["realFrameId"],
            "captureManifestSha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "motionSha256": motion_hash, "fixtureSha256": hashlib.sha256(fixture_bytes).hexdigest(),
            "numericProbesPassed": all(probe["passed"] for probe in probes), "probes": probes,
            "routeCoverageValidated": False, "acceptanceStatus": "incomplete-route-fixture",
            "remainingRequirement": "Execute a production all-route scene driver and independently verify each probe's route ownership"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        report = compare(args.capture, args.fixture)
    except (ValueError, KeyError, TypeError, IndexError, OSError, OverflowError, ZeroDivisionError) as error:
        report = {"schema": "temporal.motion.numeric-evidence.v1", "numericProbesPassed": False,
                  "routeCoverageValidated": False, "acceptanceStatus": "rejected", "error": str(error)}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    # 2 is explicitly INCOMPLETE, never an all-route acceptance success.
    return 2 if report["numericProbesPassed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
