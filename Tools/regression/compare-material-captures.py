"""Compare matching controlled product capture attachments across build configurations."""
import argparse
import array
import json
import math
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    left = json.loads((args.left / "manifest.json").read_text(encoding="utf-8-sig"))
    right = json.loads((args.right / "manifest.json").read_text(encoding="utf-8-sig"))
    for key in ("width", "height", "camera", "lights", "captureMode", "sampleIndex", "historyPolicy"):
        if left.get(key) != right.get(key):
            raise ValueError(f"Capture inputs differ: {key}")
    if left.get("captureMode") != "static-repeatability-v1":
        raise ValueError("Requires controlled captures")
    if Path(left["skyBoxPath"]).name != Path(right["skyBoxPath"]).name:
        raise ValueError("Environment asset names differ")
    def material_draws(manifest):
        draws = [{k: draw.get(k) for k in ("route", "modelId", "meshId")} |
                {"material": {k: draw.get("lattice", {}).get(k)
                              for k in ("features", "coverage", "uniformBytes", "textures")}}
                for draw in manifest["draws"]]
        # Independent mesh preparation may publish opaque draws in a different
        # order. Match the full input records by identity without discarding any.
        return sorted(draws, key=lambda draw: json.dumps(draw, sort_keys=True))
    if material_draws(left) != material_draws(right):
        raise ValueError("Material inputs or mesh identities differ")
    rows = []
    for attachment in left["attachments"]:
        other = next(a for a in right["attachments"] if a["name"] == attachment["name"])
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
        nonfinite = 0
        for x, y in zip(a, b):
            if not math.isfinite(x) or not math.isfinite(y):
                nonfinite += 1
                continue
            difference = abs(x - y)
            maximum = max(maximum, difference)
            normalized = max(normalized, difference / (1 + max(abs(x), abs(y))))
            squares += difference * difference
            total += difference
        # Display is quantized to eight bits; linear attachments use the native
        # scalar probe's 1e-3 normalized error ceiling. This is not a Blender gate.
        passed = nonfinite == 0 and (maximum <= 1 / 255 + 1e-7 if attachment["name"] == "display"
                                     else normalized <= 1e-3)
        rows.append(dict(name=attachment["name"], components=len(a), maxAbs=maximum,
                         maxNormalized=normalized, meanAbs=total / len(a),
                         rms=math.sqrt(squares / len(a)), nonfinite=nonfinite, passed=passed))
    report = dict(left=str(args.left), right=str(args.right), width=left["width"], height=left["height"],
                  passed=all(row["passed"] for row in rows), attachments=rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
