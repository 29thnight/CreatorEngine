"""Prepare a meter-scale humanoid outside the engine asset-watcher directories.

Blender 5.1:
  blender -b --factory-startup --python Tools/blender/prepare_starter_robot.py -- \
    --source <sci-fi-humanoid-robot> --animation-source <SU_Mythic.glb> --out <folder>

The original FBX/textures are read-only inputs. SU_Mythic clip derivatives are
local evaluation assets; their redistribution rights are not established by
the robot mesh's CC BY 4.0 license. The RigOnly directory contains no SU data.
"""
import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from starter_robot_motion import (CLIP_ORDER, LOOPING, apply_pose, capture_pose,
    finish_action, ground_pose, mix_pose, neutral_rotation, smooth, author_airborne_clips, zero_clip_times)


HEIGHT = 1.80
FPS = 30
CLIPS = {
    'Idle': 'Skin_SU_Mythic_Swd_Idle_1',
    'Walk': 'Skin_SU_Mythic_Swd_Walk_1',
    'Run': 'Skin_SU_Mythic_Swd_Run_1',
    'Hit': 'Skin_SU_Mythic_Swd_Dmg_1',
    'Death': 'Skin_SU_Mythic_Death_1',
}
BONES = []


def bone(name, parent, head, tail):
    BONES.append((name, parent, Vector(head), Vector(tail)))


def build_bone_definitions():
    BONES.clear()
    bone('root', None, (0, 0, 0), (0, 0, .18))
    bone('hips', 'root', (0, -.025, 1.015), (0, -.015, 1.105))
    bone('spine', 'hips', (0, -.015, 1.105), (0, -.005, 1.225))
    bone('chest', 'spine', (0, -.005, 1.225), (0, -.020, 1.36))
    bone('upper_chest', 'chest', (0, -.020, 1.36), (0, -.025, 1.49))
    bone('neck', 'upper_chest', (0, -.025, 1.49), (0, -.030, 1.56))
    bone('head', 'neck', (0, -.030, 1.56), (0, -.030, 1.75))
    for side, sign in [('l', -1), ('r', 1)]:
        def p(x,y,z): return (sign*x,y,z)
        bone('clavicle_'+side, 'upper_chest', p(.075,-.06,1.475), p(.26,-.115,1.484))
        bone('upper_arm_'+side, 'clavicle_'+side, p(.26,-.115,1.484), p(.568,-.115,1.484))
        bone('lower_arm_'+side, 'upper_arm_'+side, p(.568,-.115,1.484), p(.825,-.115,1.494))
        bone('hand_'+side, 'lower_arm_'+side, p(.825,-.115,1.494), p(.926,-.120,1.494))
        fingers = {
            'thumb': [(.852,-.080,1.495),(.877,-.063,1.499),(.906,-.053,1.502),(.935,-.040,1.503)],
            'index': [(.926,-.094,1.498),(.970,-.097,1.497),(1.001,-.098,1.496),(1.035,-.099,1.496)],
            'middle':[(.928,-.113,1.494),(.974,-.114,1.493),(1.008,-.115,1.492),(1.041,-.116,1.492)],
            'ring':  [(.927,-.134,1.490),(.972,-.134,1.489),(1.005,-.135,1.488),(1.034,-.135,1.488)],
            'pinky': [(.922,-.155,1.485),(.963,-.154,1.484),(.993,-.154,1.483),(1.016,-.154,1.483)],
        }
        for finger, points in fingers.items():
            parent = 'hand_'+side
            for i in range(3):
                name=f'{finger}_{i+1:02d}_{side}'
                bone(name, parent, p(*points[i]), p(*points[i+1]))
                parent=name
        bone('upper_leg_'+side,'hips',p(.150,-.025,1.015),p(.150,-.017,.632))
        bone('lower_leg_'+side,'upper_leg_'+side,p(.150,-.017,.632),p(.150,-.005,.098))
        bone('foot_'+side,'lower_leg_'+side,p(.150,-.005,.098),p(.150,.105,.031))
        bone('toes_'+side,'foot_'+side,p(.150,.105,.031),p(.150,.185,.029))


