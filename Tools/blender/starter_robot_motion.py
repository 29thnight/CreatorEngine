"""Blender authoring helpers for the eight-clip CreatorHumanoid starter set.

Airborne poses are authored here on the target rig; they use no third-party
animation keys. All movement of the character through the world belongs to the
controller. Only joint rotations and the local pelvis translation are baked.
"""
import math

import bpy
from mathutils import Matrix, Quaternion, Vector


CLIP_ORDER = ('Idle', 'Walk', 'Run', 'Jump', 'FallLoop', 'Land', 'Hit', 'Death')
LOOPING = {'Idle', 'Walk', 'Run', 'FallLoop'}
AUTHORED_DURATIONS = {'Jump': .6, 'FallLoop': .8, 'Land': .6}


def smooth(t):
    t = max(0., min(1., t))
    return t*t*(3.-2.*t)


def capture_pose(rig):
    return {b.name: (b.location.copy(), b.rotation_quaternion.copy()) for b in rig.pose.bones}


def apply_pose(rig, pose):
    for b in rig.pose.bones:
        b.location, b.rotation_quaternion = pose[b.name]
        b.scale = (1, 1, 1)
    bpy.context.view_layer.update()


def mix_pose(a, b, t):
    return {name: (p[0].lerp(b[name][0], t), p[1].slerp(b[name][1], t)) for name, p in a.items()}


def floor_height(mesh):
    evaluated = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    geometry = evaluated.to_mesh()
    z = min(v.co.z for v in geometry.vertices)
    evaluated.to_mesh_clear()
    return z


def ground_pose(rig, mesh, clearance=0.):
    offset = clearance - floor_height(mesh)
    hips = rig.pose.bones['hips']
    hips.location += rig.data.bones['hips'].matrix_local.to_3x3().inverted() @ Vector((0, 0, offset))
    bpy.context.view_layer.update()
    return offset


def key_pose(rig, frame, previous):
    for b in rig.pose.bones:
        if b.name in previous and previous[b.name].dot(b.rotation_quaternion) < 0:
            b.rotation_quaternion.negate()
        previous[b.name] = b.rotation_quaternion.copy()
        b.keyframe_insert(data_path='rotation_quaternion', frame=frame, group=b.name)
        if b.name == 'hips':
            b.keyframe_insert(data_path='location', frame=frame, group=b.name)


def finish_action(action, loop, provenance):
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for curve in bag.fcurves:
                    for key in curve.keyframe_points:
                        key.interpolation = 'LINEAR'
    action['loop'] = loop
    action['playback'] = 'loop' if loop else 'hold_last' if action.name == 'Death' else 'once'
    action['provenance'] = provenance
    action['root_motion'] = False
    action['fps'] = 30
    action.use_fake_user = True


def zero_clip_times(actions):
    """Un-sampled export does not consistently honor the glTF slide option.

    Store actual authoring keys at zero so both Blender and the engine have the
    same clip origin, including fractional terminal key times.
    """
    for action in actions:
        start=float(action.frame_range[0])
        for layer in action.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for curve in bag.fcurves:
                        for key in curve.keyframe_points:
                            key.co.x-=start
                            key.handle_left.x-=start
                            key.handle_right.x-=start
                        curve.update()


def neutral_rotation(rig, name, clip, phase, leg_directions, anatomical_basis):
    """Replace weapon-ready upper-body poses with upright, unarmed movement."""
    lean = {'Idle': 0., 'Walk': 4., 'Run': 11.}[clip]
    breath = math.sin(phase*math.tau*(3 if clip == 'Idle' else 1))
    if name in ('hips', 'spine', 'chest', 'upper_chest', 'neck', 'head') or name.startswith('clavicle_'):
        tilt = lean + (.6*breath if name not in ('hips', 'neck', 'head') else 0.)
        if name in ('neck', 'head'):
            tilt *= .3
        return Quaternion((1, 0, 0), math.radians(-tilt)) @ rig.data.bones[name].matrix_local.to_quaternion()
    if name.startswith(('upper_arm_', 'lower_arm_', 'hand_')):
        side = name[-1]
        sign = -1 if side == 'l' else 1
        swing = 0. if clip == 'Idle' else -leg_directions[side].y*(.8 if clip == 'Walk' else 1.05)
        angle = swing + (math.radians(8 if clip != 'Run' else 75) if not name.startswith('upper_arm_') else 0.)
        if clip == 'Idle':
            angle += math.radians(1.*breath)
        direction = Vector((sign*.11, math.sin(angle), -math.cos(angle)))
        return anatomical_basis(direction, Vector((0, 1, 0)))
    return None


