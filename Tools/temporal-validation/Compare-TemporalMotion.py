#!/usr/bin/env python3
"""Independent analytic motion and GPU ownership oracle. UNEXECUTED source.

This compares real capture bytes with an axis-aligned pinhole/orthographic
oracle. It does not import the engine's motion, matrix or shader helpers.
Legacy numeric agreement alone never proves route ownership. The v2 path also
requires exact submitted input identities and isolated GPU negative controls.
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
    if not matching:
        raise MissingProof(f"missing {name} attachment")
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


def compare_legacy(capture, fixture_path):
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
    require(metadata["source"] == "final-production-temporal-inputs" and metadata["schemaVersion"] == 2,
            "missing final production motion source")
    require(metadata["historyReset"] is False and metadata["realFrameId"] == manifest["realFrameId"] and
            metadata["historyGeneration"] > 0, "reset/stale/unidentified motion frame")
    previous_frame = metadata["previousSubmittedRealFrameId"]
    require(type(previous_frame) is int and 0 < previous_frame < manifest["realFrameId"],
            "missing or invalid previous submitted motion frame")
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
            "previousSubmittedRealFrameId": previous_frame,
            "captureManifestSha256": hashlib.sha256(manifest_bytes).hexdigest(),
            "motionSha256": motion_hash, "fixtureSha256": hashlib.sha256(fixture_bytes).hexdigest(),
            "numericProbesPassed": all(probe["passed"] for probe in probes), "probes": probes,
            "routeCoverageValidated": False, "acceptanceStatus": "incomplete-route-fixture",
            "remainingRequirement": "Execute a production all-route scene driver and independently verify each probe's route ownership"}


class MissingProof(ValueError):
    """Missing evidence is INCOMPLETE, never an inferred route success."""


ROUTES = {"static", "skinned", "instanced", "meshlet", "alpha", "decal", "sprite"}
PRODUCT_SCHEMA = "temporal.motion.executed-fixture.v2"


def artifact(base, reference):
    path = (base / reference["path"]).resolve()
    payload = path.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    require(reference["sha256"] == digest, f"input artifact hash mismatch: {path.name}")
    return payload, digest


def positive_integer(value):
    return type(value) is int and value > 0


def load_sprite_stage(root, manifest, extent):
    # Resolve a named graph association, not a guessed filename or final HDR.
    stages = manifest["diagnosticStages"]
    def sprite_name(name):
        require(isinstance(name, str), "invalid diagnostic stage name")
        parts = name.split("-", 2)
        return len(parts) == 3 and parts[0] == "hdr" and parts[1].isdigit() and parts[2] == "Sprite"
    matches = [stage for stage in stages if sprite_name(stage["name"])]
    if not matches:
        raise MissingProof("missing immediate post-Sprite diagnostic association")
    require(len(matches) == 1, "ambiguous post-Sprite diagnostic association")
    stage = matches[0]
    owners = [entry for entry in stages if entry["name"] == stage["readbackStage"]]
    if not owners:
        raise MissingProof("missing Sprite readback owner")
    require(len(owners) == 1, "missing or ambiguous Sprite readback owner")
    owner = owners[0]
    owner_name = owner["name"].split("-", 2)
    require(len(owner_name) == 3 and owner_name[0] == "hdr" and owner_name[1].isdigit() and
            int(owner_name[1]) <= int(stage["name"].split("-", 2)[1]),
            "Sprite ownership cannot use a later postprocessing stage")
    require(owner["sharedReadback"] is False and owner["readbackStage"] == owner["name"] and
            stage["sharedReadback"] is (stage["name"] != owner["name"]),
            "invalid Sprite shared readback association")
    for field in ("file", "resource", "version", "kind", "graphEpoch", "encoding", "width", "height", "channels", "nonfinite"):
        require(stage[field] == owner[field], "Sprite association is not the same captured graph resource")
    require(stage["file"] == owner["name"] + ".f32" and
            stage["encoding"] == "float32-le-row-major" and stage["channels"] == 4 and
            [stage["width"], stage["height"]] == extent and stage["nonfinite"] == 0,
            "invalid post-Sprite image contract")
    for field in ("resource", "version", "kind", "graphEpoch"):
        require(type(stage[field]) is int and stage[field] >= 0, "invalid Sprite graph identity")
    path = (root / stage["file"]).resolve()
    require(path.parent == root, "Sprite readback escapes capture directory")
    payload = path.read_bytes()
    require(len(payload) == extent[0] * extent[1] * 16, "wrong post-Sprite byte count")
    pixels = [v[0] for v in struct.iter_unpack("<f", payload)]
    require(all(math.isfinite(v) for v in pixels), "nonfinite post-Sprite bytes")
    return pixels, hashlib.sha256(payload).hexdigest()


def capture_frame(base, reference, extent, sprite=False):
    root = (base / reference["path"]).resolve()
    payload = (root / "manifest.json").read_bytes()
    require(hashlib.sha256(payload).hexdigest() == reference["manifestSha256"],
            "capture manifest hash mismatch")
    m = json.loads(payload)
    require(m["source"] == "product-live" and m["captureMode"] == "temporal-motion-v1",
            "requires executed product motion capture")
    require(m["motionEvidence"] == "exact-tagged-fixture" and m["fixtureBindingRequired"] is True,
            "unbound motion observation cannot establish fixture ownership")
    require(m["temporalProvenanceSchemaVersion"] == 2 and m["frameKind"] == "real" and
            m["generatedOrdinal"] == 0 and m["temporalNativeGateActive"] is True,
            "requires schema-2 native real-frame provenance")
    require(m["upscaler"] == "none" and m["frameGenerator"] == "none" and
            m["spatialMode"] == "off" and m["deepDvcApplied"] is False and
            m["validationCount"] == 0 and m["resolutionState"] == "native",
            "capture is not a clean native submission")
    require([m["renderWidth"], m["renderHeight"]] == extent and
            [m["displayWidth"], m["displayHeight"]] == extent,
            "fixture/capture extent mismatch")
    t = m["temporalMotion"]
    require(t["schemaVersion"] == 2 and t["source"] == "final-production-temporal-inputs" and
            t["historyReset"] is False and positive_integer(t["historyGeneration"]) and
            t["realFrameId"] == m["realFrameId"] and positive_integer(m["realFrameId"]) and
            positive_integer(t["previousSubmittedRealFrameId"]) and
            t["previousSubmittedRealFrameId"] < m["realFrameId"],
            "missing exact submitted previous/current real-frame seam")
    require(t["direction"] == "current-to-previous" and t["units"] == "render-pixels" and
            t["origin"] == "top-left" and t["jitterIncluded"] is False and
            [t["renderWidth"], t["renderHeight"]] == extent,
            "motion convention mismatch")
    images, hashes = {}, {}
    for name, channels in (("baseColor", 4), ("preToneHdr", 4), ("depth", 1),
                           ("temporalDepth", 1), ("temporalMotionRG", 2),
                           ("temporalResponsive", 1)):
        images[name], hashes[name] = load_attachment(root, m, name, channels, extent)
    if sprite:
        images["postSpriteHdr"], hashes["postSpriteHdr"] = load_sprite_stage(root, m, extent)
    return m, images, hashes


def submission_proof(manifest, case, step, predecessor):
    s, t = manifest["fixtureSubmission"], manifest["temporalMotion"]
    require(s["valid"] is True and s["sourceVerified"] is True and
            t["previousSubmittedFixtureSourceVerified"] is True and s["submissionProof"] ==
            "native-submit-succeeded-and-temporal-history-committed",
            "fixture has no successful native submission ACK")
    require(s["historyReset"] is False and s["coalesced"] is False and
            manifest["fixtureCoalesced"] is False, "reset/coalesced fixture transition")
    require(positive_integer(case["sessionId"]) and positive_integer(step) and
            positive_integer(predecessor) and step != predecessor, "invalid fixture session/steps")
    for value in (s["sessionId"], t["fixtureSessionId"], manifest["fixtureSessionId"],
                  t["previousSubmittedFixtureSessionId"]):
        require(value == case["sessionId"], "mixed fixture session")
    require(s["stepId"] == t["fixtureStepId"] == manifest["fixtureStepId"] == step and
            s["requestedPredecessorStepId"] == s["predecessorStepId"] ==
            t["fixturePredecessorStepId"] == t["previousSubmittedFixtureStepId"] ==
            manifest["fixturePredecessorStepId"] == predecessor,
            "capture did not submit the requested exact pose transition")
    require(s["realFrameId"] == manifest["realFrameId"] and
            s["sourceFrameId"] == manifest["frameId"] and
            s["predecessorRealFrameId"] == t["previousSubmittedRealFrameId"] and
            s["predecessorSourceFrameId"] == t["previousSubmittedSourceFrameId"] and
            positive_integer(s["predecessorSourceFrameId"]) and
            s["sourceFrameId"] > s["predecessorSourceFrameId"],
            "submission ACK disagrees with actual temporal predecessor")
    for field in ("viewId", "sceneEpoch"):
        require(s[field] == manifest[field] == t["previousSubmitted" + field[0].upper() + field[1:]] and
                positive_integer(s[field]), "fixture view/scene changed")
    require(s["historyGeneration"] == t["historyGeneration"] == t["previousSubmittedHistoryGeneration"] and
            s["historyRevision"] == manifest["historyRevision"] == t["previousSubmittedHistoryRevision"],
            "fixture history discontinuity")
    digest = case["input"]["sha256"]
    require(s["inputSha256"] == s["predecessorInputSha256"] == s["requestedPredecessorInputSha256"] ==
            manifest["fixtureInputSha256"] == manifest["fixturePredecessorInputSha256"] ==
            t["fixtureInputSha256"] == t["fixturePredecessorInputSha256"] ==
            t["previousSubmittedFixtureInputSha256"] == digest,
            "submitted pose is not bound to the authored input bytes")
    allow_meshlets = case["route"] == "meshlet"
    for flag in (case["allowMeshlets"], manifest["fixtureAllowMeshlets"], t["fixtureAllowMeshlets"],
                 t["previousSubmittedFixtureAllowMeshlets"], s["allowMeshlets"]):
        require(flag is allow_meshlets, "sealed meshlet route choice differs from authored case")


def posed_vertices(case, endpoint, instance):
    pose = case[endpoint]
    require(set(pose) <= {"worldTranslation", "boneTranslations", "instanceTranslations"},
            "unsupported authored transform; do not silently ignore rotations or scales")
    shift = vector(pose.get("worldTranslation", [0, 0, 0]))
    instances = pose.get("instanceTranslations", [[0, 0, 0]])
    offset = vector(instances[instance])
    bones = [vector(bone) for bone in pose.get("boneTranslations", [])]
    output = []
    for vertex in case["geometry"]["vertices"]:
        point = vector(vertex["position"])
        if bones:
            weights = vector(vertex["weights"], len(bones))
            require(all(w >= 0 for w in weights) and abs(sum(weights) - 1) < 1e-7,
                    "invalid authored skin weights")
            point = [point[a] + sum(w * b[a] for w, b in zip(weights, bones)) for a in range(3)]
        output.append([point[a] + shift[a] + offset[a] for a in range(3)])
    return output


def barycentric(point, triangle):
    a, b, c = triangle
    denominator = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
    require(abs(denominator) > 1e-9, "degenerate authored triangle")
    u = ((b[1] - c[1]) * (point[0] - c[0]) + (c[0] - b[0]) * (point[1] - c[1])) / denominator
    v = ((c[1] - a[1]) * (point[0] - c[0]) + (a[0] - c[0]) * (point[1] - c[1])) / denominator
    return [u, v, 1 - u - v]


def weighted(values, weights):
    return [sum(w * value[a] for w, value in zip(weights, values)) for a in range(len(values[0]))]


def geometry_probes(case, camera, extent):
    # Pixel selection comes only from authored triangles, never GPU motion/color.
    # Orthographic affine interpolation is exact; unsupported projection stays open.
    require(camera["projection"] == "orthographic", "product fixture requires orthographic camera")
    require(camera["depthConvention"] == "forward-z", "unsupported fixture depth convention")
    near, far = float(camera["near"]), float(camera["far"])
    require(math.isfinite(near) and math.isfinite(far) and 0 < near < far,
            "invalid camera depth interval")
    require(float(camera["width"]) > 0 and float(camera["height"]) > 0, "invalid orthographic size")
    indices = case["geometry"]["indices"]
    count = len(case["geometry"]["vertices"])
    require(indices and len(indices) % 3 == 0 and
            all(type(i) is int and 0 <= i < count for i in indices), "invalid triangle indices")
    instance_count = len(case["current"].get("instanceTranslations", [[0, 0, 0]]))
    require(instance_count == len(case["previous"].get("instanceTranslations", [[0, 0, 0]])) and
            instance_count > 0, "instance correspondence changed")
    if case["route"] == "instanced":
        require(instance_count >= 2, "instanced proof needs independent instance correspondence")
    if case["route"] == "skinned":
        require(len(case["current"]["boneTranslations"]) >= 2 and
                len(case["current"]["boneTranslations"]) == len(case["previous"]["boneTranslations"]),
                "skinned proof needs multiple independently weighted bones")
        require(len({tuple(v["weights"]) for v in case["geometry"]["vertices"]}) >= 2,
                "uniform skin weights cannot distinguish weighted deformation")
    result, seen = [], set()
    for instance in range(instance_count):
        current = posed_vertices(case, "current", instance)
        previous = posed_vertices(case, "previous", instance)
        for start in range(0, len(indices), 3):
            tri = indices[start:start + 3]
            screen = [project(current[i], camera, extent) for i in tri]
            seeds = ((.6, .2, .2), (.2, .6, .2), (.2, .2, .6))
            if case["route"] == "alpha":
                seeds = [(u / 10, v / 10, 1 - (u + v) / 10)
                         for u in range(2, 7) for v in range(2, 9 - u)]
            for seed in seeds:
                sample = weighted(screen, seed)
                x, y = math.floor(sample[0]), math.floor(sample[1])
                if (x, y) in seen:
                    continue
                require(0 <= x < extent[0] and 0 <= y < extent[1], "fixture geometry is offscreen")
                center = [x + .5, y + .5]
                weights = barycentric(center, screen)
                require(min(weights) >= .1, "fixture footprint is too small for interior probes")
                point = weighted([current[i] for i in tri], weights)
                previous_point = weighted([previous[i] for i in tri], weights)
                old = project(previous_point, camera, extent)
                depth = (point[2] - vector(camera["position"])[2] - near) / (far - near)
                require(0 < depth < 1, "fixture geometry outside depth range")
                motion = [old[a] - center[a] for a in range(2)]
                require(max(abs(v) for v in motion) >= 1, "motion does not distinguish camera-only zero fallback")
                result.append({"pixel": [x, y], "instance": instance, "expectedPixels": motion,
                               "expectedDepth": depth, "triangle": tri, "weights": weights})
                if case["route"] == "alpha":
                    uvs = [vector(case["geometry"]["vertices"][i]["uv"], 2) for i in tri]
                    result[-1]["uv"] = weighted(uvs, weights)
                    # Derivatives for fixed-function point/minification selection.
                    result[-1]["uvDx"] = weighted(uvs, barycentric([x + 1.5, y + .5], screen))
                    result[-1]["uvDy"] = weighted(uvs, barycentric([x + .5, y + 1.5], screen))
                seen.add((x, y))
    require(len(result) >= 3 * instance_count, "insufficient independent geometry probes")
    return result


def alpha_coverage(base, case, probes, assets):
    reference = case["ownership"]["alphaTexture"]
    require(reference in assets, "alpha texture is not one of the hashed mounted inputs")
    payload, _ = artifact(base, reference)
    require(len(payload) >= 104 and payload[:4] == b"CECT", "unsupported alpha texture artifact")
    header = struct.unpack_from("<12I2Q", payload)
    _, schema, header_size, representation, fmt, flags, width, height, mips, arrays, count, entry_size, offset, total = header
    require(schema == 2 and header_size == 64 and representation == 2 and fmt == 2 and
            flags & 2 and arrays == 1 and count == mips and entry_size == 40 and
            0 < width <= 16384 and 0 < height <= 16384 and 0 < mips <= 15 and
            offset == 64 + count * 40 and total == len(payload), "invalid alpha texture header")
    cursor = offset
    first = None
    for mip in range(mips):
        w, h, row_pitch, slice_pitch, start, size = struct.unpack_from("<II4Q", payload, 64 + mip * 40)
        require(w == max(1, width >> mip) and h == max(1, height >> mip) and
                row_pitch == w * 4 and slice_pitch == size == row_pitch * h and
                start == cursor and start + size <= len(payload), "invalid alpha mip payload")
        if mip == 0:
            first = payload[start:start + size]
        cursor += size
    require(cursor == len(payload), "trailing alpha texture bytes")
    covered = set()
    for probe in probes:
        uv = probe["uv"]
        for derivative in (probe["uvDx"], probe["uvDy"]):
            footprint = math.hypot((derivative[0] - uv[0]) * width,
                                   (derivative[1] - uv[1]) * height)
            if footprint >= 1:
                raise MissingProof("alpha mip-zero ownership needs magnified point-sampled fixture")
        tx, ty = (uv[0] % 1) * width, (uv[1] % 1) * height
        # Avoid point-sampler texel ties; GPU rounding must not choose the proof.
        if min(tx % 1, 1 - tx % 1, ty % 1, 1 - ty % 1) < .05:
            probe["skipCoverageBoundary"] = True
            continue
        texel = first[4 * (math.floor(ty) * width + math.floor(tx)):
                      4 * (math.floor(ty) * width + math.floor(tx)) + 4]
        require(list(texel[:3]) == [255, 255, 255] and texel[3] in (0, 255),
                "fixture alpha proof requires binary white source texels")
        probe["covered"] = texel[3] == 255
        covered.add(probe["covered"])
    require(covered == {False, True}, "alpha case needs independently located opaque and hole probes")
    return [p for p in probes if not p.get("skipCoverageBoundary", False)]


def ownership_result(case, probes, positive, control, extent):
    rule = case["ownership"]
    attachment = "postSpriteHdr" if case["route"] == "sprite" else "baseColor"
    require(rule["attachment"] == attachment, "wrong route ownership attachment")
    color = vector(rule["expectedColor"])
    tolerance, contrast = float(rule["colorTolerance"]), float(rule["minimumContrast"])
    require(0 < tolerance <= .05 and math.isfinite(tolerance) and
            .05 <= contrast <= 1 and math.isfinite(contrast), "unbounded ownership tolerance")
    results = []
    for probe in probes:
        x, y = probe["pixel"]
        offset = y * extent[0] + x
        observed = positive["temporalMotionRG"][2 * offset:2 * offset + 2]
        disabled = control["temporalMotionRG"][2 * offset:2 * offset + 2]
        rgb = positive[attachment][4 * offset:4 * offset + 3]
        absent = control[attachment][4 * offset:4 * offset + 3]
        errors = [abs(observed[a] - probe["expectedPixels"][a]) for a in range(2)]
        if probe.get("covered") is False:
            same_color = max(abs(rgb[a] - absent[a]) for a in range(3)) <= .01
            same_depth = abs(positive["depth"][offset] - control["depth"][offset]) <= 2e-5
            same_depth = same_depth and abs(positive["temporalDepth"][offset] -
                                           control["temporalDepth"][offset]) <= 2e-5
            same_motion = max(abs(observed[a] - disabled[a]) for a in range(2)) <= .125
            results.append(dict(probe, observedPixels=observed, observedColor=rgb, controlColor=absent,
                                passed=same_color and same_depth and same_motion))
            continue
        # Decal writes receiver depth; sprite writes temporal depth, not GBuffer depth.
        depth_target = "temporalDepth" if case["route"] == "sprite" else "depth"
        depth_error = abs(positive[depth_target][offset] - probe["expectedDepth"])
        color_error = max(abs(rgb[a] - color[a]) for a in range(3))
        color_contrast = max(abs(rgb[a] - absent[a]) for a in range(3))
        disappeared = max(abs(absent[a] - color[a]) for a in range(3)) > contrast
        depth_disappeared = abs(control[depth_target][offset] - probe["expectedDepth"]) > 1e-4
        # Receiver remains in the decal negative control, unlike mesh/sprite surface.
        temporal_depth_error = abs(positive["temporalDepth"][offset] - probe["expectedDepth"])
        if case["route"] == "decal":
            control_depth_valid = abs(control["depth"][offset] - probe["expectedDepth"]) <= 2e-5
        else:
            control_depth_valid = depth_disappeared
        depth_valid = depth_error <= 2e-5 and temporal_depth_error <= 2e-5 and control_depth_valid
        if case["route"] == "sprite":
            depth_valid = depth_valid and abs(positive["depth"][offset] - control["depth"][offset]) <= 2e-5
        passed = (max(errors) <= .125 and max(abs(v) for v in disabled) <= .125 and
                  abs(positive["temporalResponsive"][offset]) <= .001 and
                  color_error <= tolerance and color_contrast >= contrast and disappeared and depth_valid)
        results.append(dict(probe, observedPixels=observed, absoluteErrors=errors,
                            observedColor=rgb, controlColor=absent, colorError=color_error,
                            colorContrast=color_contrast, depthError=depth_error,
                            controlMotion=disabled, passed=passed))
    return results


def camera_proof(manifest, camera):
    eye = vector(camera["position"])
    width, height = float(camera["width"]), float(camera["height"])
    near, far = float(camera["near"]), float(camera["far"])
    require(width > 0 and height > 0 and 0 < near < far, "invalid camera")
    view = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -eye[0], -eye[1], -eye[2], 1]
    projection = [2 / width, 0, 0, 0, 0, 2 / height, 0, 0,
                  0, 0, 1 / (far - near), 0, 0, 0, -near / (far - near), 1]
    for name, expected in (("view", view), ("projection", projection)):
        observed = vector(manifest["camera"][name], 16)
        require(max(abs(a - b) for a, b in zip(observed, expected)) <= 1e-5,
                "captured camera differs from independent authored projection")
    for field in ("jitterX", "jitterY", "previousJitterX", "previousJitterY"):
        require(abs(float(manifest["temporalMotion"][field])) <= 1e-8,
                "isolated native fixture must not contain projection jitter")


def geometry_route_proof(current, control, case):
    for manifest in (current, control):
        routes = manifest["geometryRoutes"]
        require(routes["source"] == "joined-command-recording-after-native-submission" and
                routes["pass"] == "LX.Scene.GBuffer" and
                routes["sourceFrameId"] == manifest["frameId"] and
                routes["viewId"] == manifest["viewId"] and
                routes["sceneEpoch"] == manifest["sceneEpoch"],
                "geometry command evidence belongs to another source frame")
    selected = current["geometryRoutes"]["selected"]
    absent = control["geometryRoutes"]["selected"]
    route = case["route"]
    if route == "sprite":
        require(not selected and not absent, "sprite fixture must not substitute GBuffer geometry")
    elif route == "decal":
        key = lambda entry: (entry["geometryKey"], entry["temporalObjectId"], entry["temporalInstanceId"])
        require(selected and {key(e) for e in selected} == {key(e) for e in absent},
                "decal-disabled control must preserve the exact receiver geometry")
    else:
        require(selected and not absent, "isolated route must be recorded and disappear in control")
    if route == "instanced":
        identities = {(e["temporalObjectId"], e["temporalInstanceId"]) for e in selected}
        require(all(positive_integer(instance) for _, instance in identities) and
                len(identities) == len(case["current"]["instanceTranslations"]),
                "independent instance identities were collapsed or lost")
    if route == "meshlet":
        audit = current["geometryRoutes"]
        if audit["recordedMeshletDispatchCount"] == 0:
            raise MissingProof("actual meshlet dispatch unavailable; raster fallback cannot cover meshlets")
        require(audit["emptyScene"] is False and audit["recordedMeshletBatchCount"] > 0 and
                control["geometryRoutes"]["recordedMeshletDispatchCount"] == 0 and
                all(e["meshShader"] is True and e["meshletCount"] > 0 for e in selected),
                "meshlet selection does not agree with isolated dispatch/control")
    else:
        require(not any(e["meshShader"] is True for e in selected + absent),
                "indexed fixture unexpectedly used a mesh-shader route")


def compare_product(capture, fixture_path, fixture):
    base = fixture_path.resolve().parent
    report = {"schema": "temporal.motion.route-evidence.v2", "numericProbesPassed": False,
              "routeCoverageValidated": False, "acceptanceStatus": "INCOMPLETE", "cases": [],
              "fixtureSha256": hashlib.sha256(fixture_path.read_bytes()).hexdigest()}
    try:
        extent = fixture["extent"]
        require(len(extent) == 2 and all(positive_integer(v) for v in extent), "invalid extent")
        assets = fixture["inputAssets"]
        if not assets:
            raise MissingProof("no source asset byte hashes")
        report["inputHashes"] = [artifact(base, a)[1] for a in assets]
        routes = [case["route"] for case in fixture["cases"]]
        require(len(routes) == len(set(routes)) and set(routes) <= ROUTES, "duplicate/unknown route")
        if set(routes) != ROUTES:
            raise MissingProof("all seven independently owned routes are required")
        signatures = []
        for case in fixture["cases"]:
            input_bytes, _ = artifact(base, case["input"])
            authored = json.loads(input_bytes)
            for field in ("route", "allowMeshlets", "sessionId", "baselineStepId", "currentStepId", "controlStepId",
                          "geometry", "previous", "current", "ownership"):
                require(authored[field] == case[field], f"source input differs from fixture {field}")
            for field in ("camera", "extent", "inputAssets"):
                require(authored[field] == fixture[field], f"source input differs from fixture {field}")
            sprite = case["route"] == "sprite"
            current, pixels, hashes = capture_frame(base, case["captures"]["current"], extent, sprite)
            control, disabled, control_hashes = capture_frame(base, case["captures"]["control"], extent, sprite)
            camera_proof(current, fixture["camera"])
            camera_proof(control, fixture["camera"])
            submission_proof(current, case, case["currentStepId"], case["baselineStepId"])
            submission_proof(control, case, case["controlStepId"], case["currentStepId"])
            require(control["realFrameId"] > current["realFrameId"] and
                    control["viewId"] == current["viewId"] and control["sceneEpoch"] == current["sceneEpoch"],
                    "control is not a later submission in the same fixture view")
            probes = geometry_probes(case, fixture["camera"], extent)
            if case["route"] == "alpha":
                probes = alpha_coverage(base, case, probes, assets)
            geometry_route_proof(current, control, case)
            signature = tuple(round(v, 3) for v in probes[0]["expectedPixels"])
            require(signature not in signatures, "route vectors are not independently distinguishable")
            signatures.append(signature)
            results = ownership_result(case, probes, pixels, disabled, extent)
            if case["route"] in ("skinned", "instanced"):
                vectors = [p["expectedPixels"] for p in probes]
                require(any(max(abs(v[a] - vectors[0][a]) for a in range(2)) > .5 for v in vectors[1:]),
                        "weighted/instance motion collapses to a rigid fallback")
            report["cases"].append({"route": case["route"], "realFrameId": current["realFrameId"],
                "previousSubmittedRealFrameId": current["temporalMotion"]["previousSubmittedRealFrameId"],
                "inputSha256": case["input"]["sha256"], "attachmentHashes": hashes,
                "controlAttachmentHashes": control_hashes, "probes": results,
                "passed": all(p["passed"] for p in results)})
        passed = all(c["passed"] for c in report["cases"])
        report.update(numericProbesPassed=passed, routeCoverageValidated=passed,
                      acceptanceStatus="PASS" if passed else "FAIL")
    except (MissingProof, KeyError, OSError) as error:
        report["missingProof"] = str(error)
    except (ValueError, TypeError, IndexError, OverflowError, ZeroDivisionError, struct.error) as error:
        report.update(acceptanceStatus="FAIL", error=str(error))
    return report


def compare(capture, fixture_path):
    fixture = json.loads(fixture_path.read_bytes())
    if fixture.get("schema") == PRODUCT_SCHEMA:
        return compare_product(capture, fixture_path, fixture)
    return compare_legacy(capture, fixture_path)


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
    if report["routeCoverageValidated"] and report["acceptanceStatus"] == "PASS":
        return 0
    # Old analytic JSON can never reach the full-route exit-0 acceptance branch.
    return 2 if report["numericProbesPassed"] or report["acceptanceStatus"] == "INCOMPLETE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
