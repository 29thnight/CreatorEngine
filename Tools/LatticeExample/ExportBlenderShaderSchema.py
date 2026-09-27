"""Export the installed Blender shader node schema for the LX-0 baseline.

Run with Blender 5.1.1 in background mode:
    blender --background --factory-startup --python ExportBlenderShaderSchema.py -- output.json
"""

import json
import pathlib
import sys

import bpy


def json_value(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    try:
        return [json_value(item) for item in value]
    except TypeError:
        return None


def socket_record(socket):
    record = {
        "identifier": socket.identifier,
        "name": socket.name,
        "type": socket.bl_idname,
        "enabled": socket.enabled,
        "hidden": socket.hide,
        "hide_value": socket.hide_value,
        "multi_input": socket.is_multi_input,
    }
    if hasattr(socket, "default_value"):
        record["default"] = json_value(socket.default_value)
    return record


def sockets(node):
    return {
        "inputs": [socket_record(socket) for socket in node.inputs],
        "outputs": [socket_record(socket) for socket in node.outputs],
    }


def enum_variants(node, default_sockets):
    variants = []
    for prop in node.bl_rna.properties:
        if prop.type != "ENUM" or prop.is_readonly:
            continue
        try:
            original = getattr(node, prop.identifier)
        except (AttributeError, TypeError):
            continue
        for item in prop.enum_items:
            if item.identifier == original:
                continue
            try:
                setattr(node, prop.identifier, item.identifier)
                variant_sockets = sockets(node)
            except (AttributeError, TypeError, ValueError):
                continue
            finally:
                try:
                    setattr(node, prop.identifier, original)
                except (AttributeError, TypeError, ValueError):
                    pass
            if variant_sockets != default_sockets:
                variants.append(
                    {
                        "property": prop.identifier,
                        "value": item.identifier,
                        **variant_sockets,
                    }
                )
    return variants


def main():
    arguments = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    if len(arguments) != 1:
        raise SystemExit("Expected exactly one output JSON path after --")
    if bpy.app.version[:3] != (5, 1, 1):
        raise SystemExit(f"Expected Blender 5.1.1, found {bpy.app.version_string}")

    tree = bpy.data.node_groups.new("LX-0 Shader Schema Probe", "ShaderNodeTree")
    records = []
    authoring_types = {"NodeReroute", "NodeFrame", "NodeGroupInput", "NodeGroupOutput"}
    node_types = authoring_types | {
        name for name in dir(bpy.types) if name.startswith("ShaderNode") and name != "ShaderNode"
    }
    for type_name in sorted(node_types):
        node_type = getattr(bpy.types, type_name)
        if not hasattr(node_type, "bl_rna"):
            continue
        record = {"type": type_name, "label": node_type.bl_rna.name}
        try:
            node = tree.nodes.new(type_name)
        except RuntimeError as error:
            record["creation_error"] = str(error)
            records.append(record)
            continue

        record.update(sockets(node))
        record["variants"] = enum_variants(node, sockets(node))
        records.append(record)
        tree.nodes.remove(node)

    group_tree = bpy.data.node_groups.new("LX-0 Group Interface Probe", "ShaderNodeTree")
    group_tree.interface.new_socket(name="Base Color", in_out="INPUT", socket_type="NodeSocketColor")
    group_tree.interface.new_socket(name="Surface", in_out="OUTPUT", socket_type="NodeSocketShader")
    group_node = tree.nodes.new("ShaderNodeGroup")
    group_node.node_tree = group_tree
    group_example = sockets(group_node)
    tree.nodes.remove(group_node)

    output = pathlib.Path(arguments[0])
    output.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "blender_version": bpy.app.version_string,
        "blender_build_hash": bpy.app.build_hash.decode("ascii"),
        "node_tree": "ShaderNodeTree",
        "variant_rule": "Each writable enum changes alone from the node default; enum combinations are not enumerated.",
        "group_interface_example": group_example,
        "nodes": records,
    }
    output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    index = output.with_suffix(".md")
    lines = [
        "# Blender 5.1.1 Shader Node 및 편집 노드 목록",
        "",
        f"실측 설치본: Blender {bpy.app.version_string}, build `{document['blender_build_hash']}`.",
        f"[소켓·기본값 전체 JSON]({output.name})에서 입력·출력 순서, 타입, 기본값, 표시 상태를 확인한다.",
        "enum 변형은 각 속성을 기본 상태에서 하나씩 변경해 관찰했다. 조합은 아직 전수 조사하지 않았다.",
        "현재 LX 예제는 제품용 Material compiler가 없어 아래 노드를 지원 완료로 표시하지 않는다.",
        "",
        "| Blender node type | 이름 | 기본 입력 | 기본 출력 | 소켓 변경 enum 상태 | LX 제품 지원 |",
        "|---|---|---:|---:|---:|---|",
    ]
    for record in records:
        if "creation_error" in record:
            continue
        label = record["label"].replace("|", "\\|")
        lines.append(
            f"| `{record['type']}` | {label} | {len(record['inputs'])} | "
            f"{len(record['outputs'])} | {len(record['variants'])} | 미구현 |"
        )
    lines.extend(
        [
            "",
            "## 생성할 수 없는 RNA 타입",
            "",
        ]
    )
    for record in records:
        if "creation_error" in record:
            reason = " ".join(record["creation_error"].split())
            lines.append(f"- `{record['type']}`: {reason}")
    index.write_text("\n".join(lines) + "\n", encoding="utf-8")
    bpy.data.node_groups.remove(tree)
    bpy.data.node_groups.remove(group_tree)
    failures = sum("creation_error" in record for record in records)
    print(f"LX_SHADER_SCHEMA_OK nodes={len(records)} creation_failures={failures} path={output} index={index}")


if __name__ == "__main__":
    main()