def select_only(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects: obj.select_set(True)
    bpy.context.view_layer.objects.active=objects[0]


def components(mesh):
    """Find disconnected mechanical parts, including paired left/right parts."""
    parent=list(range(len(mesh.vertices)))
    def find(i):
        while parent[i]!=i:
            parent[i]=parent[parent[i]]
            i=parent[i]
        return i
    for e in mesh.edges:
        a,b=(find(i) for i in e.vertices)
        if a!=b: parent[b]=a
    groups={}
    for i in range(len(parent)): groups.setdefault(find(i),[]).append(i)
    return groups.values()


def point_segment_distance(p,a,b):
    ab=b-a
    t=max(0.,min(1.,(p-a).dot(ab)/ab.length_squared))
    return (p-(a+t*ab)).length


def rigid_part_bone(material, center, extent, minimum):
    x,y,z=center
    side='l' if x<0 else 'r'
    lateral=abs(x)
    if material=='Head_Mat':
        return 'head' if z>=1.548 else 'neck' if z>=1.49 else 'upper_chest'
    if material=='Arms_Mat':
        if lateral<.25: return 'clavicle_'+side
        if lateral<.556: return 'upper_arm_'+side
        if lateral<.817: return 'lower_arm_'+side
        # Palm pieces cross multiple digits; keep them on the hand.
        if lateral<.919 or extent.y>.06:
            if .851<lateral<.938 and y>-.080 and extent.y<.055:
                candidates=[b for b in BONES if b[0].startswith('thumb_') and b[0].endswith('_'+side)]
            else: return 'hand_'+side
        else:
            candidates=[b for b in BONES if b[0].split('_')[0] in ('thumb','index','middle','ring','pinky') and b[0].endswith('_'+side)]
        return min(candidates,key=lambda b:point_segment_distance(center,b[2],b[3]))[0]
    if material=='LegsMat':
        if z<.10: return 'foot_'+side
        if z<.655: return 'lower_leg_'+side
        return 'upper_leg_'+side
    if z<1.10: return 'hips'
    if z<1.23: return 'spine'
    if z<1.37: return 'chest'
    return 'upper_chest'


def create_rig():
    arm=bpy.data.armatures.new('CreatorHumanoid')
    rig=bpy.data.objects.new('CreatorRobot',arm)
    bpy.context.scene.collection.objects.link(rig)
    select_only([rig])
    bpy.ops.object.mode_set(mode='EDIT')
    for name,parent,head,tail in BONES:
        b=arm.edit_bones.new(name)
        b.head=head; b.tail=tail
        if parent: b.parent=arm.edit_bones[parent]
        b.align_roll(Vector((0,1,0)) if abs((tail-head).normalized().y)<.95 else Vector((0,0,1)))
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.show_in_front=True
    arm.display_type='OCTAHEDRAL'
    rig['height_meters']=HEIGHT
    rig['engine_forward']='+Z'
    rig['blender_forward']='+Y'
    rig['unit_contract']='1 Blender unit = 1 glTF meter = 1 CreatorEngine unit; object scale 1'
    rig['rig_version']='CreatorHumanoid 1'
    for pb in rig.pose.bones: pb.rotation_mode='QUATERNION'
    rig.animation_data_create()
    return rig


def prepare_meshes(args, report):
    bpy.ops.import_scene.fbx(filepath=str(args.source/'source/Textured Bot final.fbx'))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    # Original is centimeter-authored under a .01 FBX root. Bake the evaluated
    # world transform, rotate its +X facing direction to Blender +Y, and ground it.
    orientation=Matrix.Rotation(math.pi/2,4,'Z')
    for obj in meshes:
        world=obj.matrix_world.copy()
        obj.parent=None
        obj.data.transform(orientation@world)
        obj.matrix_world=Matrix.Identity(4)
    lo=min(v.co.z for o in meshes for v in o.data.vertices)
    hi=max(v.co.z for o in meshes for v in o.data.vertices)
    scale=HEIGHT/(hi-lo)
    norm=Matrix.Diagonal((scale,scale,scale,1.))@Matrix.Translation((0,0,-lo))
    for obj in meshes: obj.data.transform(norm)
    for obj in list(bpy.context.scene.objects):
        if obj.type!='MESH': bpy.data.objects.remove(obj,do_unlink=True)
    report['source_geometry']={'triangles':sum(len(o.data.loop_triangles) or sum(len(p.vertices)-2 for p in o.data.polygons) for o in meshes),
                               'objects':len(meshes),'height_meters':hi-lo,'normalization_factor':scale}
    # Coordinates in BONES are measured on the 1.80 m normalized T pose.
    part_summary={}
    for obj in meshes:
        material=obj.data.materials[0].name
        for name,_,_,_ in BONES: obj.vertex_groups.new(name=name)
        for ids in components(obj.data):
            pts=[obj.data.vertices[i].co for i in ids]
            minimum=Vector(tuple(min(v[a] for v in pts) for a in range(3)))
            maximum=Vector(tuple(max(v[a] for v in pts) for a in range(3)))
            center=(minimum+maximum)*.5
            name=rigid_part_bone(material,center,maximum-minimum,minimum)
            # Tall spine cables need continuous bending across torso joints.
            if material=='TorsoMat' and maximum.z-minimum.z>.35:
                z_bones=[(1.02,'hips'),(1.15,'spine'),(1.29,'chest'),(1.45,'upper_chest'),(1.53,'neck')]
                for i in ids:
                    z=obj.data.vertices[i].co.z
                    if z<=z_bones[0][0]: pairs=[(z_bones[0][1],1.)]
                    elif z>=z_bones[-1][0]: pairs=[(z_bones[-1][1],1.)]
                    else:
                        a,b=next((a,b) for a,b in zip(z_bones,z_bones[1:]) if a[0]<=z<=b[0])
                        t=(z-a[0])/(b[0]-a[0]); pairs=[(a[1],1-t),(b[1],t)]
                    for bn,w in pairs:
                        if w>1e-6: obj.vertex_groups[bn].add([i],w,'REPLACE')
            else: obj.vertex_groups[name].add(ids,1.,'REPLACE')
            part_summary[name]=part_summary.get(name,0)+1
        select_only([obj])
        original=sum(len(p.vertices)-2 for p in obj.data.polygons)
        if original>300:
            dec=obj.modifiers.new('Starter mesh reduction','DECIMATE')
            dec.ratio=max(.18,min(1.,120./original))
            dec.use_collapse_triangulate=True
            bpy.ops.object.modifier_apply(modifier=dec.name)
    # Keep four materials/UV layouts, but make one skinned object.
    select_only(meshes)
    bpy.ops.object.join()
    mesh=bpy.context.object
    mesh.name='CreatorRobot_Body'; mesh.data.name='CreatorRobot_Body'
    low=min(v.co.z for v in mesh.data.vertices)
    high=max(v.co.z for v in mesh.data.vertices)
    correction=HEIGHT/(high-low)
    mesh.data.transform(Matrix.Diagonal((correction,correction,correction,1.))@Matrix.Translation((0,0,-low)))
    mesh.data.calc_loop_triangles()
    report['prepared_geometry']={'vertices':len(mesh.data.vertices),'triangles':len(mesh.data.loop_triangles),
                                'objects':1,'materials':len(mesh.data.materials),'height_meters':HEIGHT,
                                'rigid_parts_by_bone':part_summary}
    return mesh


def image_pixels(path, size=2048):
    image=bpy.data.images.load(str(path),check_existing=False)
    image.colorspace_settings.name='Non-Color'
    image.scale(size,size)
    pixels=np.empty(size*size*4,dtype=np.float32)
    image.pixels.foreach_get(pixels)
    bpy.data.images.remove(image)
    return pixels.reshape((size,size,4))


def save_pixels(name, pixels, path, color=False):
    h,w,_=pixels.shape
    image=bpy.data.images.new(name,width=w,height=h,alpha=False)
    image.colorspace_settings.name='sRGB' if color else 'Non-Color'
    image.pixels.foreach_set(pixels.ravel())
    image.filepath_raw=str(path)
    image.file_format='PNG'
    image.save()
    image.pack()
    return image


def prepare_materials(args,mesh):
    folder=args.out/'Textures'; folder.mkdir(parents=True,exist_ok=True)
    group=bpy.data.node_groups.new('glTF Material Output','ShaderNodeTree')
    group.interface.new_socket(name='Occlusion',in_out='INPUT',socket_type='NodeSocketFloat')
    for mat in mesh.data.materials:
        source_name=mat.name
        mat.use_nodes=True
        nodes=mat.node_tree.nodes; nodes.clear(); links=mat.node_tree.links
        output=nodes.new('ShaderNodeOutputMaterial'); output.location=(650,0)
        bsdf=nodes.new('ShaderNodeBsdfPrincipled'); bsdf.location=(340,0)
        links.new(bsdf.outputs['BSDF'],output.inputs['Surface'])
        for socket in ('Metallic','Roughness'): bsdf.inputs[socket].default_value=1.
        base=image_pixels(args.source/'textures'/f'{source_name}_Base_color.png')
        base_image=save_pixels(source_name+'_BaseColor',base,folder/f'{source_name}_BaseColor.png',True)
        orm=np.ones_like(base)
        for channel,suffix in [(0,'Mixed_AO'),(1,'Roughness'),(2,'Metallic')]:
            orm[:,:,channel]=image_pixels(args.source/'textures'/f'{source_name}_{suffix}.png')[:,:,0]
        orm_image=save_pixels(source_name+'_ORM',orm,folder/f'{source_name}_ORM.png')
        normal=image_pixels(args.source/'textures'/f'{source_name}_Normal.png')
        # FBX explicitly names its normal textures *_Normal_DirectX.png.
        normal[:,:,1]=1.-normal[:,:,1]
        normal_image=save_pixels(source_name+'_NormalGL',normal,folder/f'{source_name}_NormalGL.png')
        def tex(image,y):
            node=nodes.new('ShaderNodeTexImage');node.image=image;node.location=(-600,y);return node
        links.new(tex(base_image,350).outputs['Color'],bsdf.inputs['Base Color'])
        split=nodes.new('ShaderNodeSeparateColor');split.location=(-250,0)
        links.new(tex(orm_image,0).outputs['Color'],split.inputs['Color'])
        links.new(split.outputs['Green'],bsdf.inputs['Roughness'])
        links.new(split.outputs['Blue'],bsdf.inputs['Metallic'])
        occlusion=nodes.new('ShaderNodeGroup');occlusion.node_tree=group;occlusion.location=(0,-300)
        links.new(split.outputs['Red'],occlusion.inputs['Occlusion'])
        normal_node=nodes.new('ShaderNodeNormalMap');normal_node.location=(0,-500)
        links.new(tex(normal_image,-400).outputs['Color'],normal_node.inputs['Color'])
        links.new(normal_node.outputs['Normal'],bsdf.inputs['Normal'])
        mat.name='CreatorRobot_'+source_name


def reset_pose(rig):
    for b in rig.pose.bones:
        b.location=(0,0,0); b.rotation_quaternion=(1,0,0,0); b.scale=(1,1,1)


def export_glb(rig,mesh,path,animations):
    select_only([rig,mesh])
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',use_selection=True,
        export_yup=True,export_normals=True,export_tangents=True,export_texcoords=True,
        # Preserve fractional final key times (8.75s / 0.91666s), otherwise
        # forced integer-frame sampling truncates the loop closing pose.
        export_animations=animations,export_animation_mode='ACTIONS',export_force_sampling=False,
        export_anim_slide_to_zero=True,
        export_frame_range=False,export_frame_step=1,export_nla_strips=True,
        export_skins=True,export_all_influences=False,export_def_bones=False,
        export_armature_object_remove=False,export_extras=True,export_apply=False)
    if animations:
        # Blender sorts Actions alphabetically; the engine's initial clip is 0.
        # Keep Idle first and a predictable starter order without changing any
        # channels, accessors, skins, or embedded binary data.
        raw=path.read_bytes()
        json_length=struct.unpack_from('<I',raw,12)[0]
        document=json.loads(raw[20:20+json_length])
        document['animations'].sort(key=lambda a:CLIP_ORDER.index(a['name']))
        encoded=json.dumps(document,separators=(',',':'),ensure_ascii=False).encode('utf-8')
        encoded+=b' '*((-len(encoded))%4)
        remaining=raw[20+json_length:]
        path.write_bytes(struct.pack('<4sII',b'glTF',2,20+len(encoded)+len(remaining))
                         +struct.pack('<II',len(encoded),0x4E4F534A)+encoded+remaining)


