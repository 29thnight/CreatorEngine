#!/usr/bin/env python3
"""Offline-only legacy input migration. No runtime importer or callback dispatcher.

Inspect is read-only. Convert requires an explicit, source-hash-bound review for
all action types, domains, callback consumers, and changed axis/conflict policy.
The deliberately narrow YAML reader accepts the canonical v1 map corpus; it does
not guess unknown YAML features, enum names, value types, or callback signatures.
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys
import uuid

MAX_BYTES = 4 * 1024 * 1024
FIELDS = {"actionName", "inputType", "actionType", "keyState", "keys", "scriptName", "functionName"}
# Physical Windows Set-1 scan codes. Ambiguous layout/OEM keys require manual authoring.
VK_SCAN = dict(zip(range(65, 91), [30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
                                 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44]))
VK_SCAN.update({32: 0x39, 13: 0x1C, 27: 0x01, 9: 0x0F,
                37: 0xE04B, 38: 0xE048, 39: 0xE04D, 40: 0xE050})


def read_text(path):
    if path.stat().st_size > MAX_BYTES:
        raise ValueError("input exceeds the offline migration byte limit")
    return path.read_text(encoding="utf-8-sig")


def strict_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON field: " + key)
        result[key] = value
    return result


def parse_json(text):
    return json.loads(text, object_pairs_hook=strict_object)


def scalar(value):
    if value.startswith('"') or value.startswith('['):
        return parse_json(value)
    if re.fullmatch(r"[0-9]+", value):
        return int(value)
    raise ValueError("unsupported YAML scalar; use the canonical v1 archive")


def read_map(path):
    text = read_text(path)
    if text.lstrip().startswith('{'):
        result = parse_json(text)
    else:
        result, actions, current = {}, [], None
        for number, line in enumerate(text.splitlines(), 1):
            if not line.strip():
                continue
            if line == "actions:":
                if "actions" in result:
                    raise ValueError("duplicate actions field")
                result["actions"] = actions
                continue
            match = re.fullmatch(r"(  - |    |)([A-Za-z]+): (.+)", line)
            if not match:
                raise ValueError(f"line {number}: unsupported legacy YAML structure")
            indent, key, value = match.groups()
            if indent == "  - ":
                current = {}
                actions.append(current)
            target = result if not indent else current
            if target is None or key in target:
                raise ValueError(f"line {number}: duplicate or misplaced field")
            target[key] = scalar(value)
    if set(result) != {"schemaVersion", "mapName", "actions"} or result["schemaVersion"] != 1:
        raise ValueError("unsupported legacy schema or unknown root fields")
    if not isinstance(result["mapName"], str) or not result["mapName"]:
        raise ValueError("missing map name")
    if not isinstance(result["actions"], list) or not 0 < len(result["actions"]) <= 4096:
        raise ValueError("invalid action count")
    names = set()
    for action in result["actions"]:
        if set(action) != FIELDS:
            raise ValueError("unknown/missing legacy action fields; manual review required")
        if any(not isinstance(action[key], str) or not action[key] for key in FIELDS - {"keys"}):
            raise ValueError("invalid action text field")
        if action["actionName"] in names:
            raise ValueError("duplicate action name")
        names.add(action["actionName"])
        keys = action["keys"]
        if not isinstance(keys, list) or not 1 <= len(keys) <= 4 or any(type(key) is not int or not 0 <= key <= 65535 for key in keys):
            raise ValueError("invalid legacy keys")
        if action["inputType"] not in {"KeyBoard", "GamePad", "Mouse"} or action["actionType"] not in {"Button", "Value"} or action["keyState"] not in {"Down", "Pressed", "Released"}:
            raise ValueError("unsupported legacy enum; migration fails closed")
    return result


def inspect(path):
    document = read_map(path)
    return {"source": path.name, "sourceSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "mapName": document["mapName"], "actions": [
                {"name": action["actionName"], "valueType": "Unresolved" if action["actionType"] == "Value" else "Button",
                 "callback": action["scriptName"] + "." + action["functionName"],
                 "keyState": action["keyState"], "consumerStatus": "RequiresExplicitReview"}
                for action in document["actions"]]}


def quote(value):
    # LX uses std::quoted, not JSON escapes. Disallow control characters entirely.
    if any(ord(ch) < 32 for ch in value):
        raise ValueError("control characters cannot be represented in LX names")
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def convert(path, review):
    document = read_map(path)
    report = inspect(path)
    identity = str(uuid.UUID(review["graphGuid"]))
    if identity != review["graphGuid"] or uuid.UUID(identity).version != 4:
        raise ValueError("graphGuid must be a canonical UUIDv4")
    if review.get("sourceSha256") != report["sourceSha256"]:
        raise ValueError("review source hash does not match the exact legacy bytes")
    decisions = review.get("actions", {})
    if set(decisions) != {item["actionName"] for item in document["actions"]}:
        raise ValueError("every action requires exactly one reviewed decision")
    nodes, links, identifiers, bindings = [], [], set(), []

    def stable(label):
        value = int.from_bytes(hashlib.sha256((identity + "|" + label).encode()).digest()[:8], "big") & ((1 << 63) - 1)
        if not value or value in identifiers:
            raise ValueError("stable-ID collision; provide a different explicit graph identity")
        identifiers.add(value)
        return value

    def node(label, kind, pins, properties):
        nid = stable(label)
        sockets = []
        for name, direction, value_type, multiple in pins:
            sockets.append((stable(label + "/pin/" + name), name, direction, value_type, multiple))
        nodes.append((nid, kind, label, sockets, properties))
        return nid, {name: pid for pid, name, _, _, _ in sockets}

    def link(label, output, input_pin):
        links.append((stable(label), output, input_pin))

    for action in document["actions"]:
        name = action["actionName"]
        decision = decisions[name]
        if decision.get("consumerReviewed") is not True or not decision.get("consumerFile") or not decision.get("consumerSymbol"):
            raise ValueError(name + ": callback consumer implementation must be explicitly reviewed")
        dispatch = decision.get("dispatch")
        if dispatch not in {"Performed", "Completed", "ReadHeld", "ReadValue"}:
            raise ValueError(name + ": explicit typed consumer dispatch is required")
        value_type = decision.get("valueType")
        if value_type not in {"Button", "Float", "Vector2"} or (action["actionType"] == "Button" and value_type != "Button"):
            raise ValueError(name + ": explicit compatible value type required")
        domain, claim = decision.get("domain"), decision.get("claim")
        if domain not in {"Game", "UI"} or claim not in {"PassThrough", "OnPress", "OnPerformed"}:
            raise ValueError(name + ": explicit domain and claim policy required")
        layer, _ = node(name + "/layer", "Input.Layer", [], {"name": decision.get("layerName", name),
            "priority": "0", "claim": claim, "enabled": "true"})
        tag = {"Button": 1, "Float": 3, "Vector2": 11}[value_type]
        keys, device = action["keys"], action["inputType"]
        if device == "KeyBoard" and value_type == "Vector2":
            if len(keys) != 4 or decision.get("axisPolicy") != "SumOppositesNeutral":
                raise ValueError(name + ": review axis sum (legacy negative key had precedence), or author manually")
            _, pins = node(name + "/axis", "Input.Axis2D",
                [(part, 0, 1, False) for part in ["Left", "Right", "Down", "Up"]] + [("Value", 1, 11, False)], {})
            for part, key in zip(["Left", "Right", "Down", "Up"], keys):
                if key not in VK_SCAN:
                    raise ValueError(name + ": ambiguous VK; manually author a physical control")
                _, key_pin = node(name + "/" + part, "Input.Key", [("Value", 1, 1, False)], {"control": str(VK_SCAN[key])})
                link(name + "/" + part + "/link", key_pin["Value"], pins[part])
            source = pins["Value"]
        else:
            if device == "KeyBoard" and value_type == "Button" and keys[0] in VK_SCAN:
                kind, control, space = "Input.Key", VK_SCAN[keys[0]], None
            elif device == "GamePad" and value_type == "Button" and 0 <= keys[0] <= 11:
                kind, control, space = "Input.GamepadButton", keys[0], None
            elif device == "GamePad" and value_type == "Vector2" and keys[0] in {12, 13}:
                kind, control, space = "Input.GamepadStick", keys[0] - 12, "Normalized"
            else:
                raise ValueError(name + ": unsupported/ambiguous legacy source; author it explicitly in LX")
            props = {"control": str(control)}
            if space:
                props["space"] = space
            _, pins = node(name + "/source", kind, [("Value", 1, tag, False)], props)
            source = pins["Value"]
        signal, pins = node(name + "/signal", "Input.Signal." + value_type,
            [("Value", 0, tag, True), ("Signal", 1, tag, False)],
            {"name": name, "layer": str(layer), "domain": domain, "lifetime": "Persistent",
             "combine": "MaximumMagnitude", "resumePersistentValue": "false"})
        link(name + "/binding", source, pins["Value"])
        bindings.append({"legacyAction": name, "legacyCallback": action["scriptName"] + "." + action["functionName"],
                         "legacyKeyState": action["keyState"], "signalNode": signal, "dispatch": dispatch,
                         "consumerFile": decision["consumerFile"], "consumerSymbol": decision["consumerSymbol"]})
    lines = [f"LXINPUT 1 {quote(identity)}", f'LXG 10 "input" {max(identifiers) + 1}', f"N {len(nodes)}"]
    for nid, kind, title, pins, properties in nodes:
        lines.append(f"{nid} {quote(kind)} {quote(title)} {len(pins)} {len(properties)} 0 0 3 0 0 0")
        for pid, name, direction, value_type, multiple in pins:
            lines.append(f"{pid} {quote(name)} {quote(name)} {direction} {value_type} {int(multiple)} 0 0 0")
        for key, value in sorted(properties.items()):
            lines.append(f"{quote(key)} {quote(value)}")
    lines += ["P 0", "F 0", "V 0 0 0 1", f"L {len(links)}"]
    lines += [f"{lid} {output} {input_pin}" for lid, output, input_pin in links]
    lines += ["G 0", ""]
    return "\n".join(lines), {"graphGuid": identity, "sourceSha256": report["sourceSha256"], "consumers": bindings,
        "validation": "Source generation only. Run native InputGraph round-trip and cook validation before adoption."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=pathlib.Path)
    parser.add_argument("--review", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if not args.review:
        print(json.dumps(inspect(args.source), indent=2, ensure_ascii=False))
        return
    if not args.output or args.output.suffix != ".inputgraph":
        raise ValueError("conversion requires a new .inputgraph --output")
    source, manifest = convert(args.source, parse_json(read_text(args.review)))
    paths = [args.output, pathlib.Path(str(args.output) + ".meta"), pathlib.Path(str(args.output) + ".migration.json")]
    if any(path.exists() for path in paths):
        raise ValueError("refusing to overwrite an asset, sidecar or migration report")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payloads = [source, "guid: " + manifest["graphGuid"] + "\n", json.dumps(manifest, indent=2) + "\n"]
    written = []
    try:
        for path, payload in zip(paths, payloads):
            with path.open("x", encoding="utf-8", newline="\n") as output:
                written.append(path)
                output.write(payload)
    except Exception:
        for path in written:
            path.unlink(missing_ok=True)
        raise
    print("Converted source and explicit consumer migration report. Native validation remains required.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, TypeError) as error:
        print("Input migration blocked: " + str(error), file=sys.stderr)
        sys.exit(2)
