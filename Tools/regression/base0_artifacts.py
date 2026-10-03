"""BASE-0 artifact gate. Standard-library only; reads actual product captures.

Legacy reachability is audited separately from execution order. This does not
invent versions or claim that the current graph is a dependency scheduler.
"""
import argparse
import array
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     allow_nan=False).encode()).hexdigest()


def truth(value):
    require(value in (True, False, 0, 1, "true", "false", "0", "1"), "invalid boolean")
    return value in (True, 1, "true", "1")


def audit_graph(graph):
    require(graph["schemaVersion"] == 1, "graph schema")
    require(graph["orderContract"] == "legacy-declaration-order", "unsupported graph contract")
    require(graph["accessContract"] == "inferred-from-state" and
            not truth(graph["versionsSupported"]), "legacy version contract")
    passes, resources, order = graph["passes"], graph["resources"], graph["executeOrder"]
    require(passes and resources and order, "empty compiled graph")
    require([p["authoredIndex"] for p in passes] == list(range(len(passes))), "pass IDs")
    require([r["id"] for r in resources] == list(range(len(resources))), "resource IDs")
    expected_order = [i for i, p in enumerate(passes) if not truth(p["culled"])]
    require(order == expected_order, "compiled order disagrees with legacy contract")
    expected_edges = []
    for consumer, p in enumerate(passes):
        require(p["compiledIndex"] == (order.index(consumer) if consumer in order else -1),
                "compiled index")
        for u in p["usages"]:
            require(0 <= u["resource"] < len(resources), "out-of-range usage")
            require(truth(u["inferredWrite"]) == (u["state"] in (1, 2, 7, 9)),
                    "state/access inference mismatch")
            if not truth(u["inferredWrite"]):
                for producer, writer in enumerate(passes):
                    for w in writer["usages"]:
                        if truth(w["inferredWrite"]) and w["resource"] == u["resource"]:
                            expected_edges.append(dict(producer=producer, consumer=consumer,
                                                       resource=u["resource"]))
    require(graph["reachabilityEdges"] == expected_edges, "missing/extra reachability edge")
    live = {i for i, p in enumerate(passes) if truth(p["sideEffect"]) or
            any(truth(u["inferredWrite"]) and truth(resources[u["resource"]]["imported"])
                for u in p["usages"])}
    while True:
        expanded = live | {e["producer"] for e in expected_edges if e["consumer"] in live}
        if expanded == live:
            break
        live = expanded
    require(sorted(live) == order, "culling/reachability mismatch")
    uses = [[] for _ in resources]
    written = set()
    for position, index in enumerate(order):
        p = passes[index]
        for u in p["usages"]:
            r = u["resource"]
            require(truth(u["inferredWrite"]) or truth(resources[r]["imported"]) or r in written,
                    "transient read before producer")
            if truth(u["inferredWrite"]):
                written.add(r)
            uses[r].append(position)
        for b in p["barriers"]:
            require(0 <= b["resource"] < len(resources), "barrier resource")
            require(any(u["resource"] == b["resource"] for u in p["usages"]), "orphan barrier")
            if truth(b["uav"]):
                require(b["before"] == b["after"] == 7, "UAV barrier state")
    for r, positions in zip(resources, uses):
        require(truth(r["used"]) == bool(positions), "resource used flag")
        if positions:
            require((r["firstUse"], r["lastUse"]) == (min(positions), max(positions)), "lifetime")
    # Pool-dependent initial states/barriers are audited, but not a topology identity.
    topology = copy.deepcopy(graph)
    for p in topology["passes"]:
        p.pop("barriers")
    return digest(topology)


def input_identity(m):
    draws = []
    for d in m["draws"]:
        require("missing" not in d and d["modelGeneration"] > 0, "missing draw identity")
        entry = {k: d[k] for k in ("route", "modelId", "meshId", "modelGeneration", "world")}
        if d["route"] == "lattice":
            # The render-owner material slot is process-local; typed contents and
            # source/model generations are the replay identity.
            entry["lattice"] = {k: v for k, v in d["lattice"].items() if k != "slot"}
            entry["pose"] = d["pose"]
        else:
            require(truth(d["seal"]["stamped"]), "unstamped material")
            entry.update({k: d[k] for k in ("propertyBytes", "permutation", "coverageFlags",
                                           "alphaCutoff", "useNormalMap")})
            entry["authoredDigest"] = d["seal"]["authoredDigest"]
            entry["textures"] = [{k: t[k] for k in ("property", "assetId", "register", "space", "authored")}
                                 for t in d["textures"]]
        draws.append(entry)
    require(draws, "empty representative scene")
    return digest({k: m[k] for k in ("camera", "lights", "skyBoxEnabled", "viewFlags",
                                    "width", "height")} | {"draws": draws})


