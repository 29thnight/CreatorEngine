"""Validate Blender authoring and exported GLB without launching the engine."""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

CLIPS=('Idle','Walk','Run','Jump','FallLoop','Land','Hit','Death')
LOOPING={'Idle','Walk','Run','FallLoop'}
EXPECTED_DURATIONS={'Idle':8.75,'Walk':22/24,'Run':20/24,'Jump':.6,'FallLoop':.8,'Land':.6,'Hit':.5,'Death':100/24}


def reset_pose(rig):
    rig.animation_data.action=None
    for b in rig.pose.bones:
        b.location=(0,0,0);b.rotation_quaternion=(1,0,0,0);b.scale=(1,1,1)
    bpy.context.view_layer.update()


def coordinates(mesh):
    evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
    data=evaluated.to_mesh()
    values=np.empty(len(data.vertices)*3,dtype=np.float32)
    data.vertices.foreach_get('co',values)
    evaluated.to_mesh_clear()
    return values.reshape(-1,3)


def read_glb(path):
    raw=path.read_bytes();length=struct.unpack_from('<I',raw,12)[0]
    data=json.loads(raw[20:20+length]);binary=raw[28+length:]
    def accessor(index):
        a=data['accessors'][index];view=data['bufferViews'][a['bufferView']]
        dtype={5126:'<f4',5125:'<u4',5123:'<u2',5121:'u1'}[a['componentType']]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        offset=view.get('byteOffset',0)+a.get('byteOffset',0)
        stride=view.get('byteStride',np.dtype(dtype).itemsize*width)
        arr=np.ndarray((a['count'],width),dtype=dtype,buffer=binary,offset=offset,strides=(stride,np.dtype(dtype).itemsize)).copy()
        if a.get('normalized'):arr=arr.astype(np.float64)/np.iinfo(dtype).max
        return arr
    return data,accessor


