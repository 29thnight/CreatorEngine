"""Create the fixed Blender 5.1.1 material reference for PHASE 4.25 MAT-0.

Run from the repository root:
    blender --background --factory-startup --python Tools/blender/material_reference.py -- \
        --output Tools/blender/fixtures/material-reference-5.1.1

The OpenEXR is the scene-linear reference. The PNG is a display-only preview.
"""

import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib

import bpy
from bpy_extras.object_utils import world_to_camera_view
from mathutils import Vector


ROOT = Path(__file__).resolve().parents[2]
MESH_PATH = ROOT / "Dynamic_CPP/Assets/Models/Prim_Sphere.glb"
HDRI_PATH = ROOT / "Dynamic_CPP/Assets/HDR/autumn_field_puresky_1k.hdr"
WIDTH = 800
HEIGHT = 480
SAMPLES = 32
X_POSITIONS = (-3.4, -1.7, 0.0, 1.7, 3.4)
Y_POSITIONS = (1.65, 0.0, -1.65)

CASES = (
    ("core_base", {"Base Color": (0.85, 0.42, 0.24, 1.0), "Roughness": 0.45}),
    ("core_metal", {"Base Color": (0.8, 0.7, 0.45, 1.0), "Metallic": 1.0, "Roughness": 0.18}),
    ("core_rough", {"Base Color": (0.72, 0.7, 0.68, 1.0), "Roughness": 0.88}),
    ("core_normal", {"Base Color": (0.75, 0.75, 0.75, 1.0), "Roughness": 0.42, "normal_map": True}),
    ("core_emission", {"Base Color": (0.1, 0.1, 0.1, 1.0), "Emission Color": (0.3, 0.55, 1.0, 1.0),
                       "Emission Strength": 5.0}),
    ("layer_coat", {"Base Color": (0.55, 0.25, 0.18, 1.0), "Roughness": 0.35,
                    "Coat Weight": 0.85, "Coat Roughness": 0.08}),
    ("layer_sheen", {"Base Color": (0.3, 0.52, 0.66, 1.0), "Roughness": 0.55,
                     "Sheen Weight": 0.9, "Sheen Tint": (0.3, 0.9, 0.8, 1.0)}),
    ("layer_anisotropic", {"Base Color": (0.76, 0.62, 0.38, 1.0), "Metallic": 1.0,
                           "Roughness": 0.3, "Anisotropic": 0.8}),
    ("layer_thin_film", {"Base Color": (0.32, 0.42, 0.7, 1.0), "Metallic": 0.6,
                         "Coat Weight": 0.7, "Thin Film Thickness": 550.0, "Thin Film IOR": 1.4}),
    ("layer_mixed", {"Base Color": (0.55, 0.48, 0.35, 1.0), "Roughness": 0.32,
                     "Coat Weight": 0.5, "Sheen Weight": 0.45, "Anisotropic": 0.35}),
    ("special_transmission", {"Base Color": (0.72, 0.85, 0.95, 1.0), "Roughness": 0.12,
                              "IOR": 1.45, "Transmission Weight": 0.9}),
    ("special_subsurface", {"Base Color": (0.86, 0.28, 0.22, 1.0), "Roughness": 0.5,
                            "Subsurface Weight": 0.8, "Subsurface Scale": 0.18}),
    ("special_alpha", {"Base Color": (0.2, 0.7, 0.45, 1.0), "Roughness": 0.35, "Alpha": 0.4}),
    ("special_volume", {"Base Color": (0.3, 0.5, 0.85, 1.0), "Roughness": 0.5,
                        "volume_density": 0.18}),
    ("special_transmission_alpha", {"Base Color": (0.8, 0.5, 0.3, 1.0), "Roughness": 0.2,
                                    "Transmission Weight": 0.5, "Alpha": 0.6}),
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_png(path, normal=False):
    def chunk(kind, payload):
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    rows = bytearray()
    for y in range(64):
        rows.append(0)
        for x in range(64):
            if normal:
                value = (128 + round(25 * math.sin(x * 0.24)),
                         128 + round(25 * math.cos(y * 0.24)), 246, 255)
            else:
                value = (220, 104, 68, 255) if (x // 8 + y // 8) % 2 == 0 else (55, 143, 207, 255)
            rows.extend(value)
    header = b"\x89PNG\r\n\x1a\n"
    image = header + chunk(b"IHDR", struct.pack(">IIBBBBB", 64, 64, 8, 6, 0, 0, 0))
    image += chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")
    path.write_bytes(image)


def new_material(case_id, values, base_image, normal_image):
    material = bpy.data.materials.new(case_id)
    material.use_nodes = True
    material.diffuse_color = values["Base Color"]
    tree = material.node_tree
    tree.nodes.clear()
    output = tree.nodes.new("ShaderNodeOutputMaterial")
    principled = tree.nodes.new("ShaderNodeBsdfPrincipled")
    tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])

    texture = tree.nodes.new("ShaderNodeTexImage")
    texture.image = base_image
    texture.interpolation = "Linear"
    tint = tree.nodes.new("ShaderNodeMixRGB")
    tint.blend_type = "MULTIPLY"
    tint.inputs[0].default_value = 1.0
    tint.inputs[2].default_value = values["Base Color"]
    tree.links.new(texture.outputs["Color"], tint.inputs[1])
    tree.links.new(tint.outputs["Color"], principled.inputs["Base Color"])

    for name, value in values.items():
        if name not in ("Base Color", "normal_map", "volume_density"):
            principled.inputs[name].default_value = value

    if values.get("normal_map"):
        normal_texture = tree.nodes.new("ShaderNodeTexImage")
        normal_texture.image = normal_image
        normal_map = tree.nodes.new("ShaderNodeNormalMap")
        tree.links.new(normal_texture.outputs["Color"], normal_map.inputs["Color"])
        tree.links.new(normal_map.outputs["Normal"], principled.inputs["Normal"])

    if "Alpha" in values:
        material.surface_render_method = "DITHERED"
    if "volume_density" in values:
        volume = tree.nodes.new("ShaderNodeVolumePrincipled")
        volume.inputs["Color"].default_value = (0.35, 0.55, 0.9, 1.0)
        volume.inputs["Density"].default_value = values["volume_density"]
        tree.links.new(volume.outputs["Volume"], output.inputs["Volume"])
    return material


