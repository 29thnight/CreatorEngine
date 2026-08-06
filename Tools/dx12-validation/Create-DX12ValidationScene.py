import argparse
import math
from pathlib import Path

import bpy
from mathutils import Vector


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--blend", required=True)
    parser.add_argument("--glb", required=True)
    argv = []
    if "--" in __import__("sys").argv:
        argv = __import__("sys").argv[__import__("sys").argv.index("--") + 1 :]
    return parser.parse_args(argv)


def make_material(name, color, metallic, roughness):
    material = bpy.data.materials.new(name=name)
    material.use_nodes = True
    material.diffuse_color = (*color, 1.0)
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1.0)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    return material


def assign_material(obj, material, smooth=False):
    obj.data.materials.append(material)
    if smooth:
        for polygon in obj.data.polygons:
            polygon.use_smooth = True


def point_at(obj, target):
    direction = Vector(target) - obj.location
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def main():
    args = parse_args()
    blend_path = Path(args.blend).resolve()
    glb_path = Path(args.glb).resolve()
    blend_path.parent.mkdir(parents=True, exist_ok=True)
    glb_path.parent.mkdir(parents=True, exist_ok=True)

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for datablocks in (bpy.data.meshes, bpy.data.curves, bpy.data.materials,
                       bpy.data.cameras, bpy.data.lights):
        for datablock in list(datablocks):
            if datablock.users == 0:
                datablocks.remove(datablock)

    red = make_material("Mat_Red_Rough", (0.75, 0.06, 0.04), 0.0, 0.82)
    blue = make_material("Mat_Blue_Metal", (0.04, 0.16, 0.80), 0.85, 0.18)
    green = make_material("Mat_Green_Mid", (0.05, 0.62, 0.16), 0.15, 0.42)
    yellow = make_material("Mat_Yellow_Rough", (0.95, 0.55, 0.03), 0.0, 0.62)
    purple = make_material("Mat_Purple_Metal", (0.42, 0.06, 0.68), 0.65, 0.24)
    ground = make_material("Mat_Ground", (0.18, 0.20, 0.23), 0.0, 0.92)

    meshes = []

    bpy.ops.mesh.primitive_cube_add(location=(-4.8, 0.0, 1.1), scale=(1.1, 1.1, 1.1))
    cube = bpy.context.object
    cube.name = "Probe_Cube"
    assign_material(cube, red)
    meshes.append(cube)

    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, location=(-2.4, 0.0, 1.15), radius=1.15)
    sphere = bpy.context.object
    sphere.name = "Probe_Sphere"
    assign_material(sphere, blue, smooth=True)
    meshes.append(sphere)

    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=1.0, depth=2.4, location=(0.0, 0.0, 1.2))
    cylinder = bpy.context.object
    cylinder.name = "Probe_Cylinder"
    assign_material(cylinder, green, smooth=True)
    meshes.append(cylinder)

    bpy.ops.mesh.primitive_cone_add(vertices=32, radius1=1.15, radius2=0.0, depth=2.6, location=(2.5, 0.0, 1.3))
    cone = bpy.context.object
    cone.name = "Probe_Cone"
    assign_material(cone, yellow, smooth=True)
    meshes.append(cone)

    bpy.ops.mesh.primitive_torus_add(major_segments=48, minor_segments=16,
                                    major_radius=0.9, minor_radius=0.32,
                                    location=(5.0, 0.0, 1.25),
                                    rotation=(math.radians(90.0), 0.0, 0.0))
    torus = bpy.context.object
    torus.name = "Probe_Torus"
    assign_material(torus, purple, smooth=True)
    meshes.append(torus)

    bpy.ops.mesh.primitive_plane_add(size=18.0, location=(0.0, 0.0, 0.0))
    plane = bpy.context.object
    plane.name = "Probe_Ground"
    assign_material(plane, ground)
    meshes.append(plane)

    for obj in meshes:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    bpy.ops.object.camera_add(location=(10.5, -15.5, 8.5))
    camera = bpy.context.object
    camera.name = "Validation_Camera"
    camera.data.lens = 48.0
    point_at(camera, (0.0, 0.0, 1.0))
    bpy.context.scene.camera = camera

    bpy.ops.object.light_add(type="AREA", location=(0.0, -2.5, 9.0))
    key_light = bpy.context.object
    key_light.name = "Validation_Key_Area"
    key_light.data.energy = 1400.0
    key_light.data.shape = "DISK"
    key_light.data.size = 6.0
    point_at(key_light, (0.0, 0.0, 0.0))

    bpy.ops.object.light_add(type="AREA", location=(-7.0, 3.0, 4.5))
    fill_light = bpy.context.object
    fill_light.name = "Validation_Fill_Area"
    fill_light.data.energy = 700.0
    fill_light.data.color = (0.25, 0.45, 1.0)
    fill_light.data.size = 4.0
    point_at(fill_light, (-1.0, 0.0, 1.0))

    bpy.context.scene.world.color = (0.015, 0.02, 0.035)
    bpy.context.scene.render.resolution_x = 1280
    bpy.context.scene.render.resolution_y = 720
    bpy.context.scene.render.resolution_percentage = 100

    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))

    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.export_scene.gltf(
        filepath=str(glb_path),
        export_format="GLB",
        use_selection=True,
        export_apply=True,
        export_materials="EXPORT",
        export_cameras=False,
        export_lights=False,
    )
    print(f"DX12_VALIDATION_BLEND={blend_path}")
    print(f"DX12_VALIDATION_GLB={glb_path}")
    print(f"DX12_VALIDATION_MESHES={len(meshes)}")


if __name__ == "__main__":
    main()
