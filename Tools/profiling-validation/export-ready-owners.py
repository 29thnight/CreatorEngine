"""Attribute Prepare Ready windows to actual scheduled CPU occupants, without summing CPU time as wall time."""
import argparse,bisect,collections,csv,hashlib,json
from pathlib import Path

def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
def analyze(directory,timeline):
    started=load(directory/'started.json');frames=load(directory/'frames.json');data=load(timeline)
    hz=started['hz'];origin=frames[0]['beginQpc'];pid=started['processId'];main=started['threadIds'][0]
    stamp=lambda q:(q-origin)*1000/hz
    processes=collections.defaultdict(list);threads=collections.defaultdict(list)
    with (directory/'ready-metadata.csv').open(encoding='utf-8-sig') as stream:
        for row in csv.reader(stream):
            if row[0]=='P':processes[int(row[2])].append((int(row[1]),row[4]))
            elif row[0]=='T':
                tid,p=int(row[3]),int(row[2])
                if p or not tid:threads[tid].append((int(row[1]),p,row[6]))
    for rows in list(processes.values())+list(threads.values()):rows.sort()
    snapshots={x['tid']:x for x in started.get('threadSnapshots',[])}
    def at(rows,time):
        if not rows:return None
        i=bisect.bisect_right([x[0] for x in rows],time)-1
        return rows[max(0,i)]
    actors=[];actor_map={}
    def actor(tid,time):
        record=at(threads.get(tid,[]),time);owner=record[1] if record else None
        name=record[2] if record else ''
        if owner==pid and tid in snapshots:name=snapshots[tid]['name'] or name
        image=at(processes.get(owner,[]),time);image=image[1] if image else 'unknown'
        if tid==0:category='idle'
        elif owner is None:category='unknown'
        elif owner!=pid:category='external'
        elif tid==main:category='render'
        elif name.startswith('Creator.JobWorker.') or name=='Creator.JobDispatcher':category='worker'
        elif name=='Creator.RHI.Submission' or name=='[RHIThread]':category='rhi_submission'
        elif name=='Creator.RHI.Translation' or name=='[RHITranslation]':category='rhi_translation'
        else:category='same_process_other'
        key=(tid,owner,name,image,category)
        if key not in actor_map:
            actor_map[key]=len(actors);actors.append(dict(tid=tid,pid=owner,name=name,image=image,category=category))
        return actor_map[key]
    end=max(f['endQpc'] for f in frames);segments=collections.defaultdict(list);last={};switches=[];target_priorities=[]
    with (directory/'ready-scheduler.csv').open(encoding='utf-8-sig') as stream:
        for row in csv.reader(stream):
            if row[0]!='S':continue
            q,core,incoming,outgoing,state,reason,priority,old_priority,ideal=map(int,row[1:])
            if core in last:
                begin,tid,pri=last[core]
                if q>begin:segments[core].append((begin,q,tid,pri))
            last[core]=(q,incoming,priority)
            if incoming==main and any(f['prepareBeginQpc'] <= q < f['prepareEndQpc'] for f in frames):
                target_priorities.append([stamp(q),priority,core])
    for core,(begin,tid,priority) in last.items():
        if begin<end:segments[core].append((begin,end,tid,priority))
    core_count=max(started.get('logicalProcessors',0),max(segments,default=-1)+1)
    starts={c:[x[0] for x in s] for c,s in segments.items()}
    ready=[];last_core=None
    for begin,duration,state,core in data['lanes'][str(main)]['states']:
        if state==1:last_core=core
        if state!=2:continue
        left=origin+round(begin*hz/1000);right=origin+round((begin+duration)*hz/1000)
        for f in frames:
            a=max(left,f['prepareBeginQpc']);b=min(right,f['prepareEndQpc'])
            if b>a:ready.append((a,b,f['step'],last_core))
    occupancy=[];all_totals=collections.Counter();last_totals=collections.Counter();categories=collections.Counter()
    for a,b,step,previous in ready:
        for core in range(core_count):
            s=segments.get(core,[]);i=max(0,bisect.bisect_right(starts.get(core,[]),a)-1);cursor=a
            while i<len(s) and s[i][0]<b:
                left,right,tid,pri=s[i];lo=max(a,left);hi=min(b,right)
                if hi>lo:
                    if lo>cursor:
                        unknown=actor(-1,cursor);occupancy.append([stamp(cursor),(lo-cursor)*1000/hz,core,unknown,-1])
                        all_totals[(unknown,-1)]+=(lo-cursor)*1000/hz
                        categories['unknown']+=(lo-cursor)*1000/hz
                        if previous==core:last_totals[(unknown,-1)]+=(lo-cursor)*1000/hz
                    index=actor(tid,lo);ms=(hi-lo)*1000/hz
                    occupancy.append([stamp(lo),ms,core,index,pri]);all_totals[(index,pri)]+=ms;categories[actors[index]['category']]+=ms
                    if previous==core:last_totals[(index,pri)]+=ms
                    cursor=hi
                i+=1
            if cursor<b:
                index=actor(-1,cursor);ms=(b-cursor)*1000/hz
                occupancy.append([stamp(cursor),ms,core,index,-1]);all_totals[(index,-1)]+=ms;categories['unknown']+=ms
                if previous==core:last_totals[(index,-1)]+=ms
    ready_ms=sum((b-a)*1000/hz for a,b,_,_ in ready)
    def totals(counter):return [dict(actor=k[0],priority=k[1],cpuMs=v) for k,v in counter.most_common()]
    assert abs(sum(all_totals.values())-ready_ms*core_count)<0.02,(sum(all_totals.values()),ready_ms*core_count)
    result=dict(schema=1,scope='Prepare Ready intersections only',pid=pid,mainTid=main,frames=len(frames),cores=core_count,
        readyMs=ready_ms,readyMsPerFrame=ready_ms/len(frames),actors=actors,
        readyFields=['beginMs','durationMs','step','lastCpu'],ready=[[stamp(a),(b-a)*1000/hz,s,c] for a,b,s,c in ready],
        occupancyFields=['beginMs','durationMs','cpu','actorIndex','priority'],occupancy=sorted(occupancy),
        allCpuTotals=totals(all_totals),lastCpuTotals=totals(last_totals),categoryCpuMs=dict(categories),
        targetPriorityFields=['timeMs','priority','cpu'],targetPriorities=target_priorities,
        source=dict(metadata='ready-metadata.csv',scheduler='ready-scheduler.csv'),
        interpretation='Occupancy is CPU residency; sum across cores is CPU-ms, not target wait wall-ms. Last CPU is observational, not exclusive causation.')
    save(directory/'ready-owners.json',result);data['readyOwners']=result
    for tid,lane in data['lanes'].items():
        snap=snapshots.get(int(tid))
        if snap and snap.get('name') and snap['name'] not in lane['names']:lane['names'].append(snap['name'])
    save(timeline,data)
    return dict(name=directory.name,readyMsPerFrame=ready_ms/len(frames),categories=dict(categories),cores=core_count,
                dominant=[dict(**actors[x['actor']],priority=x['priority'],cpuMs=x['cpuMs']) for x in totals(all_totals)[:12]],
                targetPriorities=sorted({x[1] for x in target_priorities}),foregroundPid=started.get('foregroundPid'),
                processPriorityClass=started.get('processPriorityClass'),affinityMask=started.get('affinityMask'))

def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--timeline',type=Path);o=p.parse_args();r=o.root.resolve()
    if o.timeline:
        print(json.dumps(analyze(r,o.timeline.resolve()),ensure_ascii=False));return
    results=[analyze(r/x['name'],r/'timeline'/(x['name']+'.json')) for x in load(r/'runs.json')]
    save(r/'ready-summary.json',dict(schema=1,runs=results));print('READY_OWNERS_EXPORTED',len(results))
if __name__=='__main__':main()