def make_scene(output):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.eevee.taa_render_samples = SAMPLES
    scene.eevee.use_raytracing = False
    scene.render.resolution_x = WIDTH
    scene.render.resolution_y = HEIGHT
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "32"
    scene.render.image_settings.exr_codec = "ZIP"
    scene.view_settings.view_transform = "Raw"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0

    write_png(output / "base-checker.png")
    write_png(output / "normal-wave.png", normal=True)
    base_image = bpy.data.images.load(str(output / "base-checker.png"))
    base_image.colorspace_settings.name = "sRGB"
    normal_image = bpy.data.images.load(str(output / "normal-wave.png"))
    normal_image.colorspace_settings.name = "Non-Color"

    world = bpy.data.worlds.new("Fixed HDRI")
    world.use_nodes = True
    scene.world = world
    nodes = world.node_tree.nodes
    nodes.clear()
    environment = nodes.new("ShaderNodeTexEnvironment")
    environment.image = bpy.data.images.load(str(HDRI_PATH))
    background = nodes.new("ShaderNodeBackground")
    background.inputs["Strength"].default_value = 0.35
    world_output = nodes.new("ShaderNodeOutputWorld")
    world.node_tree.links.new(environment.outputs["Color"], background.inputs["Color"])
    world.node_tree.links.new(background.outputs["Background"], world_output.inputs["Surface"])

    light_data = bpy.data.lights.new("Fixed Area", "AREA")
    light_data.energy = 850.0
    light_data.shape = "DISK"
    light_data.size = 4.0
    light_data.use_shadow = False
    light = bpy.data.objects.new("Fixed Area", light_data)
    scene.collection.objects.link(light)
    light.location = (-3.0, 4.0, 5.0)

    camera_data = bpy.data.cameras.new("Orthographic Camera")
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 10.0
    camera = bpy.data.objects.new("Orthographic Camera", camera_data)
    scene.collection.objects.link(camera)
    camera.location = (0.0, 0.0, 10.0)
    scene.camera = camera

    bpy.ops.import_scene.gltf(filepath=str(MESH_PATH))
    spheres = [item for item in scene.objects if item.type == "MESH"]
    if len(spheres) != 1 or len(spheres[0].data.uv_layers) != 1:
        raise RuntimeError("Prim_Sphere.glb did not yield one UV sphere")
    template = spheres[0]
    mesh = template.data
    mesh.materials.clear()
    mesh.materials.append(None)

    records = []
    for index, (case_id, values) in enumerate(CASES):
        obj = template if index == 0 else bpy.data.objects.new(case_id, mesh)
        if index != 0:
            scene.collection.objects.link(obj)
        obj.name = case_id
        x = X_POSITIONS[index % 5]
        y = Y_POSITIONS[index // 5]
        obj.location = (x, y, 0.0)
        obj.material_slots[0].link = "OBJECT"
        obj.material_slots[0].material = new_material(case_id, values, base_image, normal_image)
        records.append({"id": case_id, "position": [x, y, 0.0], "inputs": values})

    bpy.ops.file.pack_all()
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "material-grid.blend"))
    return scene, records


