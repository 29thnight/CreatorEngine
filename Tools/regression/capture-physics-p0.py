"""Preserve the pre-R0 physics source/corpus and a hash-audited consumer inventory."""
from pathlib import Path
import hashlib,json,re,subprocess,zipfile,datetime
repo=Path(__file__).resolve().parents[2]
out=repo/'Build/Obj/Phase19P0'
out.mkdir(parents=True,exist_ok=True)
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip()
status=subprocess.check_output(['git','status','--short'],cwd=repo,text=True)
types=['RigidBodyComponent','BoxColliderComponent','SphereColliderComponent','CapsuleColliderComponent','MeshColliderComponent','TerrainColliderComponent','CharacterControllerComponent','RagdollComponent']
uuid_text=(repo/'Engine/SceneRuntime/ComponentTypeUUID.h').read_text(encoding='utf-8-sig')
uuids={name:re.search(r'"'+name+r'"\s*,\s*"([0-9a-f-]+)"',uuid_text).group(1) for name in types}
pattern=re.compile(r'\b(?:'+ '|'.join(types)+r')\b|PhysicsManagers->|Physics->|\b(?:Cct_|Rigid_|Collider_)\w*')
files=[];consumers=[];assets=[]
for root in ['Engine','ScriptCore','ScriptBinder','Dynamic_CPP','Player','Editor','Tools/AssetCooker','Tools/regression/fixtures/physics-p0']:
    for path in (repo/root).rglob('*'):
        if not path.is_file() or path.suffix.lower() not in ('.h','.hpp','.cpp','.cs','.creator','.prefab'):continue
        if any(x in path.parts for x in ('obj','bin','Library','x64','generated')):continue
        content=path.read_bytes();text=content.decode('utf-8-sig',errors='replace')
        hits=[{'line':i,'text':line.strip()} for i,line in enumerate(text.splitlines(),1) if pattern.search(line)]
        is_asset=path.suffix.lower() in ('.creator','.prefab')
        occurrences={name:text.count(uuid)+len(re.findall(r'\b'+name+r'\b',text)) for name,uuid in uuids.items()}
        selected=path.is_relative_to(repo/'Engine/Physics') or bool(hits) or (is_asset and any(occurrences.values()))
        if not selected:continue
        relative=path.relative_to(repo).as_posix()
        record={'path':relative,'sha256':hashlib.sha256(content).hexdigest(),'bytes':len(content)}
        files.append(record)
        if hits and not is_asset:consumers.append({'path':relative,'hits':hits})
        if is_asset:assets.append(dict(record,physics_occurrences=occurrences))
for relative in ['Engine/SceneRuntime/Scene.cpp','Engine/SceneRuntime/Scene.h','Engine/SceneRuntime/Component.h','Engine/SceneRuntime/ComponentTypeUUID.h','Engine/SceneRuntime/RuntimeFrame.cpp','Engine/SceneRuntime/LifecycleRegistry.h','Engine/EngineDiagnostics/ProfileEvent.h','Engine/EngineDiagnostics/ProfileCapture.h','Engine/EngineDiagnostics/ProfileService.h','Engine/Physics/Physics.vcxproj','Directory.Build.props','Directory.Build.targets','vcpkg.json','ThirdParty/Mathematics/PROVENANCE.md']:
    path=repo/relative
    if not any(x['path']==relative for x in files):files.append({'path':relative,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size})
archive=out/('pre-r0-'+head[:8]+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for record in files:z.write(repo/record['path'],record['path'])
with zipfile.ZipFile(archive) as z:
    assert all(hashlib.sha256(z.read(x['path'])).hexdigest()==x['sha256'] for x in files)
manifest={'head':head,'captured_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'worktree_status':status,'archive':archive.name,'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'uuids':uuids,'files':files,'consumers':consumers,'assets':assets,'limitations':['occurrences are lexical, not AST reachability or component instance counts','archive preserves current worktree bytes including ignored local physics assets; not published']}
(out/'source-corpus.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'files':len(files),'consumer_files':len(consumers),'consumer_lines':sum(len(x['hits']) for x in consumers),'physics_assets':len(assets),'head':head,'archive':str(archive)}))