def anatomical_basis(direction,reference):
    y=direction.normalized()
    z=reference-y*reference.dot(y)
    if z.length<1e-5: z=Vector((0,0,1))-y*y.z
    z.normalize();x=y.cross(z).normalized();z=x.cross(y).normalized()
    return Matrix((x,y,z)).transposed().to_quaternion()


def retarget(args,rig,mesh,report):
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(args.animation_source))
    source_fps=bpy.context.scene.render.fps/bpy.context.scene.render.fps_base
    imported=set(bpy.data.objects)-before
    source=next(o for o in imported if o.type=='ARMATURE')
    source_mesh=next(o for o in imported if o.type=='MESH' and any(m.type=='ARMATURE' for m in o.modifiers))
    foot_groups={g.index for g in source_mesh.vertex_groups if g.name in ('Bip01-L-Foot','Bip01-R-Foot')}
    foot_vertices=[v.index for v in source_mesh.data.vertices if any(g.group in foot_groups and g.weight>.5 for g in v.groups)]
    if not foot_vertices:raise RuntimeError('Source foot skin groups are missing')
    for track in source.animation_data.nla_tracks: track.mute=True
    mapping={'hips':'Bip01-Pelvis','spine':'Bip01-Spine','chest':'Bip01-Spine1',
             'upper_chest':'Bip01-Spine2','neck':'Bip01-Neck','head':'Bip01-Head'}
    successors={'spine':'Bip01-Spine1','chest':'Bip01-Spine2','upper_chest':'Bip01-Neck','neck':'Bip01-Head'}
    for s,S in [('l','L'),('r','R')]:
        for a,b in [('clavicle','Clavicle'),('upper_arm','UpperArm'),('lower_arm','Forearm'),
                    ('hand','Hand'),('upper_leg','Thigh'),('lower_leg','Calf'),('foot','Foot')]:
            mapping[a+'_'+s]='Bip01-'+S+'-'+b
        for a,b in [('clavicle','UpperArm'),('upper_arm','Forearm'),('lower_arm','Hand'),
                    ('upper_leg','Calf'),('lower_leg','Foot')]:
            successors[a+'_'+s]='Bip01-'+S+'-'+b
    turn=Matrix.Rotation(math.pi,3,'Z').to_quaternion()
    corrections={}
    for target,src in mapping.items():
        sb=source.data.bones[src]
        if target in successors:
            direction=source.data.bones[successors[target]].head_local-sb.head_local
            desired=anatomical_basis(direction,Vector((0,-1,0)))
        elif target.startswith('hand_'):
            forearm=source.data.bones[mapping['lower_arm_'+target[-1]]]
            desired=anatomical_basis(sb.head_local-forearm.head_local,Vector((0,-1,0)))
        else:
            desired=turn.inverted()@rig.data.bones[target].matrix_local.to_quaternion()
        corrections[target]=sb.matrix_local.to_quaternion().inverted()@desired
    source_hips=source.data.bones['Bip01-Pelvis'].head_local.copy()
    source_scale=rig.data.bones['hips'].head_local.z/source_hips.z
    scene=bpy.context.scene
    # The glTF importer creates frame numbers using the current scene FPS.
    scene.render.fps=FPS
    scene.render.fps_base=1.
    report['retarget']={'source_scale_from_hip_height':source_scale,'source_fps':source_fps,'mapping':mapping,
                        'source_forward':'-Y','target_forward':'+Y',
                        'translation_policy':'in-place XY; scaled source foot clearance preserves airborne phases'}
    report['clips']=[]
    actions=[]
    neutral=None
    for output_name,input_name in CLIPS.items():
        src_action=bpy.data.actions.get(input_name)
        if src_action is None: raise RuntimeError('Required source clip missing: '+input_name)
        source.animation_data.action=src_action
        source.animation_data.action_slot=src_action.slots[0]
        start,end=(float(f) for f in src_action.frame_range)
        duration=(end-start)/source_fps
        samples=[start+i*source_fps/FPS for i in range(math.ceil(duration*FPS))]+[end]
        source_sole_heights=[]
        for frame in samples:
            scene.frame_set(int(frame),subframe=frame-int(frame))
            evaluated=source_mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
            geometry=evaluated.to_mesh()
            source_sole_heights.append(min(geometry.vertices[i].co.z for i in foot_vertices))
            evaluated.to_mesh_clear()
        baseline=min(source_sole_heights)
        clearances=[max(0.,(z-baseline)*source_scale) for z in source_sole_heights]
        clearances=[0. if z<.004 else z for z in clearances]
        # A fallen body contacts the ground with torso/head, not only its feet.
        if output_name=='Death': clearances=[0.]*len(samples)
        action=bpy.data.actions.new(output_name);action.use_fake_user=True
        rig.animation_data.action=action
        reset_pose(rig)
        corrections_z=[]
        root_motion=[]
        previous_quaternions={}
        initial_source_pos=None
        expected_clearances=[]
        for sample_index,frame in enumerate(samples):
            scene.frame_set(int(frame),subframe=frame-int(frame))
            source_pos=source.pose.bones['Bip01-Pelvis'].matrix.translation
            if initial_source_pos is None: initial_source_pos=source_pos.copy()
            root_motion.append(list((turn@(source_pos-source_hips))*source_scale))
            reset_pose(rig)
            poses={}
            phase=(frame-start)/(end-start)
            leg_directions={s:(turn@source.pose.bones[mapping['upper_leg_'+s]].matrix.to_quaternion()
                              @corrections['upper_leg_'+s])@Vector((0,1,0)) for s in ('l','r')}
            for name,parent,head,tail in BONES:
                pb=rig.pose.bones[name]
                rest=rig.data.bones[name]
                if name=='root':
                    poses[name]=rest.matrix_local.copy()
                    continue
                if name in mapping:
                    rotation=turn@source.pose.bones[mapping[name]].matrix.to_quaternion()@corrections[name]
                    if output_name in ('Idle','Walk','Run'):
                        adjusted=neutral_rotation(rig,name,output_name,phase,leg_directions,anatomical_basis)
                        if adjusted is not None: rotation=adjusted
                    if name=='hips':
                        position=head.copy()
                        position.z+=(source_pos.z-source_hips.z)*source_scale
                        if output_name=='Death':
                            delta=(turn@(source_pos-initial_source_pos))*source_scale
                            position.x+=delta.x;position.y+=delta.y
                    else:
                        local=rig.data.bones[parent].matrix_local.inverted()@head
                        position=poses[parent]@local
                    poses[name]=Matrix.Translation(position)@rotation.to_matrix().to_4x4()
                    pb.matrix_basis=(rest.matrix_local.inverted()@rig.data.bones[parent].matrix_local
                                     @poses[parent].inverted()@poses[name])
                else:
                    poses[name]=poses[parent]@rig.data.bones[parent].matrix_local.inverted()@rest.matrix_local
            bpy.context.view_layer.update()
            # Limb proportions differ: ground the lowest foot each sample, using
            # only the skinned foot vertices, not an approximate bone head.
            deps=bpy.context.evaluated_depsgraph_get()
            evaluated=mesh.evaluated_get(deps)
            evmesh=evaluated.to_mesh()
            lowest=min(v.co.z for v in evmesh.vertices)
            evaluated.to_mesh_clear()
            offset=clearances[sample_index]-lowest
            rig.pose.bones['hips'].location+=(rig.data.bones['hips'].matrix_local.to_3x3().inverted()
                                             @Vector((0,0,offset)))
            corrections_z.append(offset)
            bpy.context.view_layer.update()
            clearance=clearances[sample_index]
            if output_name in ('Hit','Death'):
                weight=smooth((frame-start)/source_fps/.10)
                if output_name=='Hit': weight=min(weight,smooth((end-frame)/source_fps/.13))
                apply_pose(rig,mix_pose(neutral,capture_pose(rig),weight))
                clearance*=weight
                ground_pose(rig,mesh,clearance)
            if output_name=='Idle' and sample_index==0: neutral=capture_pose(rig)
            expected_clearances.append(clearance)
            for pb in rig.pose.bones:
                # Local translations are fixed except hips; scale is never keyed.
                previous=previous_quaternions.get(pb.name)
                if previous is not None and previous.dot(pb.rotation_quaternion)<0:
                    pb.rotation_quaternion.negate()
                previous_quaternions[pb.name]=pb.rotation_quaternion.copy()
                output_frame=(frame-start)/source_fps*FPS+1
                pb.keyframe_insert(data_path='rotation_quaternion',frame=output_frame,group=pb.name)
                if pb.name=='hips':pb.keyframe_insert(data_path='location',frame=output_frame,group=pb.name)
        finish_action(action,output_name in LOOPING,'SU_Mythic retarget with target-rig pose cleanup')
        action['source_clip']=input_name
        action['usage']='Local evaluation; source animation redistribution rights not established'
        report['clips'].append({'name':output_name,'source_clip':input_name,'frames':len(samples),
                                'provenance':'SU_Mythic_derivative','loop':output_name in LOOPING,
                                'playback':action['playback'],
                                'duration_seconds':duration,'ground_correction_range': [min(corrections_z),max(corrections_z)],
                                'source_foot_clearance_meters':clearances,
                                'expected_ground_clearance_meters':expected_clearances,
                                'source_motion_range':[[min(v[i] for v in root_motion),max(v[i] for v in root_motion)] for i in range(3)]})
        actions.append(action)
        print('BAKED_CLIP',output_name,len(samples),flush=True)
    # Strip the source model, unrelated clips, helpers and images completely.
    rig.animation_data.action=None
    for obj in imported: bpy.data.objects.remove(obj,do_unlink=True)
    for act in list(bpy.data.actions):
        if act not in actions: bpy.data.actions.remove(act)
    reset_pose(rig)
    bpy.context.view_layer.update()
    return actions


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--animation-source',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    args.out=args.out.resolve();args.source=args.source.resolve();args.animation_source=args.animation_source.resolve()
    args.out.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.filepaths.save_version=0
    readme=Path(__file__).with_name('README-starter-robot.md').read_text(encoding='utf-8')
    (args.out/'README.md').write_text(readme,encoding='utf-8')
    text=bpy.data.texts.new('README - CreatorRobot')
    text.use_fake_user=True
    text.write(readme)
    scene=bpy.context.scene
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1.
    scene.render.fps=FPS
    scene.render.fps_base=1.
    build_bone_definitions()
    report={'blender_version':bpy.app.version_string,'height_meters':HEIGHT,'fps':FPS,
            'engine_imported':False,'object_scale':[1,1,1],
            'source_model_sha256':hashlib.sha256((args.source/'source/Textured Bot final.fbx').read_bytes()).hexdigest(),
            'source_animation_sha256':hashlib.sha256(args.animation_source.read_bytes()).hexdigest()}
    mesh=prepare_meshes(args,report)
    print('PREPARED_MESH',report['prepared_geometry']['triangles'],flush=True)
    rig=create_rig()
    mesh.parent=rig;mesh.matrix_parent_inverse=Matrix.Identity(4)
    modifier=mesh.modifiers.new('Humanoid skin','ARMATURE');modifier.object=rig
    prepare_materials(args,mesh)
    report['bones']=[{'name':n,'parent':p,'head':list(h),'tail':list(t)} for n,p,h,t in BONES]
    for obj in (mesh,rig):
        if any(abs(s-1)>1e-6 for s in obj.scale): raise RuntimeError('Non-unit object scale')
    for v in mesh.data.vertices:
        total=sum(g.weight for g in v.groups)
        if abs(total-1)>1e-4: raise RuntimeError(f'Invalid skin weight sum at vertex {v.index}: {total}')
    # Purge imported FBX texture blocks before saving the packed authoring file.
    for image in list(bpy.data.images):
        if image.users==0: bpy.data.images.remove(image)
    rig_dir=args.out/'RigOnly';rig_dir.mkdir(exist_ok=True)
    scene.render.fps=FPS;scene.render.fps_base=1.
    scene.frame_start=1;scene.frame_end=210
    select_only([rig])
    bpy.ops.wm.save_as_mainfile(filepath=str(rig_dir/'CreatorRobot_Rig.blend'))
    export_glb(rig,mesh,rig_dir/'CreatorRobot_Rig.glb',False)
    actions=retarget(args,rig,mesh,report)
    actions+=author_airborne_clips(rig,mesh,report)
    zero_clip_times(actions)
    report['clips'].sort(key=lambda c:CLIP_ORDER.index(c['name']))
    report['clip_set_version']=2
    report['export_timing']='All glTF clips start at 0 seconds; fractional terminal keys are retained'
    scene.frame_start=0;scene.frame_end=math.ceil(actions[0].frame_range[1])
    rig.animation_data.action=actions[0]
    rig.animation_data.action_slot=actions[0].slots[0]
    scene.frame_set(0)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                space=area.spaces.active
                space.region_3d.view_location=(0,0,.9)
                space.region_3d.view_distance=3.7
                space.region_3d.view_rotation=Vector((.8,4.,1.)).to_track_quat('Z','Y')
                space.shading.type='MATERIAL'
    for block in (bpy.data.images,bpy.data.materials,bpy.data.meshes,bpy.data.armatures):
        for item in list(block):
            if item.users==0: block.remove(item)
    bpy.data.orphans_purge(do_recursive=True)
    select_only([rig])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out/'CreatorRobot.blend'))
    export_glb(rig,mesh,args.out/'CreatorRobot.glb',True)
    (args.out/'preparation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('STARTER_ROBOT_PREPARED',args.out,flush=True)


if __name__=='__main__':main()
