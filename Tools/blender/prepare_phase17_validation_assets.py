"""Prepare the local high-fidelity model corpus used by PHASE 17 gates.

The input FBX files are imported without geometry transforms, modifiers, decimation,
welding, or cache optimization.  Materials are rebuilt as glTF metallic-roughness
node graphs that CreatorEngine's fastgltf importer consumes.  GLB output explicitly
disables Draco, gltfpack, sparse accessor generation, and animation key reduction.
"""

import argparse
import hashlib
import json
import os
import struct
import sys

import bpy


FORBIDDEN_EXTENSIONS = {
    "EXT_meshopt_compression",
    "KHR_draco_mesh_compression",
    "KHR_mesh_quantization",
}

SPONZA_TEXTURE_KEYS = {
    "arch_stone_wall_01": "arch_stone_wall_01",
    "brickwall_01": "brickwall_01",
    "brickwall_02": "brickwall_02",
    "ceiling_plaster_01": "ceiling_plaster_01",
    "ceiling_plaster_02": "ceiling_plaster_01",
    "column_1stfloor": "column_1stfloor",
    "column_brickwall_01": "column_brickwall_01",
    "column_head_1stfloor": "column_head_1stfloor",
    "column_head_2ndfloor_02": "column_head_2ndfloor_02",
    "column_head_2ndfloor_03": "column_head_2ndfloor_03",
    "door_stoneframe_01": "door_stoneframe_01",
    "door_stoneframe_02": "door_stoneframe_02",
    "floor_01": "floor_tiles_01",
    "metal_door": "metal_door_01",
    "ornament_01": "ornament_01",
    "ornament_lion": "lionhead_01",
    "roof_tiles_01": "roof_tiles_01",
    "stone_trims_01": "stone_trims_01",
    "stone_trims_02": "stone_trims_02",
    "stones_01_tile": "stone_01_tile",
    "stones_2ndfloor": "stones_2ndfloor_01",
    "window_frame_01": "window_frame_01",
    "wood_01": "wood_tile_01",
    "wood_door_01": "wood_door_01",
}


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        while True:
            block = stream.read(4 * 1024 * 1024)
            if not block:
                break
            digest.update(block)
    return digest.hexdigest()


def clear_all_data():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    # Removing only zero-user blocks in dependency order is insufficient: a
    # deleted object can leave an orphan mesh -> material -> image chain, and an
    # armature can keep its Actions alive until the object block is unlinked.
    # Force-unlink the whole previous import so the two validation assets cannot
    # contaminate one another inside the same headless Blender process.
    for collection in (
        bpy.data.objects,
        bpy.data.meshes,
        bpy.data.armatures,
        bpy.data.cameras,
        bpy.data.curves,
        bpy.data.lights,
        bpy.data.materials,
        bpy.data.images,
        bpy.data.actions,
    ):
        for item in list(collection):
            collection.remove(item, do_unlink=True)


def import_fbx(path):
    result = bpy.ops.import_scene.fbx(
        filepath=os.path.abspath(path),
        use_anim=True,
        use_custom_normals=True,
        use_image_search=False,
        use_subsurf=False,
        use_custom_props=True,
        ignore_leaf_bones=False,
        bake_space_transform=False,
    )
    if "FINISHED" not in result:
        raise RuntimeError("FBX import failed: " + path)


def used_materials():
    result = []
    seen = set()
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        for slot in obj.material_slots:
            material = slot.material
            if material and material.name not in seen:
                seen.add(material.name)
                result.append(material)
    return result