def read_pixels(path):
    image = bpy.data.images.load(str(path), check_existing=False)
    try:
        if (image.size[0], image.size[1]) != (WIDTH, HEIGHT) or image.channels != 4:
            raise RuntimeError("Unexpected EXR dimensions or channels")
        pixels = array("f", [0.0]) * (WIDTH * HEIGHT * 4)
        image.pixels.foreach_get(pixels)
        if not all(math.isfinite(value) for value in pixels):
            raise RuntimeError("Non-finite Blender reference pixel")
        return pixels
    finally:
        bpy.data.images.remove(image)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare-to", type=Path)
    args = parser.parse_args(argv)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    scene, records = make_scene(output)
    golden = output / "material-grid-linear.exr"
    scene.render.filepath = str(golden)
    bpy.ops.render.render(write_still=True)
    pixels = read_pixels(golden)

    hdr_max = max(pixels[index] for index in range(0, len(pixels), 4))
    if hdr_max <= 1.0:
        raise RuntimeError("Reference has no value above display white")
    for record in records:
        projection = world_to_camera_view(scene, scene.camera, Vector(record["position"]))
        px = round(projection.x * WIDTH)
        py = round(projection.y * HEIGHT)
        samples = []
        for y in range(py - 3, py + 4):
            for x in range(px - 3, px + 4):
                if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
                    raise RuntimeError("Reference patch is outside the image")
                offset = (y * WIDTH + x) * 4
                samples.append(pixels[offset:offset + 4])
        record["center_uv"] = [round(projection.x, 6), round(projection.y, 6)]
        record["center_rgba_mean"] = [round(sum(sample[channel] for sample in samples) / len(samples), 7)
                                      for channel in range(4)]
        if record["center_rgba_mean"][3] < 0.05:
            raise RuntimeError("Reference patch missed " + record["id"])

    comparison = None
    if args.compare_to:
        baseline = read_pixels(args.compare_to.resolve())
        maximum = max(abs(a - b) for a, b in zip(baseline, pixels))
        comparison = {"path": str(args.compare_to.resolve()), "max_abs_rgba": maximum}
        if maximum > 0.002:
            raise RuntimeError(f"Blender rerender drift: {maximum}")

    scene.view_settings.view_transform = "AgX"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    bpy.data.images["Render Result"].save_render(str(output / "material-grid-preview.png"), scene=scene)

    manifest = {
        "schema": "creator.material.blender-reference.v1",
        "blender_version": bpy.app.version_string,
        "blender_hash": bpy.app.build_hash.decode("ascii"),
        "renderer": "BLENDER_EEVEE",
        "reference": "scene-linear pre-view-transform RGBA32F OpenEXR",
        "preview": "AgX display only; never compare as material golden",
        "resolution": [WIDTH, HEIGHT],
        "render_samples": SAMPLES,
        "ray_tracing": False,
        "film_transparent": True,
        "mesh": {"path": str(MESH_PATH.relative_to(ROOT)).replace("\\", "/"), "sha256": sha256(MESH_PATH)},
        "hdri": {"path": str(HDRI_PATH.relative_to(ROOT)).replace("\\", "/"), "sha256": sha256(HDRI_PATH),
                 "strength": 0.35},
        "light": {"type": "AREA", "energy": 850.0, "shape": "DISK", "size": 4.0,
                  "position": [-3.0, 4.0, 5.0], "shadows": False},
        "camera": {"type": "ORTHO", "scale": 10.0, "position": [0.0, 0.0, 10.0]},
        "cases": records,
        "source_sha256": sha256(Path(__file__)),
        "files": {name: sha256(output / name) for name in (
            "base-checker.png", "normal-wave.png", "material-grid.blend", "material-grid-linear.exr",
            "material-grid-preview.png")},
        "max_red_linear": hdr_max,
        "rerender_comparison": comparison,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("MAT0_REFERENCE_OK", golden, "max_red", hdr_max, "compare", comparison)


if __name__ == "__main__":
    main()
