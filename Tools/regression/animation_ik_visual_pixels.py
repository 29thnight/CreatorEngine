"""Check the rendered CreatorRobot IK fade across automatic L0/L1 stages."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


POSES = ("ik-base", "ik-full", "ik-down1", "ik-down2", "ik-down3",
         "ik-up1", "ik-up2", "ik-up3")
WEIGHTS = (1., 1., 2. / 3., 1. / 3., 0., 1. / 3., 2. / 3., 1.)
STAGES = (0, 0, 1, 1, 1, 0, 0, 0)
TASKS = (0, 1, 1, 1, 0, 1, 1, 1)


def main(work):
    work = Path(work)
    results = [json.loads(line) for line in (work / "results.jsonl").read_text(
        encoding="utf-8-sig").splitlines()]
    reports = {}
    latest = None
    for result in results:
        if result["command"] == "animation.visual.probe":
            latest = result["data"]
        if result["command"] == "render.pbr.capture":
            reports[Path(result["data"]["directory"]).name] = latest

    checks, failures, captures = {}, [], {}

    def check(name, condition):
        checks[name] = bool(condition)
        if not condition:
            failures.append(name)

    check("all-captures", set(reports) == set(POSES))
    if set(reports) != set(POSES):
        print(f"IK capture names: {sorted(reports)}")
        return 1

    for name, weight, stage, tasks in zip(POSES, WEIGHTS, STAGES, TASKS):
        report = reports[name]
        folder = work / name
        manifest = json.loads((folder / "manifest.json").read_text(encoding="utf-8-sig"))
        pixels = {}
        for attachment in manifest["attachments"]:
            if attachment["name"] not in ("baseColor", "depth", "display"):
                continue
            image = np.fromfile(folder / attachment["file"], dtype="<f4")
            pixels[attachment["name"]] = image.reshape(
                attachment["height"], attachment["width"], attachment["channels"])
            check(f"{name}/finite/{attachment['name']}",
                  np.isfinite(pixels[attachment["name"]]).all())
        check(f"{name}/product-dx12", manifest["source"] == "product-live"
              and manifest["backend"] == "dx12"
              and manifest["captureMode"] == "static-repeatability-v1")
        check(f"{name}/render-errors", manifest["sealLedger"]["encoderDrops"] == 0
              and manifest["sealLedger"]["textureUploadFailures"] == 0)
        skin = [draw for draw in manifest["draws"] if draw["modelId"] == report["modelId"]]
        marker = [draw for draw in manifest["draws"] if draw["meshId"] == report["markerMeshId"]]
        check(f"{name}/product-draws", len(skin) == 4 and len(marker) == 1
              and report["skinnedMeshes"] == 4)
        if marker:
            world = np.asarray(marker[0]["world"])[12:15]
            socket = np.asarray([report["x"], report["y"], report["z"]])
            check(f"{name}/socket-proxy", np.max(np.abs(world - socket)) < .002)
        check(f"{name}/automatic-stage", report["qualityStage"] == stage
              and (report["projectedHeight"] >= .08 if stage == 1
                   else report["projectedHeight"] >= .12))
        check(f"{name}/ik-weight", abs(report["optionalIKWeight"] - weight) < .002)
        check(f"{name}/ik-recipe", report["ikTasks"] == tasks)

        socket = np.array([report["x"], report["y"], report["z"], 1.])
        camera = manifest["camera"]
        clip = (socket @ np.asarray(camera["view"]).reshape(4, 4)
                @ np.asarray(camera["projection"]).reshape(4, 4))
        h, w = pixels["baseColor"].shape[:2]
        point = np.array([(clip[0] / clip[3] * .5 + .5) * w,
                          (.5 - clip[1] / clip[3] * .5) * h])
        x0, x1 = max(0, int(point[0]) - 28), min(w, int(point[0]) + 29)
        y0, y1 = max(0, int(point[1]) - 28), min(h, int(point[1]) + 29)
        check(f"{name}/marker-in-view", clip[3] > 0 and x0 < x1 and y0 < y1)
        green = np.zeros((h, w), dtype=bool)
        if x0 < x1 and y0 < y1:
            patch = pixels["baseColor"][y0:y1, x0:x1, :3]
            green[y0:y1, x0:x1] = ((patch[..., 1] > .5)
                                    & (patch[..., 0] < .1) & (patch[..., 2] < .15))
        ys, xs = np.where(green)
        check(f"{name}/rendered-marker", len(xs) >= 6)
        centroid = np.array([xs.mean(), ys.mean()]) if len(xs) else point
        check(f"{name}/marker-projection", np.linalg.norm(centroid - point) < 12.)
        image = Image.fromarray(np.round(np.clip(pixels["display"][..., :3], 0, 1)
                                          * 255).astype(np.uint8))
        image.save(folder / "display.png")
        captures[name] = dict(manifest=manifest, report=report, pixels=pixels,
                              point=centroid, image=image)

    base, full = captures["ik-base"]["point"], captures["ik-full"]["point"]
    distance = float(np.linalg.norm(full - base))
    check("ik-visible-marker-travel", distance >= 3.)
    outgoing = [captures[name]["point"] for name in POSES[1:5]]
    incoming = [captures[name]["point"] for name in POSES[4:]]
    out_distances = [float(np.linalg.norm(point - base)) for point in outgoing]
    in_distances = [float(np.linalg.norm(point - base)) for point in incoming]
    check("ik-down-monotone-screen", all(a + .8 >= b for a, b in zip(
        out_distances, out_distances[1:])))
    check("ik-up-monotone-screen", all(a <= b + .8 for a, b in zip(
        in_distances, in_distances[1:])))
    check("ik-no-single-frame-snap", all(float(np.linalg.norm(a - b))
        < distance * .7 + 1.5 for path in (outgoing, incoming)
        for a, b in zip(path, path[1:])))
    check("ik-zero-return", np.linalg.norm(captures["ik-down3"]["point"] - base) < 1.5
          and captures["ik-down3"]["report"]["paletteDigest"]
          == captures["ik-base"]["report"]["paletteDigest"])
    check("ik-full-return", np.linalg.norm(captures["ik-up3"]["point"] - full) < 1.5
          and captures["ik-up3"]["report"]["paletteDigest"]
          == captures["ik-full"]["report"]["paletteDigest"])
    check("ik-intermediate-repeat", np.linalg.norm(
        captures["ik-down2"]["point"] - captures["ik-up1"]["point"]) < 1.5
          and np.linalg.norm(captures["ik-down1"]["point"]
                             - captures["ik-up2"]["point"]) < 1.5)

    # The robot's skin must change too, not only the green socket marker.
    original = captures["ik-base"]["pixels"]["depth"]
    corrected = captures["ik-full"]["pixels"]["depth"]
    changed = np.any(np.abs(original - corrected) > 1.e-5, axis=2)
    for name in ("ik-base", "ik-full"):
        x, y = captures[name]["point"]
        # The cube spans about eight pixels; a wider box also erases the arm.
        x0, x1 = max(0, int(x) - 8), min(changed.shape[1], int(x) + 9)
        y0, y1 = max(0, int(y) - 8), min(changed.shape[0], int(y) + 9)
        changed[y0:y1, x0:x1] = False
    skin_pixels = int(changed.sum())
    check("ik-skin-depth-changes", skin_pixels >= 30)
    check("ik-render-order", all(captures[a]["manifest"]["frameId"]
        < captures[b]["manifest"]["frameId"] for a, b in zip(POSES, POSES[1:])))

    sheet = Image.new("RGB", (430 * 4, 270 * 2), "#151920")
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(POSES):
        x, y = index % 4 * 430, index // 4 * 270
        sheet.paste(captures[name]["image"], (x, y + 30))
        draw.text((x + 10, y + 8), name, fill="white")
    sheet.save(work / "animation-ik-contact-sheet.png")
    summary = dict(passed=not failures, captures=len(captures), checks=len(checks),
                   failures=failures, screenTravelPixels=distance,
                   outgoingDistances=out_distances, incomingDistances=in_distances,
                   changedSkinDepthPixels=skin_pixels, assertions=checks)
    (work / "ik-visual-summary.json").write_text(json.dumps(summary, indent=2),
                                                   encoding="utf-8")
    print(f"ANIMATION_IK_VISUAL_{'OK' if not failures else 'FAILED'} "
          f"captures={len(captures)} checks={len(checks)} "
          f"travel={distance:.2f}px skin={skin_pixels}")
    for failure in failures:
        print(f"  FAIL {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
