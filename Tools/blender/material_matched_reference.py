"""Supplement MAT-0 with matched punctual-light and uniform furnace references.

The original area-light golden is never modified. Run in Blender 5.1.1.
"""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from material_reference import CASES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=1024)
    parser.add_argument('--seed', type=int, default=0)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if bpy.app.version != (5, 1, 1) or args.samples < 1:
        raise RuntimeError('Requires pinned Blender 5.1.1 and a positive sample count')
    output = args.output.resolve()
    if output.exists():
        raise RuntimeError('Reference output must be new')
    output.mkdir(parents=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = args.samples
    scene.cycles.use_denoising = False
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.seed = args.seed
    # Match single-sample raster evaluation at pixel centers. The original
    # area-light reference keeps its normal reconstruction filter.
    # Cycles forces BOX width to one regardless of the RNA width value.
    scene.cycles.pixel_filter_type = 'GAUSSIAN'
    scene.cycles.filter_width = .01
    scene.cycles.max_bounces = 1
    scene.cycles.diffuse_bounces = 1
    scene.cycles.glossy_bounces = 1
    scene.cycles.transmission_bounces = 0
    scene.render.resolution_x = scene.render.resolution_y = 64
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.render.image_settings.color_depth = '32'
    scene.view_settings.view_transform = 'Raw'
    scene.view_settings.look = 'None'
    scene.view_settings.exposure = 0
    scene.view_settings.gamma = 1
    camera_data = bpy.data.cameras.new('Matched Perspective')
    camera_data.angle = math.pi / 4
    camera_data.sensor_fit = 'HORIZONTAL'
    camera = bpy.data.objects.new('Matched Perspective', camera_data)
    scene.collection.objects.link(camera)
    camera.location = (0, 0, 3)
    scene.camera = camera
    sun_data = bpy.data.lights.new('Matched Sun', 'SUN')
    sun_data.energy = 1
    sun_data.angle = 0
    sun_data.use_shadow = False
    sun = bpy.data.objects.new('Matched Sun', sun_data)
    scene.collection.objects.link(sun)
    to_light = Vector((.35, -.2, .8)).normalized()
    sun.rotation_euler = (-to_light).to_track_quat('-Z', 'Y').to_euler()
    world = bpy.data.worlds.new('Matched World')
    world.use_nodes = True
    scene.world = world
    background = world.node_tree.nodes.get('Background')
    background.inputs['Color'].default_value = (1, 1, 1, 1)
    bpy.ops.import_scene.gltf(filepath=str(ROOT / 'Dynamic_CPP/Assets/Models/Prim_Sphere.glb'))
    obj = next(o for o in scene.objects if o.type == 'MESH')
    mesh = obj.data
    # glTF importer conversion is included in the shared exported geometry.
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    mesh.calc_loop_triangles()
    mesh.calc_tangents(uvmap=mesh.uv_layers.active.name)
    vertices = []
    for triangle in mesh.loop_triangles:
        for loop_index in triangle.loops:
            loop = mesh.loops[loop_index]
            position = mesh.vertices[loop.vertex_index].co
            normal = mesh.corner_normals[loop_index].vector
            tangent = loop.tangent
            uv = mesh.uv_layers.active.data[loop_index].uv
            vertices.extend((*position, *normal, *tangent, loop.bitangent_sign, *uv))
    geometry = struct.pack('<I', len(vertices) // 12) + struct.pack('<%sf' % len(vertices), *vertices)
    (output / 'sphere.bin').write_bytes(geometry)
    records = []
    definitions = [(name, values) for name, values in CASES[:10]]
    definitions += [('control_emission', {'Base Color': (0, 0, 0, 1), 'Specular IOR Level': 0,
                    'Emission Color': (.3, .55, 1, 1), 'Emission Strength': 5})]
    definitions += [('control_white', {'Base Color': (1, 1, 1, 1), 'IOR': 1, 'Roughness': 1})]
    for mode in ('sun', 'furnace'):
        sun.hide_render = mode != 'sun'
        background.inputs['Strength'].default_value = 1 if mode == 'furnace' else 0
        for name, values in definitions:
            material = bpy.data.materials.new(name + '-' + mode)
            material.use_nodes = True
            principled = material.node_tree.nodes.get('Principled BSDF')
            numeric = {k: v for k, v in values.items() if k != 'normal_map'}
            for key, value in numeric.items():
                principled.inputs[key].default_value = value
            if numeric.get('Anisotropic', 0) > 0:
                tangent = material.node_tree.nodes.new('ShaderNodeTangent')
                tangent.direction_type = 'UV_MAP'
                tangent.uv_map = mesh.uv_layers.active.name
                material.node_tree.links.new(tangent.outputs['Tangent'], principled.inputs['Tangent'])
            mesh.materials.clear()
            mesh.materials.append(material)
            stem = mode + '-' + name
            scene.render.filepath = str(output / (stem + '.exr'))
            bpy.ops.render.render(write_still=True)
            image = bpy.data.images.load(scene.render.filepath, check_existing=False)
            pixels = array('f', [0]) * (64 * 64 * 4)
            image.pixels.foreach_get(pixels)
            if sys.byteorder != 'little':
                pixels.byteswap()
            (output / (stem + '.f32')).write_bytes(pixels.tobytes())
            bpy.data.images.remove(image)
            # Human-readable source of truth consumed by the native graph builder.
            with (output / (stem + '.inputs')).open('w', encoding='utf-8') as stream:
                for key, value in numeric.items():
                    components = list(value) if isinstance(value, (list, tuple)) else [value]
                    stream.write(json.dumps(key) + ' ' + str(len(components)) + ' ' +
                                 ' '.join(str(v) for v in components) + '\n')
            records.append({'id': stem, 'inputs': numeric, 'normal_map': False,
                            'texture': False, 'reference_sha256': hashlib.sha256(pixels.tobytes()).hexdigest()})
    # Geometry/view diagnostics identify setup drift before a BRDF is blamed.
    for source_socket, stem in (('Normal', 'debug-normal'), ('Incoming', 'debug-view')):
        material = bpy.data.materials.new(stem)
        material.use_nodes = True
        tree = material.node_tree
        tree.nodes.clear()
        geometry_node = tree.nodes.new('ShaderNodeNewGeometry')
        scale = tree.nodes.new('ShaderNodeVectorMath')
        scale.operation = 'SCALE'
        scale.inputs[3].default_value = .5
        add = tree.nodes.new('ShaderNodeVectorMath')
        add.operation = 'ADD'
        add.inputs[1].default_value = (.5, .5, .5)
        emission = tree.nodes.new('ShaderNodeEmission')
        material_output = tree.nodes.new('ShaderNodeOutputMaterial')
        tree.links.new(geometry_node.outputs[source_socket], scale.inputs[0])
        tree.links.new(scale.outputs['Vector'], add.inputs[0])
        tree.links.new(add.outputs['Vector'], emission.inputs['Color'])
        tree.links.new(emission.outputs['Emission'], material_output.inputs['Surface'])
        mesh.materials.clear()
        mesh.materials.append(material)
        scene.render.filepath = str(output / (stem + '.exr'))
        bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(scene.render.filepath, check_existing=False)
        pixels = array('f', [0]) * (64 * 64 * 4)
        image.pixels.foreach_get(pixels)
        if sys.byteorder != 'little':
            pixels.byteswap()
        (output / (stem + '.f32')).write_bytes(pixels.tobytes())
        bpy.data.images.remove(image)
    manifest = {'schema': 'creator.material.matched-reference.v1', 'blender': bpy.app.version_string,
                'renderer': 'CYCLES', 'resolution': [64, 64], 'samples': args.samples, 'seed': args.seed,
                'pixel_filter': {'type': 'GAUSSIAN', 'width': .01}, 'anisotropy_tangent': 'explicit active UV tangent',
                'camera': {'eye': [0, 0, 3], 'vertical_fov': math.pi / 4, 'near': .1, 'far': 10},
                'sun': {'to_light': list(to_light), 'irradiance': [1, 1, 1], 'angle': 0},
                'furnace': {'radiance': [1, 1, 1]}, 'bounces': {'max': 1, 'diffuse': 1, 'glossy': 1},
                'exclusions': ['texture/normal-map filtering', 'special transport', 'original area-light/HDRI grid'],
                'geometry_sha256': hashlib.sha256(geometry).hexdigest(), 'cases': records}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('MAT9_MATCHED_REFERENCE_OK cases=' + str(len(records)))


if __name__ == '__main__':
    main()