def logical_backend_graph(graph, backend):
    """Project only proven transport/ring differences after the full graph audit."""
    g = copy.deepcopy(graph)
    resources, passes = g["resources"], g["passes"]
    shared = [r for r in resources if r["name"] == "Live.Shared"]
    require(backend in ("dx12", "vulkan"), "unknown backend graph")
    require(len(shared) == (1 if backend == "dx12" else 0), "backend transport shape")
    if shared:
        r = shared[0]
        uses = [(i, u) for i, p in enumerate(passes) for u in p["usages"] if u["resource"] == r["id"]]
        require(r["id"] == len(resources) - 1 and truth(r["imported"]) and
                r["kind"] == "texture" and truth(r["used"]) and len(uses) == 1,
                "shared transport must be an imported leaf")
        i, u = uses[0]
        require(passes[i]["name"] == "live_present" and truth(passes[i]["sideEffect"]) and
                u["state"] == 9 and truth(u["inferredWrite"]) and
                r["firstUse"] == r["lastUse"] == passes[i]["compiledIndex"] and
                not any(e["resource"] == r["id"] for e in g["reachabilityEdges"]),
                "shared transport has unexpected consumers/state")
        passes[i]["usages"].remove(u)
        resources.remove(r)
    resolve = [p for p in passes if p["name"] == "SSGI.Resolve"]
    store = [p for p in passes if p["name"] == "SSGI.StoreHistory"]
    require(len(resolve) == len(store) == 1, "SSGI history role passes")
    remap = {r["id"]: r["id"] for r in resources}
    for prefix in ("SSGI.History", "SSGI.HistoryDepth"):
        pair = [r for r in resources if r["name"] in (prefix + "0", prefix + "1")]
        require(len(pair) == 2 and all(truth(r["imported"]) for r in pair), "SSGI ring shape")
        ids = {r["id"] for r in pair}
        reads = [u for u in resolve[0]["usages"] if u["resource"] in ids]
        writes = [u for u in store[0]["usages"] if u["resource"] in ids]
        require(len(reads) == len(writes) == 1 and reads[0]["state"] == 4 and
                not truth(reads[0]["inferredWrite"]) and writes[0]["state"] == 9 and
                truth(writes[0]["inferredWrite"]) and reads[0]["resource"] != writes[0]["resource"],
                "SSGI history read/write role mismatch")
        previous, current = reads[0]["resource"], writes[0]["resource"]
        remap[previous], remap[current] = min(ids), max(ids)
        for r in pair:
            r["name"] = prefix + (".Previous" if r["id"] == previous else ".Current")
    for p in passes:
        for u in p["usages"]:
            u["resource"] = remap[u["resource"]]
        p["barriers"] = []  # Already audited on the original backend graph.
    for e in g["reachabilityEdges"]:
        e["resource"] = remap[e["resource"]]
    for r in resources:
        r["id"] = remap[r["id"]]
    resources.sort(key=lambda r: r["id"])
    return audit_graph(g)


