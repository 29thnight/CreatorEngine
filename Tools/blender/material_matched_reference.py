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
from thin_film_cases import CONTROLS, definitions as film_definitions
from special_material_cases import BOUNCES as SPECIAL_BOUNCES, TRANSPORT as SPECIAL_TRANSPORT, definitions as special_definitions
from special_material_cases import volume_definitions, surface_definitions


def definitions_for(suite):
    if suite == 'special-surface':
        return surface_definitions()
    if suite == 'special-volume':
        return volume_definitions()
    if suite == 'special':
        return special_definitions()
    if suite == 'thin-film':
        # Fixed before the measurements; every film has an identical film-off
        # control. This separates a film-specific change from the common BRDF.
        return film_definitions()
    return [(name, values) for name, values in CASES[:10]] + CONTROLS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=1024)
    parser.add_argument('--seed', type=int, default=0)
    parser.add_argument('--suite', choices=('core', 'thin-film', 'special', 'special-volume', 'special-surface'), default='core')
    parser.add_argument('--lighting', choices=('controls', 'hdri'), default='controls')
    parser.add_argument('--environment-config', type=Path)
    parser.add_argument('--only-case', action='append', default=[])
    parser.add_argument('--flat-special-diagnostic', action='store_true')
    parser.add_argument('--volume-shadow-diagnostic', action='store_true')
    parser.add_argument('--volume-bounces-diagnostic', type=int, choices=(0, 1))
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    is_special = args.suite in ('special','special-volume','special-surface')
    if (args.flat_special_diagnostic or args.volume_shadow_diagnostic or
            args.volume_bounces_diagnostic is not None) and not is_special:
        raise RuntimeError('Special geometry/lighting diagnostics require the Special suite')
    if is_special and args.lighting != 'controls':
        raise RuntimeError('Special suite currently fixes sun/furnace transport only')
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
    if is_special:
        scene.cycles.max_bounces = SPECIAL_BOUNCES['max']
        scene.cycles.transmission_bounces = SPECIAL_BOUNCES['transmission']
        scene.cycles.transparent_max_bounces = SPECIAL_BOUNCES['transparent']
        scene.cycles.volume_bounces = SPECIAL_BOUNCES['volume']
        if args.volume_bounces_diagnostic is not None:
            scene.cycles.volume_bounces = args.volume_bounces_diagnostic
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
    environments = None
    if args.lighting == 'hdri':
        if args.environment_config is None:
            raise RuntimeError('HDRI capture requires a frozen environment config')
        environments = json.loads(args.environment_config.read_text(encoding='utf-8'))
        if set(environments) != {'forest', 'autumn'}:
            raise RuntimeError('Requires the fixed forest/autumn environment pair')
        for record in environments.values():
            for path_key, hash_key in (('source', 'source_sha256'), ('cooked', 'cooked_sha256')):
                if hashlib.sha256(Path(record[path_key]).read_bytes()).hexdigest() != record[hash_key]:
                    raise RuntimeError('Environment artifact identity changed')
            if record['strength'] != .35:
                raise RuntimeError('Requires the original grid environment strength')
        # Shared geometry is already exported in Blender coordinates. Match the
        # engine's +Y-up, atan2(z,x) longitude without rotating that geometry.
        # Cycles bottom-up v = .5 + asin(z)/pi; engine top-down v = .5 - asin(y)/pi.
        world_geometry = world.node_tree.nodes.new('ShaderNodeNewGeometry')
        separate = world.node_tree.nodes.new('ShaderNodeSeparateXYZ')
        combine = world.node_tree.nodes.new('ShaderNodeCombineXYZ')
        negative = world.node_tree.nodes.new('ShaderNodeMath')
        negative.operation = 'MULTIPLY'
        negative.inputs[1].default_value = -1
        world.node_tree.links.new(world_geometry.outputs['Incoming'], separate.inputs[0])
        # Incoming is -ray direction: combine produces (d.x, -d.z, d.y).
        negate_x = world.node_tree.nodes.new('ShaderNodeMath')
        negate_x.operation = 'MULTIPLY'
        negate_x.inputs[1].default_value = -1
        world.node_tree.links.new(separate.outputs['X'], negate_x.inputs[0])
        world.node_tree.links.new(negate_x.outputs[0], combine.inputs['X'])
        world.node_tree.links.new(separate.outputs['Z'], combine.inputs['Y'])
        world.node_tree.links.new(separate.outputs['Y'], negative.inputs[0])
        world.node_tree.links.new(negative.outputs[0], combine.inputs['Z'])
        environment_node = world.node_tree.nodes.new('ShaderNodeTexEnvironment')
        environment_node.interpolation = 'Linear'
        world.node_tree.links.new(combine.outputs[0], environment_node.inputs['Vector'])
        world.node_tree.links.new(environment_node.outputs['Color'], background.inputs['Color'])
        scene.world.cycles.sampling_method = 'MANUAL'
        scene.world.cycles.sample_map_resolution = 1024
    if is_special:
        # The product rejects volume boundaries beyond 128 triangles. Use one
        # explicitly bounded geometry for every Special case and both engines.
        # The original 2208-triangle sphere is retained for Core/Layered suites.
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=.5)
        obj = bpy.context.object
        for polygon in obj.data.polygons:
            polygon.use_smooth = not args.flat_special_diagnostic
        uv_layer = obj.data.uv_layers.new(name='Special UV')
        for loop in obj.data.loops:
            direction = obj.data.vertices[loop.vertex_index].co.normalized()
            uv_layer.data[loop.index].uv = (.5+math.atan2(direction.y, direction.x)/(2*math.pi),
                                          .5+math.asin(direction.z)/math.pi)
    else:
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
    definitions = definitions_for(args.suite)
    if environments is not None:
        definitions += [('control_mirror', {'Base Color': (1, 1, 1, 1), 'Metallic': 1, 'Roughness': 0})]
    modes = ('forest', 'autumn') if environments is not None else ('sun', 'furnace')
    expected_cases = {mode+'-'+name for mode in modes for name,_ in definitions}
    if len(args.only_case) != len(set(args.only_case)) or not set(args.only_case) <= expected_cases:
        raise RuntimeError('Invalid diagnostic case subset')
    for mode in modes:
        sun.hide_render = mode != 'sun'
        background.inputs['Strength'].default_value = environments[mode]['strength'] if environments else (1 if mode == 'furnace' else 0)
        if environments is not None:
            environment_node.image = bpy.data.images.load(environments[mode]['source'], check_existing=False)
            environment_node.image.colorspace_settings.name = 'Linear Rec.709'
        for name, values in definitions:
            if args.only_case and mode+'-'+name not in args.only_case:
                continue
            material = bpy.data.materials.new(name + '-' + mode)
            material.use_nodes = True
            principled = material.node_tree.nodes.get('Principled BSDF')
            numeric = {k: v for k, v in values.items() if k != 'normal_map'}
            sun_data.use_shadow = bool(is_special and numeric.get('Volume Only') == 1)
            volume = None
            if any(k.startswith('Volume.') for k in numeric):
                volume = material.node_tree.nodes.new('ShaderNodeVolumePrincipled')
                material_output = material.node_tree.nodes.get('Material Output')
                material.node_tree.links.new(volume.outputs['Volume'], material_output.inputs['Volume'])
                if numeric.get('Volume Only') == 1:
                    for link in list(material_output.inputs['Surface'].links):
                        material.node_tree.links.remove(link)
            for key, value in numeric.items():
                if key == 'Volume Only':
                    continue
                if key.startswith('Volume.'):
                    volume.inputs[key[len('Volume.'):]].default_value = value
                else:
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
    if args.suite != 'core':
        manifest['suite'] = args.suite
    if is_special:
        manifest['special_reference_version'] = 2
        manifest['bounces'] = dict(SPECIAL_BOUNCES)
        manifest['special_transport'] = dict(SPECIAL_TRANSPORT)
        if args.flat_special_diagnostic:
            manifest['diagnostic_geometry'] = 'flat-special-80'
            manifest['special_transport']['geometry'] = SPECIAL_TRANSPORT['geometry'].replace('smooth', 'flat')
        if args.volume_shadow_diagnostic:
            manifest['diagnostic_volume_shadow'] = True
        if args.volume_bounces_diagnostic is not None:
            manifest['diagnostic_volume_bounces'] = args.volume_bounces_diagnostic
            manifest['bounces']['volume'] = args.volume_bounces_diagnostic
        manifest['exclusions'] = ['texture/normal-map filtering', 'alpha blended queue',
                                  'original area-light/HDRI grid', 'multiple volume scattering']
    if args.only_case:
        manifest['diagnostic_subset'] = sorted(args.only_case)
    if environments is not None:
        manifest['lighting'] = 'hdri'
        manifest['environments'] = environments
        manifest['environment_coordinates'] = 'Cycles outgoing (x,-z,y); engine longitude atan2(z,x), +Y up'
        manifest['exclusions'] = ['texture/normal-map filtering', 'special transport', 'original grid geometry/area-light']
        with (output / 'environment.config').open('w', encoding='utf-8') as stream:
            for mode, record in environments.items():
                stream.write(mode + ' ' + json.dumps(record['cooked']) + ' ' + str(record['strength']) + '\n')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('MAT9_MATCHED_REFERENCE_OK cases=' + str(len(records)))


if __name__ == '__main__':
    main()