def authored_pose(rig, *, crouch, lean, arm_angle, elbow_angle, airborne=False, sway=0.):
    """Analytical two-bone leg posing keeps authored soles aligned with ground.

    The knee bend pole points forward. Feet retain their rest orientation while
    the pelvis lowers, giving a clear compression/extension without foot slides.
    """
    poses = {}
    rotations = {}
    hips_position = rig.data.bones['hips'].head_local + Vector((0, -.32*crouch, -crouch))
    pitch = Quaternion((1, 0, 0), math.radians(-lean))
    for name in ('hips', 'spine', 'chest', 'upper_chest', 'neck', 'head'):
        angle = lean*(.35 if name in ('neck', 'head') else 1.)
        rotations[name] = Quaternion((1, 0, 0), math.radians(-angle)) @ rig.data.bones[name].matrix_local.to_quaternion()
    # Hips stay level to keep the two leg roots symmetric during compression.
    rotations['hips'] = rig.data.bones['hips'].matrix_local.to_quaternion()
    def orient(name, direction):
        rest = rig.data.bones[name]
        rotations[name] = (rest.tail_local-rest.head_local).rotation_difference(direction) @ rest.matrix_local.to_quaternion()
    for side, sign in (('l', -1), ('r', 1)):
        thigh = rig.data.bones['upper_leg_'+side]
        calf = rig.data.bones['lower_leg_'+side]
        hip = hips_position + (thigh.head_local-rig.data.bones['hips'].head_local)
        ankle = rig.data.bones['foot_'+side].head_local.copy()
        if airborne:
            ankle.y -= .10
            ankle.z += .045
        target = ankle-hip
        distance = min(target.length, thigh.length+calf.length-.00001)
        axis = target.normalized()
        pole = Vector((0, 1, 0))
        pole = (pole-axis*pole.dot(axis)).normalized()
        along = (thigh.length**2-calf.length**2+distance**2)/(2*distance)
        height = math.sqrt(max(0., thigh.length**2-along**2))
        knee = hip+axis*along+pole*height
        orient('upper_leg_'+side, knee-hip)
        orient('lower_leg_'+side, ankle-knee)
        rotations['foot_'+side] = rig.data.bones['foot_'+side].matrix_local.to_quaternion()
        rotations['clavicle_'+side] = pitch @ rig.data.bones['clavicle_'+side].matrix_local.to_quaternion()
        for part, extra in (('upper_arm', 0.), ('lower_arm', elbow_angle), ('hand', elbow_angle)):
            a = math.radians(arm_angle+extra+sign*sway)
            orient(part+'_'+side, Vector((sign*.16, math.sin(a), -math.cos(a))))
    for bone in rig.data.bones:
        name = bone.name
        if name == 'root':
            poses[name] = bone.matrix_local.copy()
            rig.pose.bones[name].matrix_basis = Matrix.Identity(4)
            continue
        parent = bone.parent
        if name in rotations:
            position = hips_position if name == 'hips' else poses[parent.name] @ (parent.matrix_local.inverted() @ bone.head_local)
            poses[name] = Matrix.Translation(position) @ rotations[name].to_matrix().to_4x4()
        else:
            poses[name] = poses[parent.name] @ parent.matrix_local.inverted() @ bone.matrix_local
        rig.pose.bones[name].matrix_basis = bone.matrix_local.inverted() @ parent.matrix_local @ poses[parent.name].inverted() @ poses[name]
    bpy.context.view_layer.update()
    return capture_pose(rig)


def author_airborne_clips(rig, mesh, report):
    scene = bpy.context.scene
    idle = bpy.data.actions['Idle']
    rig.animation_data.action = idle
    rig.animation_data.action_slot = idle.slots[0]
    scene.frame_set(1)
    neutral = capture_pose(rig)
    rig.animation_data.action = None
    authored_pose(rig, crouch=.23, lean=20, arm_angle=-27, elbow_angle=30)
    ground_pose(rig, mesh)
    compression = capture_pose(rig)
    authored_pose(rig, crouch=.09, lean=5, arm_angle=24, elbow_angle=42, airborne=True)
    ground_pose(rig, mesh, .06)
    airborne = capture_pose(rig)
    actions = []
    for name, duration in AUTHORED_DURATIONS.items():
        action = bpy.data.actions.new(name)
        rig.animation_data.action = action
        previous = {}; clearances = []
        count = round(duration*30)
        for i in range(count+1):
            t = i/count
            scene.frame_set(i+1)
            if name == 'Jump':
                if t < .38:
                    pose = mix_pose(neutral, compression, smooth(t/.38)); clearance = 0.
                else:
                    weight = smooth((t-.38)/.62)
                    pose = mix_pose(compression, airborne, weight); clearance = .06*weight
            elif name == 'FallLoop':
                # Small periodic joint motion; both boundary poses exactly match.
                apply_pose(rig, airborne)
                pulse = math.sin(t*math.tau)
                for side, sign in (('l', -1), ('r', 1)):
                    rig.pose.bones['upper_arm_'+side].rotation_quaternion @= Quaternion((1, 0, 0), math.radians(sign*2*pulse))
                rig.pose.bones['head'].rotation_quaternion @= Quaternion((1, 0, 0), math.radians(.7*pulse))
                pose = capture_pose(rig); clearance = .06
            else:
                if t < .3:
                    weight = smooth(t/.3)
                    pose = mix_pose(airborne, compression, weight); clearance = .06*(1-weight)
                else:
                    pose = mix_pose(compression, neutral, smooth((t-.3)/.7)); clearance = 0.
            apply_pose(rig, pose)
            ground_pose(rig, mesh, clearance)
            key_pose(rig, i+1, previous)
            clearances.append(clearance)
        finish_action(action, name in LOOPING, 'Authored directly on CreatorHumanoid in Blender; no source animation keys')
        action['source_clip'] = 'Original CreatorHumanoid authoring'
        report['clips'].append({'name': name, 'source_clip': None, 'provenance': 'original_authoring',
            'frames': count+1, 'duration_seconds': duration, 'loop': name in LOOPING,
            'playback': action['playback'], 'expected_ground_clearance_meters': clearances,
            'motion_policy': 'in-place; controller supplies jump trajectory and gravity'})
        actions.append(action)
        print('AUTHORED_CLIP', name, count+1, flush=True)
    return actions
