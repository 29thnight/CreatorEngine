"""Compare matching controlled product capture attachments across build configurations."""
import argparse
import array
import json
import math
import sys
from pathlib import Path
from base0_artifacts import audit_pixel_measurement


def material_draws(manifest):
    identity_fields = (
        "route", "modelId", "meshId", "modelGeneration", "world", "pose",
        "coverageFlags", "alphaCutoff", "meshDomain", "assetId", "subassetId",
        "resolverRevision", "representation", "artifactSchema", "artifactBytes",
        "targetPlatform", "targetAbi", "contentSha256",
    )
    draws = [{key: draw.get(key) for key in identity_fields} |
             {"material": {key: draw.get("lattice", {}).get(key)
                           for key in ("features", "coverage", "uniformBytes", "textures")}}
             for draw in manifest["draws"]]
    # Publication order and runtime material slots may differ. Preserve every
    # input record, including repeated instances, while matching by value.
    return sorted(draws, key=lambda draw: json.dumps(draw, sort_keys=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--exact", action="store_true", help="Require identical finite float32 values")
    parser.add_argument("--diagnostic-stages", action="store_true", help="Also compare every named intermediate stage")
    args = parser.parse_args()
    left = json.loads((args.left / "manifest.json").read_text(encoding="utf-8-sig"))
    right = json.loads((args.right / "manifest.json").read_text(encoding="utf-8-sig"))
    audit_pixel_measurement(left)
    audit_pixel_measurement(right)
    for key in ("width", "height", "camera", "lights", "captureMode", "sampleIndex", "historyPolicy"):
        if left.get(key) != right.get(key):
            raise ValueError(f"Capture inputs differ: {key}")
    if left.get("captureMode") != "static-repeatability-v1":
        raise ValueError("Requires controlled captures")
    if Path(left["skyBoxPath"]).name != Path(right["skyBoxPath"]).name:
        raise ValueError("Environment asset names differ")
    if material_draws(left) != material_draws(right):
        raise ValueError("Material inputs or mesh identities differ")
    rows = []
    groups = [("attachment", "attachments")]
    if args.diagnostic_stages:
        groups.append(("stage", "diagnosticStages"))
    pairs = []
    for kind, field in groups:
        descriptions = left.get(field, [])
        others = right.get(field, [])
        names = [item["name"] for item in descriptions]
        other_names = [item["name"] for item in others]
        if (not names or len(set(names)) != len(names) or len(set(other_names)) != len(other_names)
                or set(names) != set(other_names)):
            raise ValueError(f"Missing, duplicate or mismatched {field}")
        by_name = {item["name"]: item for item in others}
        pairs.extend((kind, item, by_name[item["name"]]) for item in descriptions)
    for kind, attachment, other in pairs:
        for key in ("channels", "width", "height", "encoding"):
            if attachment[key] != other[key]:
                raise ValueError(f"Attachment format differs: {attachment['name']}")
        def read(directory, description):
            values = array.array("f")
            values.frombytes((directory / description["file"]).read_bytes())
            if sys.byteorder != "little":
                values.byteswap()
            if len(values) != description["width"] * description["height"] * description["channels"]:
                raise ValueError("Attachment length differs from manifest")
            return values
        a, b = read(args.left, attachment), read(args.right, other)
        maximum = normalized = squares = total = 0.0
        nonfinite = changed = 0
        for x, y in zip(a, b):
            if not math.isfinite(x) or not math.isfinite(y):
                nonfinite += 1
                continue
            difference = abs(x - y)
            changed += difference != 0
            maximum = max(maximum, difference)
            normalized = max(normalized, difference / (1 + max(abs(x), abs(y))))
            squares += difference * difference
            total += difference
        # Display is quantized to eight bits; linear attachments use the native
        # scalar probe's 1e-3 normalized error ceiling. This is not a Blender gate.
        passed = nonfinite == 0 and (changed == 0 if args.exact else
                                    maximum <= 1 / 255 + 1e-7 if attachment["name"] == "display"
                                    else normalized <= 1e-3)
        rows.append(dict(name=attachment["name"], kind=kind, components=len(a), maxAbs=maximum,
                         maxNormalized=normalized, meanAbs=total / len(a),
                         rms=math.sqrt(squares / len(a)), nonfinite=nonfinite,
                         changedComponents=changed, exact=changed == 0 and nonfinite == 0, passed=passed))
    report = dict(left=str(args.left), right=str(args.right), width=left["width"], height=left["height"],
                  comparison="exact" if args.exact else "tolerance",
                  passed=all(row["passed"] for row in rows),
                  attachments=[row for row in rows if row["kind"] == "attachment"],
                  diagnosticStages=[row for row in rows if row["kind"] == "stage"],
                  firstDifferingStage=next((row["name"] for row in rows
                                           if row["kind"] == "stage" and not row["exact"]), None))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