def scene_summary():
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    armatures = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    actions = sorted(bpy.data.actions, key=lambda action: action.name)
    return {
        "objects": len(bpy.context.scene.objects),
        "meshes": len(meshes),
        "source_vertices": sum(len(obj.data.vertices) for obj in meshes),
        "source_loops": sum(len(obj.data.loops) for obj in meshes),
        "source_polygons": sum(len(obj.data.polygons) for obj in meshes),
        "uv_layers": sum(len(obj.data.uv_layers) for obj in meshes),
        "armatures": len(armatures),
        "bones": sum(len(obj.data.bones) for obj in armatures),
        "materials": len(used_materials()),
        "actions": [
            {
                "name": action.name,
                "frame_start": float(action.frame_range[0]),
                "frame_end": float(action.frame_range[1]),
            }
            for action in actions
        ],
    }


def reset_principled(material):
    material.use_nodes = True
    material.node_tree.nodes.clear()
    output = material.node_tree.nodes.new("ShaderNodeOutputMaterial")
    output.location = (640, 0)
    bsdf = material.node_tree.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (340, 0)
    material.node_tree.links.new(bsdf.outputs["BSDF"], output.inputs["Surface"])
    bsdf.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    bsdf.inputs["Metallic"].default_value = 1.0
    bsdf.inputs["Roughness"].default_value = 1.0
    bsdf.inputs["Alpha"].default_value = 1.0
    material.diffuse_color = (1.0, 1.0, 1.0, 1.0)
    return bsdf


def load_image(path, color_space, cache):
    absolute = os.path.abspath(path)
    if not os.path.isfile(absolute):
        raise FileNotFoundError(absolute)
    key = (os.path.normcase(absolute), color_space)
    if key in cache:
        return cache[key]
    image = bpy.data.images.load(absolute, check_existing=False)
    image.filepath = absolute
    image.colorspace_settings.name = color_space
    cache[key] = image
    return image


def add_image_node(material, path, color_space, cache, location):
    node = material.node_tree.nodes.new("ShaderNodeTexImage")
    node.image = load_image(path, color_space, cache)
    node.label = os.path.basename(path)
    node.location = location
    return node


def configure_standard_pbr(material, texture_root, texture_key, cache):
    bsdf = reset_principled(material)
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    suffixes = {
        "base_color": "_BaseColor.png",
        "metallic": "_Metalness.png",
        "normal": "_Normal.png",
        "roughness": "_Roughness.png",
    }
    paths = {name: os.path.join(texture_root, texture_key + suffix)
             for name, suffix in suffixes.items()}

    base = add_image_node(material, paths["base_color"], "sRGB", cache, (-760, 240))
    metallic = add_image_node(material, paths["metallic"], "Non-Color", cache, (-760, 20))
    roughness = add_image_node(material, paths["roughness"], "Non-Color", cache, (-760, -180))
    normal = add_image_node(material, paths["normal"], "Non-Color", cache, (-760, -420))
    normal_map = nodes.new("ShaderNodeNormalMap")
    normal_map.location = (-180, -360)

    links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    links.new(metallic.outputs["Color"], bsdf.inputs["Metallic"])
    links.new(roughness.outputs["Color"], bsdf.inputs["Roughness"])
    links.new(normal.outputs["Color"], normal_map.inputs["Color"])
    links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
    material.use_backface_culling = True
    return paths