def audit_manifest(m):
    require(m["source"] == "product-live" and m["captureMode"] == "static-repeatability-v1",
            "not a controlled product capture")
    require(m["historyPolicy"] == "restart-ssgi-fog" and m["totalSeconds"] == m["deltaSeconds"] == 0
            and m["sampleIndex"] == 0, "uncontrolled render clock/history")
    require(m["frameKind"] == "real", "generated/unknown frame")
    require(m["width"] == m["renderWidth"] == m["displayWidth"] > 0 and
            m["height"] == m["renderHeight"] == m["displayHeight"] > 0, "resolution provenance")
    require(m["validationCount"] == 0 and truth(m["finite"]), "validation/nonfinite failure")
    require(truth(m["sealLedger"]["recorded"]), "missing binding ledger")
    require(m["ibl"] == dict(baseSamples=1024, reflectionSamples=4096,
                             proposalWidth=5120, proposalHeight=2), "IBL quality contract")
    require(m["graph"]["declared"] == len(m["compiledGraph"]["passes"]) and
            m["graph"]["executed"] == len(m["compiledGraph"]["executeOrder"]) and
            m["graph"]["barriers"] == sum(len(p["barriers"]) for p in m["compiledGraph"]["passes"]),
            "graph statistics mismatch")
    measure = m["measurement"]
    require(math.isfinite(measure["cpuGraphCompileMs"]) and measure["cpuGraphCompileMs"] >= 0,
            "compile cost")
    require(truth(measure["memory"]["available"]) and measure["memory"]["budgetMB"] > 0, "VRAM unavailable")
    if m["backend"] == "vulkan":
        require(truth(measure["validationLayerEnabled"]), "Vulkan validation disabled")
    require(math.isfinite(measure["cpuRecordMs"]) and measure["cpuRecordMs"] >= 0, "CPU cost")
    require(measure["scope"] == "capture-submission-including-readbacks", "timing scope")
    if measure["gpuStatus"] == "measured":
        require(measure["queryOverflow"] == measure["droppedSlices"] == 0 and measure["passes"],
                "incomplete GPU query coverage")
        require(measure["sliceCount"] >= m["graph"]["executed"], "GPU pass coverage")
        for p in measure["passes"]:
            require(math.isfinite(p["milliseconds"]) and p["milliseconds"] >= 0, "GPU time")
    else:
        raise ValueError("GPU measurement failed/unavailable")
    return dict(inputHash=input_identity(m), graphHash=audit_graph(m["compiledGraph"]))


def png(path, width, height, rgb):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def load_capture(directory):
    m = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
    identity = audit_manifest(m)
    images = {}
    expected = {"baseColor", "metalRough", "normal", "emissive", "depth", "preToneHdr", "display"}
    for a in m["attachments"]:
        require(a["name"] in expected and a["name"] not in images, "attachment set")
        require(a["width"] == m["width"] and a["height"] == m["height"] and a["nonfinite"] == 0,
                "attachment extent/finite")
        require(a["channels"] == (1 if a["name"] == "depth" else 4), "attachment channels")
        path = directory / a["file"]
        require(path.resolve().parent == directory.resolve(), "attachment escaped capture directory")
        values = array.array("f", path.read_bytes())
        if sys.byteorder != "little":
            values.byteswap()
        require(len(values) == a["width"] * a["height"] * a["channels"] and
                all(math.isfinite(v) for v in values), "attachment bytes/nonfinite")
        images[a["name"]] = values
    require(set(images) == expected, "missing attachment")
    for a in m.get("diagnosticStages", []):
        name = a["name"]
        require(name.startswith("hdr-") and name not in images, "diagnostic stage identity")
        require(a["width"] == m["width"] and a["height"] == m["height"] and
                a["channels"] == 4 and a["nonfinite"] == 0 and
                a["encoding"] == "float32-le-row-major", "diagnostic stage layout")
        path = directory / a["file"]
        require(path.resolve().parent == directory.resolve(), "stage escaped capture directory")
        values = array.array("f", path.read_bytes())
        if sys.byteorder != "little":
            values.byteswap()
        require(len(values) == m["width"] * m["height"] * 4 and
                all(math.isfinite(v) for v in values), "stage bytes/nonfinite")
        images[name] = values
    return m, identity, images


