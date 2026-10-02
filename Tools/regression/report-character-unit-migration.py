"""Offline legacy character report. Never overwrites Scene/Prefab or supplies runtime fallbacks.
Requires PyYAML. Baseline seconds must come from the old project's measured/configured fixed tick.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import yaml


def number(value, name, *, minimum=None, maximum=None):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name}: finite number required")
    value = float(value)
    if minimum is not None and value < minimum or maximum is not None and value > maximum:
        raise ValueError(f"{name}: out of range")
    return value


def convert(fields, baseline_seconds):
    dt = number(baseline_seconds, "baseline_seconds", minimum=1e-6, maximum=1)
    required = ("m_radius", "m_height", "maxSpeed", "acceleration", "staticFriction",
                "dynamicFriction", "jumpSpeed", "gravityWeight", "m_fBaseSpeed", "m_fFinalMultiplierSpeed")
    values = {name: number(fields[name], name, minimum=0) for name in required}
    if values["m_radius"] == 0 or values["m_height"] == 0:
        raise ValueError("Capsule dimensions must be positive")
    for name in ("staticFriction", "dynamicFriction"):
        number(values[name], name, maximum=1)
    def damping(fraction):
        return {"instantStop": fraction == 1,
                "decayPerSecond": None if fraction == 1 else -math.log1p(-fraction) / dt}
    converted = {"serializedMaxSpeedMetresPerSecond": values["maxSpeed"] / dt,
                 "steadyMaxSpeedMetresPerSecond": values["m_fBaseSpeed"] * values["m_fFinalMultiplierSpeed"] / dt,
                 "accelerationMetresPerSecondSquared": values["acceleration"] * values["m_fFinalMultiplierSpeed"] / dt,
                 "jumpVelocityMetresPerSecond": values["jumpSpeed"] / dt,
                 "gravityMetresPerSecondSquared": -values["gravityWeight"] / dt,
                 "staticDamping": damping(values["staticFriction"]),
                 "dynamicDamping": damping(values["dynamicFriction"])}
    if not all(math.isfinite(v) for v in converted.values() if isinstance(v, (int, float))):
        raise ValueError("Unit conversion overflow")
    return {"baselineSeconds": dt, "convertedMovement": converted,
            "authoringProposal": {"m_characterSchema": 1, "m_radius": values["m_radius"],
                "m_cylinderHeight": values["m_height"], "m_contactOffset": .1, "m_stepOffset": .001,
                "m_slopeLimitCosine": .7, "m_gravity": converted["gravityMetresPerSecondSquared"],
                "m_minimumDistance": .01,
                "m_acceleration": converted["accelerationMetresPerSecondSquared"],
                "m_brakingDecay": converted["staticDamping"]["decayPerSecond"],
                "m_jumpSpeed": converted["jumpVelocityMetresPerSecond"], "m_maxFallSpeed": 55,
                "m_initialVelocity": {"x": 0, "y": 0, "z": 0}},
            "reviewRequired": ["Resolve serialized maxSpeed versus late-update baseSpeed*multiplier policy",
                "Review acceleration/jump/static braking proposals; dynamic lerp and hidden unclamped legacy momentum are not preserved",
                "Legacy controller settings absent from serialized component use source defaults; verify runtime overrides",
                "Verify position/rotation offsets, scale and parent transforms",
                "Remove companion rigid body only after reviewing its independent gameplay ownership",
                "Use ForceVelocity with simulation seconds; cancellation is explicit and maxFallSpeed=55 is a new policy choice"]}


def collect(node, baseline_seconds, path="$", output=None):
    if output is None:
        output = []
    if isinstance(node, dict):
        if "CharacterControllerComponent" in node:
            report = convert(node, baseline_seconds)
            report.update(sourcePath=path, componentId=node.get("m_instanceID"))
            output.append(report)
        for key, value in node.items():
            collect(value, baseline_seconds, f"{path}.{key}", output)
    elif isinstance(node, list):
        for i, value in enumerate(node):
            collect(value, baseline_seconds, f"{path}[{i}]", output)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--baseline-seconds", type=float, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error("Report cannot overwrite source")
    raw = args.source.read_bytes()
    reports = collect(yaml.safe_load(raw), args.baseline_seconds)
    result = {"schema": 1, "source": str(args.source.resolve()), "sha256": hashlib.sha256(raw).hexdigest(),
              "status": "review_required" if reports else "no_legacy_characters", "characters": reports}
    # Validate all inputs/conversions before creating output. Source is never modified.
    text = json.dumps(result, indent=2, allow_nan=False)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text + "\n", encoding="utf-8")
    print(f"CHARACTER_UNIT_REPORT_OK characters={len(reports)} source_unchanged=true")


if __name__ == "__main__":
    main()