def configure_infinian(texture_root):
    cache = {}
    bindings = []
    for material in used_materials():
        name = material.name
        if name.startswith("Mon_Infinian_001_A_MI"):
            key = "Mon_Infinian_001_A"
        elif name.startswith("Mon_Infinian_001_B_MI"):
            key = "Mon_Infinian_001_B"
        else:
            raise RuntimeError("Unmapped Infinian material: " + name)

        bsdf = reset_principled(material)
        nodes = material.node_tree.nodes
        links = material.node_tree.links
        paths = {
            "base_color": os.path.join(texture_root, key + "_D.png"),
            "normal": os.path.join(texture_root, key + "_N.png"),
            "packed_mask": os.path.join(texture_root, key + "_M.png"),
        }
        base = add_image_node(material, paths["base_color"], "sRGB", cache, (-760, 220))
        mask = add_image_node(material, paths["packed_mask"], "Non-Color", cache, (-760, -40))
        normal = add_image_node(material, paths["normal"], "Non-Color", cache, (-760, -340))
        separate = nodes.new("ShaderNodeSeparateColor")
        separate.location = (-430, -40)
        normal_map = nodes.new("ShaderNodeNormalMap")
        normal_map.location = (-180, -340)

        links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
        links.new(mask.outputs["Color"], separate.inputs["Color"])
        links.new(separate.outputs["Red"], bsdf.inputs["Metallic"])
        links.new(separate.outputs["Green"], bsdf.inputs["Roughness"])
        links.new(normal.outputs["Color"], normal_map.inputs["Color"])
        links.new(normal_map.outputs["Normal"], bsdf.inputs["Normal"])
        # Hair, cloth, and ornaments share these source materials.  Export them
        # double-sided so the engine sees authored backfaces rather than Blender
        # silently culling them during validation.
        material.use_backface_culling = False
        bindings.append({
            "material": name,
            "texture_key": key,
            "textures": paths,
            "packed_mask_channels": {"red": "metallic", "green": "roughness"},
        })
    return bindings


def configure_sponza(texture_root):
    cache = {}
    bindings = []
    for material in used_materials():
        name = material.name
        if name in SPONZA_TEXTURE_KEYS:
            texture_key = SPONZA_TEXTURE_KEYS[name]
            paths = configure_standard_pbr(material, texture_root, texture_key, cache)
            bindings.append({
                "material": name,
                "texture_key": texture_key,
                "textures": paths,
            })
            continue

        bsdf = reset_principled(material)
        paths = {}
        if name == "dirt_decal":
            paths = {
                "base_color": os.path.join(texture_root, "dirt_decal_01.png"),
                "alpha": os.path.join(texture_root, "dirt_decal_01_alpha.png"),
            }
            base = add_image_node(material, paths["base_color"], "sRGB", cache, (-620, 100))
            alpha = add_image_node(material, paths["alpha"], "Non-Color", cache, (-620, -140))
            material.node_tree.links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
            material.node_tree.links.new(alpha.outputs["Color"], bsdf.inputs["Alpha"])
            bsdf.inputs["Metallic"].default_value = 0.0
            bsdf.inputs["Roughness"].default_value = 0.9
            material.surface_render_method = "DITHERED"
            material.alpha_threshold = 0.05
            material.use_backface_culling = False
        elif name in {"glass", "lamp_glass_01"}:
            alpha = 0.22 if name == "glass" else 0.35
            bsdf.inputs["Base Color"].default_value = (0.55, 0.68, 0.72, 1.0)
            bsdf.inputs["Metallic"].default_value = 0.0
            bsdf.inputs["Roughness"].default_value = 0.12
            bsdf.inputs["Alpha"].default_value = alpha
            material.diffuse_color = (0.55, 0.68, 0.72, alpha)
            material.surface_render_method = "BLENDED"
            material.use_backface_culling = False
        elif name == "light_bulb":
            bsdf.inputs["Base Color"].default_value = (1.0, 0.78, 0.42, 1.0)
            bsdf.inputs["Metallic"].default_value = 0.0
            bsdf.inputs["Roughness"].default_value = 0.25
            bsdf.inputs["Emission Color"].default_value = (1.0, 0.58, 0.18, 1.0)
            bsdf.inputs["Emission Strength"].default_value = 4.0
            material.use_backface_culling = False
        else:
            raise RuntimeError("Unmapped Sponza material: " + name)
        bindings.append({"material": name, "texture_key": None, "textures": paths})
    return bindings


def save_blend(path):
    result = bpy.ops.wm.save_as_mainfile(
        filepath=os.path.abspath(path),
        compress=False,
        relative_remap=False,
    )
    if "FINISHED" not in result:
        raise RuntimeError("Blender file save failed: " + path)