def compare(left, right, output):
    require(left.resolve() != right.resolve(), "independent captures required")
    a, aid, ap = load_capture(left)
    b, bid, bp = load_capture(right)
    require(aid["inputHash"] == bid["inputHash"], "sealed input identity mismatch")
    same_backend = a["backend"] == b["backend"]
    if same_backend:
        require(aid["graphHash"] == bid["graphHash"], "same-backend graph identity mismatch")
        graph_contract = "full-topology"
        logical_hash = aid["graphHash"]
    else:
        logical_hash = logical_backend_graph(a["compiledGraph"], a["backend"])
        require(logical_hash == logical_backend_graph(b["compiledGraph"], b["backend"]),
                "logical cross-backend graph identity mismatch")
        graph_contract = "logical-ssgi-ring-and-present-transport-v1"
    require(set(ap) == set(bp), "diagnostic stage set mismatch")
    output.mkdir(parents=True, exist_ok=True)
    width, height = a["width"], a["height"]
    results = []
    for name in sorted(ap):
        maximum = squared = 0.0
        changed = exceeded = 0
        channels = 1 if name == "depth" else 4
        diff = bytearray(width * height * 3) if name in ("display", "preToneHdr") else None
        for pixel in range(width * height):
            deltas = []
            over = False
            for c in range(channels):
                i = pixel * channels + c
                x, y = ap[name][i], bp[name][i]
                delta = abs(x - y)
                maximum = max(maximum, delta)
                squared += delta * delta
                over |= delta > .002 + .005 * max(abs(x), abs(y))
                deltas.append(delta)
            changed += any(d > 0 for d in deltas)
            exceeded += over
            if diff is not None:
                diff[pixel * 3:pixel * 3 + 3] = bytes(min(255, round(d * 255 * 16)) for d in deltas[:3])
        if diff is not None:
            png(output / (name + "-difference-x16.png"), width, height, diff)
        results.append(dict(attachment=name, maxError=maximum, rmse=math.sqrt(squared / len(ap[name])),
                            changedPixels=changed, exceededPixels=exceeded))
    for m, images, label in ((a, ap, "left"), (b, bp, "right")):
        rgb = bytes(min(255, max(0, round(v * 255))) for i, v in enumerate(images["display"]) if i % 4 != 3)
        png(output / (label + "-final.png"), width, height, rgb)
    verdict = dict(schemaVersion=1, left=str(left), right=str(right), **aid,
                   sameBackend=same_backend, graphComparison=graph_contract,
                   rightGraphHash=bid["graphHash"], logicalGraphHash=logical_hash, attachments=results,
                   passed=all(r["exceededPixels"] == 0 for r in results),
                   gpuComplete=all(m["measurement"]["gpuStatus"] == "measured" for m in (a, b)))
    (output / "comparison.json").write_text(json.dumps(verdict, indent=2), encoding="utf-8")
    require(verdict["passed"], "pixel regression; see comparison.json")
    return verdict


def mutations(directory, output):
    m, identity, _ = load_capture(directory)
    cases = {}
    def reject(name, candidate, expected=None):
        try:
            actual = audit_manifest(candidate)
            if expected is not None:
                require(actual == expected, "identity mutation")
        except (ValueError, KeyError, IndexError):
            cases[name] = "rejected"
            return
        raise ValueError("mutation survived: " + name)
    c = copy.deepcopy(m); c["camera"]["view"][0] += .25; reject("input-identity", c, identity)
    c = copy.deepcopy(m); del c["renderWidth"]; reject("missing-resolution", c)
    c = copy.deepcopy(m); c["frameKind"] = "generated"; reject("generated-frame", c)
    c = copy.deepcopy(m); c["compiledGraph"]["executeOrder"].reverse(); reject("reversed-order", c)
    c = copy.deepcopy(m)
    require(c["compiledGraph"]["reachabilityEdges"], "edge mutation requires nonempty graph")
    c["compiledGraph"]["reachabilityEdges"].pop(); reject("missing-edge", c)
    c = copy.deepcopy(m); c["compiledGraph"]["resources"][0]["lastUse"] += 1; reject("lifetime", c)
    c = copy.deepcopy(m); c["measurement"]["gpuStatus"] = "failed"; reject("GPU-unavailable", c)
    c = copy.deepcopy(m); c["ibl"]["reflectionSamples"] = 1024; reject("quality-reduction", c)
    c = copy.deepcopy(m)
    barrier_pass = next(p for p in c["compiledGraph"]["passes"] if p["barriers"])
    barrier_pass["barriers"].pop(); reject("missing-barrier", c)
    # Exercise the real byte reader and pixel comparator on an isolated copy.
    import tempfile
    import shutil
    with tempfile.TemporaryDirectory(prefix="base0-pixel-mutation-") as folder:
        mutated = Path(folder) / "capture"
        shutil.copytree(directory, mutated)
        display = next(a for a in m["attachments"] if a["name"] == "display")
        image = mutated / display["file"]
        values = bytearray(image.read_bytes())
        original = struct.unpack_from("<f", values)[0]
        struct.pack_into("<f", values, 0, original + .5)
        image.write_bytes(values)
        try:
            compare(directory, mutated, Path(folder) / "comparison")
        except ValueError as error:
            require("pixel regression" in str(error), "pixel mutation failed for unrelated reason")
            cases["pixel-regression"] = "rejected"
        else:
            raise ValueError("pixel mutation survived")
    output.write_text(json.dumps(dict(passed=True, mutations=cases), indent=2), encoding="utf-8")
    return cases


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("operation", choices=("compare", "mutations"))
    parser.add_argument("left", type=Path)
    parser.add_argument("right_or_output", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    args = parser.parse_args()
    if args.operation == "compare":
        require(args.output is not None, "comparison output required")
        result = compare(args.left, args.right_or_output, args.output)
    else:
        result = mutations(args.left, args.right_or_output)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
