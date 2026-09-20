"""DX12 product readback regression; requires NumPy and Pillow. No golden images."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def main(work):
    work = Path(work)
    results = [json.loads(line) for line in (work / "results.jsonl").read_text(encoding="utf-8-sig").splitlines()]
    reports = {}
    latest = None
    for result in results:
        if result["command"] == "animation.visual.probe":
            latest = result["data"]
        if result["command"] == "render.pbr.capture":
            reports[Path(result["data"]["directory"]).name] = latest
    captures, checks, failures = {}, {}, []

    def check(name, value):
        checks[name] = bool(value)
        if not value:
            failures.append(name)

    for name, report in reports.items():
        folder = work / name
        manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8-sig"))
        pixels = {}
        for attachment in manifest["attachments"]:
            data = np.fromfile(folder / attachment["file"], dtype="<f4")
            data = data.reshape(attachment["height"], attachment["width"], attachment["channels"])
            check(f"{name}/finite/{attachment['name']}", np.isfinite(data).all())
            pixels[attachment["name"]] = data
        check(f"{name}/product-dx12", manifest["source"] == "product-live" and manifest["backend"] == "dx12")
        check(f"{name}/controlled", manifest["captureMode"] == "static-repeatability-v1")
        check(f"{name}/render-errors", manifest["sealLedger"]["encoderDrops"] == 0
              and manifest["sealLedger"]["textureUploadFailures"] == 0)
        skin_draws = [d for d in manifest["draws"] if d["modelId"] == report["modelId"]]
        marker_draws = [d for d in manifest["draws"] if d["meshId"] == report["markerMeshId"]]
        if name == "hidden":
            check("hidden/no-fixture-draws", len(manifest["draws"]) == 0)
        else:
            check(f"{name}/skinned-draws", len(skin_draws) == 4 and report["skinnedMeshes"] == 4)
            check(f"{name}/disabled-background-meshes", len(manifest["draws"]) == len(skin_draws) + 1)
            check(f"{name}/marker-draw", len(marker_draws) == 1)
            if marker_draws:
                actual = np.array(marker_draws[0]["world"])[12:15]
                expected = np.array([report["x"], report["y"], report["z"]])
                check(f"{name}/socket-proxy-position", np.max(np.abs(actual - expected)) < 0.002)
        image = Image.fromarray(np.round(np.clip(pixels["display"][..., :3], 0, 1) * 255).astype(np.uint8))
        image.save(folder / "display.png")
        captures[name] = dict(manifest=manifest, pixels=pixels, report=report, image=image)

    expected_names = {"a", "a-repeat", "b", "next", "blend0", "blendhalf", "blend1",
                      "layer", "layerdisabled", "masked", "upper", "hidden", "show"}
    check("all-captures", set(captures) == expected_names)
    metrics = {"equivalent": {}, "different": {}, "markers": {}}
    marker_bounds = {}
    reference_worlds = {d["meshId"]: d["world"] for d in captures["a"]["manifest"]["draws"]
                        if d["modelId"] == captures["a"]["report"]["modelId"]}
    for name, capture in captures.items():
        if name == "hidden":
            continue
        m, p, report = capture["manifest"], capture["pixels"], capture["report"]
        check(f"{name}/stationary-model-root", all(np.allclose(d["world"], reference_worlds[d["meshId"]],
              atol=1e-6, rtol=0) for d in m["draws"] if d["modelId"] == report["modelId"]))
        world = np.array([report["x"], report["y"], report["z"], 1.0])
        clip = world @ np.array(m["camera"]["view"]).reshape(4, 4) @ np.array(m["camera"]["projection"]).reshape(4, 4)
        h, w = p["baseColor"].shape[:2]
        x = (clip[0] / clip[3] * .5 + .5) * w
        y = (.5 - clip[1] / clip[3] * .5) * h
        radius = 28
        x0, x1 = max(0, int(x) - radius), min(w, int(x) + radius + 1)
        y0, y1 = max(0, int(y) - radius), min(h, int(y) + radius + 1)
        inside = clip[3] > 0 and x0 < x1 and y0 < y1
        check(f"{name}/marker-in-view", inside)
        count = 0
        if inside:
            patch = p["baseColor"][y0:y1, x0:x1, :3]
            green = (patch[..., 1] > .5) & (patch[..., 0] < .1) & (patch[..., 2] < .15)
            count = int(green.sum())
        metrics["markers"][name] = dict(x=float(x), y=float(y), greenPixels=count)
        marker_bounds[name] = x0, x1, y0, y1
        check(f"{name}/rendered-socket-marker", count >= 6)

    def equivalent(left, right):
        pair = {}
        for attachment in ("baseColor", "depth", "normal", "display"):
            a, b = captures[left]["pixels"][attachment], captures[right]["pixels"][attachment]
            d = np.abs(a.astype(np.float64) - b)
            exceeded = np.any(d > .002 + .005 * np.maximum(np.abs(a), np.abs(b)), axis=2)
            fraction, rmse = float(exceeded.mean()), float(np.sqrt(np.mean(d * d)))
            pair[attachment] = dict(exceededFraction=fraction, rmse=rmse, max=float(d.max()))
            # Blend TRS decomposition may perturb silhouette-edge samples slightly.
            check(f"{left}={right}/{attachment}", fraction <= .0015 and rmse <= .004)
        metrics["equivalent"][f"{left}={right}"] = pair
        check(f"{left}={right}/later-frame", captures[left]["manifest"]["frameId"] < captures[right]["manifest"]["frameId"])

    for pair in (("a", "a-repeat"), ("a", "blend0"), ("next", "blend1"),
                 ("next", "layer"), ("a", "layerdisabled"), ("a", "masked"), ("upper", "show")):
        equivalent(*pair)

    background = captures["hidden"]["pixels"]["depth"]
    for name in ("a", "b", "next", "blendhalf", "upper"):
        coverage = int(np.any(np.abs(captures[name]["pixels"]["depth"] - background) > 1e-5, axis=2).sum())
        check(f"{name}/visible-geometry", coverage > 400)
    for left, right in (("a", "b"), ("a", "blendhalf"), ("next", "blendhalf"), ("a", "upper")):
        a, b = captures[left]["pixels"]["depth"], captures[right]["pixels"]["depth"]
        changed = np.any(np.abs(a - b) > 1e-5, axis=2)
        # A moving cube alone cannot pass a skinned-mesh animation check.
        for name in (left, right):
            x0, x1, y0, y1 = marker_bounds[name]
            changed[y0:y1, x0:x1] = False
        count = int(changed.sum())
        metrics["different"][f"{left}!={right}"] = count
        check(f"{left}!={right}/skin-depth", count > 100)
        check(f"{left}!={right}/palette", captures[left]["report"]["paletteDigest"] != captures[right]["report"]["paletteDigest"])

    # Contact sheet contains the actual GPU display readbacks, with labels only.
    labels = [("a", "Walk / 15%"), ("b", "Walk / 70%"), ("next", "Run / 65%"),
              ("blend0", "Blend / 0%"), ("blendhalf", "Blend / 50%"), ("blend1", "Blend / 100%"),
              ("layer", "Overlay enabled"), ("layerdisabled", "Overlay disabled"), ("masked", "All layers masked"),
              ("upper", "Upper body overlay"), ("hidden", "Hidden"), ("show", "Visible again")]
    cell_w = 400
    source_w, source_h = captures["a"]["image"].size
    cell_h = round(source_h * cell_w / source_w)
    sheet = Image.new("RGB", (cell_w * 3, (cell_h + 30) * 4), "#151920")
    draw = ImageDraw.Draw(sheet)
    for index, (name, label) in enumerate(labels):
        x, y = index % 3 * cell_w, index // 3 * (cell_h + 30)
        sheet.paste(captures[name]["image"].resize((cell_w, cell_h)), (x, y + 30))
        draw.text((x + 10, y + 8), label, fill="white")
    sheet.save(work / "animation-contact-sheet.png")
    summary = dict(passed=not failures, checks=len(checks), captures=len(captures), failures=failures,
                   metrics=metrics, assertions=checks)
    (work / "visual-summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"ANIMATION_VISUAL_{'OK' if not failures else 'FAILED'} captures={len(captures)} checks={len(checks)}")
    for failure in failures:
        print(f"  FAIL {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