def render_review(snapshots, path, columns):
    for obj in list(bpy.context.scene.objects):
        bpy.data.objects.remove(obj,do_unlink=True)
    scene=bpy.context.scene
    rows=math.ceil(len(snapshots)/columns)
    spacing=2.25;row_height=2.6
    for i,(label,data) in enumerate(snapshots):
        obj=bpy.data.objects.new(label,data);scene.collection.objects.link(obj)
        obj.location=((columns/2-.5-i%columns)*spacing,0,(rows-1-i//columns)*row_height)
        if columns==5:obj.rotation_euler.z=math.radians(-55)
    scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.use_denoising=True
    scene.render.resolution_x=2200;scene.render.resolution_y=1200 if rows>1 else 620
    scene.render.resolution_percentage=100
    scene.world=bpy.data.worlds.new('PreviewStudio');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs['Color'].default_value=(.11,.14,.19,1)
    scene.world.node_tree.nodes['Background'].inputs['Strength'].default_value=.65
    scene.view_settings.view_transform='AgX'
    center=Vector((0,0,.9+(rows-1)*row_height*.5))
    for name,loc,power,color in [('Key',(-3,5,7),2300,(1,.88,.75)),('Fill',(5,3,4),1500,(.7,.85,1)),('Rim',(0,-4,5),2000,(.75,.85,1))]:
        light=bpy.data.lights.new(name,'AREA');light.energy=power;light.shape='DISK';light.size=6;light.color=color
        obj=bpy.data.objects.new(name,light);scene.collection.objects.link(obj);obj.location=loc
        obj.rotation_euler=(center-obj.location).to_track_quat('-Z','Y').to_euler()
    cam=bpy.data.objects.new('Camera',bpy.data.cameras.new('Camera'));scene.collection.objects.link(cam);scene.camera=cam
    cam.location=center+Vector((.35,14,2.8));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.type='ORTHO';cam.data.ortho_scale=columns*spacing+.65
    label_mat=bpy.data.materials.get('ReviewLabel') or bpy.data.materials.new('ReviewLabel')
    label_mat.use_nodes=True
    bsdf=label_mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value=(.75,.85,1,1)
    bsdf.inputs['Emission Color'].default_value=(.75,.85,1,1)
    bsdf.inputs['Emission Strength'].default_value=.6
    for i,(label,_) in enumerate(snapshots):
        x=(columns/2-.5-i%columns)*spacing;z=(rows-1-i//columns)*row_height
        curve=bpy.data.curves.new(label,'FONT');curve.body=label;curve.align_x='CENTER';curve.size=.125 if columns==5 else .17
        obj=bpy.data.objects.new(label,curve);scene.collection.objects.link(obj);obj.location=(x,.45,z-.2)
        obj.rotation_euler=cam.rotation_euler;curve.materials.append(label_mat)
        bpy.ops.mesh.primitive_cube_add(size=1,location=(x,0,z-.018))
        bpy.context.object.scale=(1.45,.025,.012)
        bpy.context.object.data.materials.append(label_mat)
    scene.render.image_settings.file_format='PNG'
    scene.render.filepath=str(path)
    bpy.ops.render.render(write_still=True)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--asset-dir',type=Path,required=True)
    parser.add_argument('--skip-review',action='store_true')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:]);folder=args.asset_dir.resolve()
    bpy.ops.wm.open_mainfile(filepath=str(folder/'CreatorRobot.blend'))
    rig=bpy.data.objects['CreatorRobot'];mesh=bpy.data.objects['CreatorRobot_Body']
    report={'engine_launched':False,'checks':[],'clips':[]}
    preparation=json.loads((folder/'preparation.json').read_text())
    expected_poses={}
    def check(condition,message):
        report['checks'].append({'check':message,'passed':bool(condition)})
        if not condition:print('VALIDATION_FAILED',message,flush=True)
    check(all(max(abs(s-1) for s in o.scale)<1e-6 for o in (rig,mesh)),'Object and armature scale is 1,1,1')
    check(abs(bpy.context.scene.unit_settings.scale_length-1)<1e-6,'Blender metric scale is 1')
    check(len(rig.data.bones)==53,'Humanoid hierarchy contains 53 bones')
    check(set(a.name for a in bpy.data.actions)==set(CLIPS),'Exactly the eight requested clips are present')
    check(all(abs(sum(g.weight for g in v.groups)-1)<1e-4 for v in mesh.data.vertices),'All vertices have normalized skin weights')
    check(all(len(v.groups)<=4 for v in mesh.data.vertices),'At most four weights per vertex')
    expected_durations=EXPECTED_DURATIONS
    reset_pose(rig);rest=coordinates(mesh)
    report['rest_bounds']={'min':rest.min(axis=0).tolist(),'max':rest.max(axis=0).tolist()}
    check(abs(float(np.ptp(rest[:,2]))-1.8)<1e-4,'Rest height is 1.8000 meters')
    check(abs(float(rest[:,2].min()))<1e-4,'Rest soles are at ground zero')
    snapshots=[];review_snapshots={}
    def snapshot(label):
        deps=bpy.context.evaluated_depsgraph_get()
        evaluated=mesh.evaluated_get(deps)
        data=bpy.data.meshes.new_from_object(evaluated)
        snapshots.append((label,data))
    for name in CLIPS:
        action=bpy.data.actions[name]
        rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0]
        start,end=action.frame_range
        check(abs((end-start)/(bpy.context.scene.render.fps/bpy.context.scene.render.fps_base)-expected_durations[name])<1e-5,name+' has the specified duration')
        check(bool(action.get('loop'))==(name in LOOPING),name+' has correct loop metadata')
        check(action.get('playback')==('loop' if name in LOOPING else 'hold_last' if name=='Death' else 'once'),name+' has correct playback metadata')
        poses=[];floor=[];heights=[];scales=[];maximum_step=0.;previous=None
        for frame in list(range(math.ceil(start),math.floor(end)+1))+[float(end)]:
            bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame))
            xyz=coordinates(mesh);floor.append(float(xyz[:,2].min()));heights.append(float(xyz[:,2].max()))
            check(np.isfinite(xyz).all(),name+' finite geometry at frame '+str(frame))
            scales.append(max(abs(s-1) for b in rig.pose.bones for s in b.scale))
            check(rig.pose.bones['root'].location.length<1e-6 and abs(rig.pose.bones['root'].rotation_quaternion.w-1)<1e-6,name+' root stays fixed at frame '+str(frame))
            if previous is not None:maximum_step=max(maximum_step,float(np.linalg.norm(xyz-previous,axis=1).max()))
            previous=xyz.copy()
            if len(poses)<1:poses.append(xyz.copy())
        last=xyz.copy()
        preview_phase={'Idle':.3,'Walk':.3,'Run':.3,'Jump':.8,'FallLoop':.25,'Land':.3,'Hit':.4,'Death':1.}[name]
        phase=start+(end-start)*preview_phase
        bpy.context.scene.frame_set(int(phase),subframe=phase-int(phase))
        snapshot(name.upper())
        moving=float(np.max(np.linalg.norm(coordinates(mesh)-poses[0],axis=1)))
        loop_error=np.linalg.norm(last-poses[0],axis=1)
        report['clips'].append({'name':name,'frame_range':[start,end],
            'ground_range': [min(floor),max(floor)],'height_range':[min(heights),max(heights)],
            'max_scale_error':max(scales),'sample_motion_meters':moving,
            'max_sample_step_meters':maximum_step,
            'loop_vertex_error_max':float(loop_error.max()),'loop_vertex_error_mean':float(loop_error.mean())})
        clearance=next(c for c in preparation['clips'] if c['name']==name)['expected_ground_clearance_meters']
        check(min(floor)>-.002 and abs(max(floor)-max(clearance))<.002,name+' body contact and intended airborne clearance are preserved')
        check(max(scales)<1e-5,name+' has no scale drift')
        check(moving>.002,name+' contains visible motion')
        check(maximum_step<.5,name+' has no sample-to-sample mesh teleport')
        if name in LOOPING:check(float(loop_error.max())<.002,name+' loop endpoints match within 2mm')
        expected_poses[name]=[]
        review_snapshots[name]=[]
        for phase in (0.,.25,.5,.75,1.):
            frame=start+(end-start)*phase
            bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame))
            expected_poses[name].append((expected_durations[name]*phase,coordinates(mesh)))
            evaluated=mesh.evaluated_get(bpy.context.evaluated_depsgraph_get())
            review_snapshots[name].append((f'{name.upper()}  {phase*100:.0f}%',bpy.data.meshes.new_from_object(evaluated)))
        if name=='Death':
            scene=bpy.context.scene
            scene.frame_set(math.ceil(end)+15)
            check(float(np.linalg.norm(coordinates(mesh)-last,axis=1).max())<.001,'Death holds its terminal pose beyond the clip end')
    report['transition_errors']=[]
    for before,after in (('Idle:0','Jump:0'),('Jump:4','FallLoop:0'),('FallLoop:4','Land:0'),('Land:4','Idle:0'),('Idle:0','Hit:0'),('Hit:4','Idle:0'),('Idle:0','Death:0')):
        a,i=before.split(':');b,j=after.split(':')
        error=float(np.linalg.norm(expected_poses[a][int(i)][1]-expected_poses[b][int(j)][1],axis=1).max())
        report['transition_errors'].append({'from':before,'to':after,'max_vertex_error_meters':error})
        check(error<.002,before+' -> '+after+' boundary poses match within 2mm')
    data,acc=read_glb(folder/'CreatorRobot.glb')
    check({a['name'] for a in data['animations']}==set(CLIPS),'GLB contains exactly the eight clips')
    check([a['name'] for a in data['animations']]==list(CLIPS),'GLB starter order begins with Idle')
    check(all(max(abs(v-1) for v in n.get('scale',[1,1,1]))<1e-5 for n in data['nodes']),'All GLB node scales are one')
    for animation in data['animations']:
        duration=max(float(acc(s['input']).max()) for s in animation['samplers'])
        check(abs(duration-expected_durations[animation['name']])<1e-5,animation['name']+' GLB retains exact clip duration')
        check(all(abs(float(acc(s['input']).min()))<1e-6 for s in animation['samplers']),animation['name']+' GLB starts at 0 seconds')
        check(all(np.isfinite(acc(s['input'])).all() and np.all(np.diff(acc(s['input']).ravel())>0) for s in animation['samplers']),animation['name']+' GLB key times increase strictly')
        for channel in animation['channels']:
            if channel['target']['path']=='scale':
                values=acc(animation['samplers'][channel['sampler']]['output'])
                check(np.max(np.abs(values-1))<1e-5,animation['name']+' GLB scale channel is unit')
    attrs=[p['attributes'] for m in data['meshes'] for p in m['primitives']]
    check(all({'POSITION','NORMAL','TANGENT','TEXCOORD_0','JOINTS_0','WEIGHTS_0'}<=set(a) for a in attrs),'Every primitive exports UV, normal, tangent and skin streams')
    positions=np.concatenate([acc(a['POSITION']) for a in attrs])
    check(abs(float(np.ptp(positions[:,1]))-1.8)<1e-4,'GLB is Y-up and 1.8000 units tall')
    check(abs(float(positions[:,1].min()))<1e-4,'GLB ground is Y=0')
    check(all(abs(acc(a['WEIGHTS_0']).sum(axis=1)-1).max()<1e-4 for a in attrs),'Exported weights sum to one')
    check(all('uri' not in i for i in data['images']),'GLB has embedded textures and no external image paths')
    check(len(data['materials'])==4 and len(data['images'])==12,'Four PBR materials, each with base color / normal / ORM')
    rig_only,_=read_glb(folder/'RigOnly/CreatorRobot_Rig.glb')
    check(not rig_only.get('animations'),'Rig-only export has no source animations')
    report['passed']=all(c['passed'] for c in report['checks'])
    (folder/'validation.json').write_text(json.dumps(report,indent=2))

    # Evaluated snapshots leave the saved authoring file untouched.
    if not args.skip_review:
        render_review(snapshots,folder/'Preview.png',columns=4)
        (folder/'MotionReview').mkdir(exist_ok=True)
        for name,items in review_snapshots.items():
            render_review(items,folder/'MotionReview'/f'{name}.png',columns=5)
    # Independently re-import the deliverable. Compare deformed surfaces in
    # meters, allowing vertices duplicated by UV/material seams in glTF.
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps=30;bpy.context.scene.render.fps_base=1.
    bpy.ops.import_scene.gltf(filepath=str(folder/'CreatorRobot.glb'))
    imported_rig=next(o for o in bpy.data.objects if o.type=='ARMATURE')
    imported_mesh=next(o for o in bpy.data.objects if o.type=='MESH' and o.name!='Icosphere')
    for track in imported_rig.animation_data.nla_tracks:track.mute=True
    report['roundtrip_pose_errors']=[]
    for name,samples in expected_poses.items():
        action=bpy.data.actions[name]
        imported_rig.animation_data.action=action;imported_rig.animation_data.action_slot=action.slots[0]
        for seconds,expected in samples:
            frame=action.frame_range[0]+seconds*30
            bpy.context.scene.frame_set(int(frame),subframe=frame-int(frame))
            actual=coordinates(imported_mesh)
            tree=KDTree(len(expected))
            for i,p in enumerate(expected):tree.insert(p,i)
            tree.balance()
            error=max(tree.find(p)[2] for p in actual)
            report['roundtrip_pose_errors'].append({'clip':name,'seconds':seconds,'max_surface_error_meters':error})
            check(error<.005,name+' GLB re-import deformation matches Blender at '+str(seconds)+'s')
    report['passed']=all(c['passed'] for c in report['checks'])
    (folder/'validation.json').write_text(json.dumps(report,indent=2))
    print('ROBOT_PREIMPORT_VALIDATION',report['passed'],len(report['checks']),flush=True)
    if not report['passed']:raise RuntimeError('See validation.json for failed checks')


if __name__=='__main__':main()