def export_glb(path):
    result = bpy.ops.export_scene.gltf(
        filepath=os.path.abspath(path),
        export_format="GLB",
        use_selection=False,
        use_visible=False,
        export_apply=False,
        export_yup=True,
        export_materials="EXPORT",
        export_normals=True,
        # The source FBX tangent layer is not a usable MikkTSpace basis (the
        # Infinian import has tangents parallel to normals on most vertices).
        # Omitting TANGENT does not alter positions/topology; it deliberately
        # selects CreatorEngine's existing MikkTSpace generation path.
        export_tangents=False,
        export_texcoords=True,
        export_skins=True,
        export_all_influences=True,
        export_morph=True,
        export_morph_normal=True,
        export_morph_tangent=True,
        export_animations=True,
        export_animation_mode="ACTIONS",
        export_force_sampling=True,
        export_frame_step=1,
        export_optimize_animation_size=False,
        export_try_sparse_sk=False,
        export_shared_accessors=False,
        export_image_format="AUTO",
        export_draco_mesh_compression_enable=False,
        export_use_gltfpack=False,
    )
    if "FINISHED" not in result:
        raise RuntimeError("GLB export failed: " + path)


def read_glb_summary(path):
    with open(path, "rb") as stream:
        magic, version, total_length = struct.unpack("<III", stream.read(12))
        if magic != 0x46546C67 or version != 2:
            raise RuntimeError("Not a glTF 2 GLB: " + path)
        json_length, json_type = struct.unpack("<II", stream.read(8))
        if json_type != 0x4E4F534A:
            raise RuntimeError("GLB JSON chunk is missing: " + path)
        document = json.loads(stream.read(json_length).decode("utf-8").rstrip(" \t\r\n\0"))
    actual_length = os.path.getsize(path)
    if total_length != actual_length:
        raise RuntimeError("GLB declared size does not match file size: " + path)

    extensions = set(document.get("extensionsUsed", []))
    forbidden = sorted(extensions.intersection(FORBIDDEN_EXTENSIONS))
    if forbidden:
        raise RuntimeError("Compressed/quantized GLB extensions found: " + ", ".join(forbidden))

    accessors = document.get("accessors", [])
    vertex_count = 0
    index_count = 0
    position_component_types = set()
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            position_index = primitive.get("attributes", {}).get("POSITION")
            if position_index is not None:
                accessor = accessors[position_index]
                vertex_count += int(accessor.get("count", 0))
                position_component_types.add(int(accessor.get("componentType", 0)))
            index_index = primitive.get("indices")
            if index_index is not None:
                index_count += int(accessors[index_index].get("count", 0))

    return {
        "bytes": actual_length,
        "sha256": sha256(path),
        "extensions_used": sorted(extensions),
        "forbidden_extensions": forbidden,
        "meshes": len(document.get("meshes", [])),
        "primitives": sum(len(mesh.get("primitives", [])) for mesh in document.get("meshes", [])),
        "vertices": vertex_count,
        "indices": index_count,
        "position_component_types": sorted(position_component_types),
        "materials": len(document.get("materials", [])),
        "textures": len(document.get("textures", [])),
        "images": len(document.get("images", [])),
        "skins": len(document.get("skins", [])),
        "joints": sum(len(skin.get("joints", [])) for skin in document.get("skins", [])),
        "animations": len(document.get("animations", [])),
        "animation_channels": sum(
            len(animation.get("channels", [])) for animation in document.get("animations", [])),
    }


def prepare_asset(name, fbx_path, texture_root, out_dir, material_builder):
    clear_all_data()
    print("[PHASE17] import " + name + " <- " + fbx_path, flush=True)
    import_fbx(fbx_path)
    source_scene = scene_summary()
    material_bindings = material_builder(texture_root)
    after_materials = scene_summary()
    for key in ("meshes", "source_vertices", "source_loops", "source_polygons", "bones"):
        if source_scene[key] != after_materials[key]:
            raise RuntimeError("Material authoring changed geometry field: " + key)

    blend_path = os.path.join(out_dir, "Phase17_" + name + ".blend")
    glb_path = os.path.join(out_dir, "Phase17_" + name + ".glb")
    save_blend(blend_path)
    print("[PHASE17] export " + name + " -> " + glb_path, flush=True)
    export_glb(glb_path)
    glb = read_glb_summary(glb_path)
    print("[PHASE17] {} meshes={} vertices={} polygons={} materials={} animations={} glbBytes={}".format(
        name, source_scene["meshes"], source_scene["source_vertices"],
        source_scene["source_polygons"], source_scene["materials"],
        len(source_scene["actions"]), glb["bytes"]), flush=True)

    texture_paths = sorted({os.path.abspath(path)
                            for binding in material_bindings
                            for path in binding["textures"].values()})
    return {
        "name": name,
        "source": {
            "fbx": os.path.abspath(fbx_path),
            "fbx_bytes": os.path.getsize(fbx_path),
            "fbx_sha256": sha256(fbx_path),
            "textures": [
                {"path": path, "bytes": os.path.getsize(path), "sha256": sha256(path)}
                for path in texture_paths
            ],
        },
        "scene": source_scene,
        "material_bindings": material_bindings,
        "blend": {
            "path": os.path.abspath(blend_path),
            "bytes": os.path.getsize(blend_path),
            "sha256": sha256(blend_path),
            "compressed": False,
        },
        "glb": {"path": os.path.abspath(glb_path), **glb},
    }


def main():
    argv = sys.argv
    argv = argv[argv.index("--") + 1:] if "--" in argv else []
    parser = argparse.ArgumentParser(description="Prepare PHASE 17 local model validation assets")
    parser.add_argument("--infinian-root", required=True)
    parser.add_argument("--sponza-root", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args(argv)

    infinian_root = os.path.abspath(args.infinian_root)
    sponza_root = os.path.abspath(args.sponza_root)
    out_dir = os.path.abspath(args.out)
    os.makedirs(out_dir, exist_ok=True)
    # The output directory is reproducible generated state.  Blender's normal
    # interactive .blend1 backup would retain a previous, potentially stale
    # material graph beside the accepted corpus.
    bpy.context.preferences.filepaths.save_version = 0

    infinian_fbx = os.path.join(
        infinian_root, "source", "Infinian", "Mon_Infinian_001_Skeleton.FBX")
    sponza_fbx = os.path.join(sponza_root, "source", "Sponza.fbx")
    for path in (infinian_fbx, sponza_fbx):
        if not os.path.isfile(path):
            raise FileNotFoundError(path)

    assets = [
        prepare_asset(
            "Infinian", infinian_fbx, os.path.join(infinian_root, "textures"),
            out_dir, configure_infinian),
        prepare_asset(
            "Sponza", sponza_fbx, os.path.join(sponza_root, "textures"),
            out_dir, configure_sponza),
    ]
    manifest = {
        "schema": 1,
        "purpose": "CreatorEngine PHASE 17 local high-fidelity validation corpus",
        "blender_version": bpy.app.version_string,
        "export_policy": {
            "geometry_modifiers_applied": False,
            "weld_vertices": False,
            "vertex_cache_optimization": False,
            "mesh_decimation": False,
            "draco_compression": False,
            "gltfpack": False,
            "mesh_quantization": False,
            "sparse_accessors": False,
            "animation_key_reduction": False,
            "animation_frame_step": 1,
            "source_tangents_exported": False,
            "engine_mikktspace_generation": True,
            "blend_file_compression": False,
            "image_format": "AUTO (source PNG, lossless generated metallic-roughness packing)",
        },
        "assets": assets,
    }
    manifest_path = os.path.join(out_dir, "phase17-model-corpus.json")
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    print("[PHASE17] manifest -> " + manifest_path, flush=True)


if __name__ == "__main__":
    main()
