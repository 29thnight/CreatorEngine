"""N1 role-specific dependency gate; not a general C++ compiler/preprocessor.
Authoring facades remain local runtime consumers. Network/neutral public surfaces
must not acquire their backend transitively. Binary parser linkage is N9 scope.
"""
import argparse, hashlib, json, pathlib, re, sys
EXTENSIONS={'.h','.hpp','.inl','.ixx','.cpp','.inc'}
RUNTIME=('Player/','Engine/RuntimeHost/','Engine/SceneRuntime/','Engine/RenderEngine/','Engine/Physics/','Engine/Network/')
NEUTRAL=('Engine/Utility_Framework/Authoring/DiagnosticDocument.h','Engine/Utility_Framework/Authoring/AuthoringDocument.h',
 'Engine/Utility_Framework/Authoring/AuthoringNodeView.h','Engine/Utility_Framework/Reflection/TypeTrait.h',
 'Engine/SceneRuntime/ComponentFactory.h','Engine/SceneRuntime/SceneObservation.h','Tools/regression/ArchiveSchemaFixture.h')
FACADES={'Engine/Utility_Framework/'+name for name in ('AuthoringReadNode.h','AuthoringWriteNode.h',
 'AuthoringParsedDocument.h','AuthoringNodeViewAccess.h','AuthoringDocumentAccess.h','AuthoringCookedDocument.h',
 'ReflectionYml.h','ReflectionTypedYml.h','ReflgenAuthoring.h','ReflgenAuthoringSerializers.h')}
LEX=re.compile(r'R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',re.S)
BACKEND_INCLUDE=re.compile(r'(?:^|/)(?:yaml-cpp|nlohmann|ryml|c4/yml)(?:/|\.|$)|^c4/format\.hpp$',re.I)
BACKEND_SYMBOL=re.compile(r'\b(?:YAML|ryml|nlohmann|c4)\b')
INCLUDE=re.compile(r'^\s*#\s*(include|include_next)\s*([^\r\n]+)',re.M)
IMPORT=re.compile(r'\b(?:export\s+)?import\s+([A-Za-z_][\w.:]*)\s*;')
class BoundaryError(ValueError):pass

def forms(text):
    text=text.replace('\\\r\n','').replace('\\\n','')
    comments=LEX.sub(lambda m: ('\n'*m[0].count('\n')) if m[0].startswith(('//','/*')) else m[0],text)
    symbols=LEX.sub(lambda m:'\n'*m[0].count('\n'),comments)
    return comments,symbols

def snapshot(root):
    files={}
    for folder in ('Engine','Player','Lattice'):
        for p in (root/folder).rglob('*'):
            if p.is_file() and p.suffix in EXTENSIONS:files[p.relative_to(root).as_posix()]=p.read_text(encoding='utf-8-sig',errors='surrogateescape')
    f='Tools/regression/ArchiveSchemaFixture.h';files[f]=(root/f).read_text(encoding='utf-8-sig')
    return files

def check(files):
    lower={p.lower():p for p in files};names={}
    for p in files:names.setdefault(pathlib.PurePosixPath(p).name.lower(),[]).append(p)
    roots=[p for p in files if p.startswith(RUNTIME)]
    for prefix in RUNTIME:
        if not any(p.startswith(prefix) for p in files):raise BoundaryError('N1_REGISTRATION: runtime root missing '+prefix)
    for p in NEUTRAL+tuple(FACADES):
        if p not in files:raise BoundaryError('N1_REGISTRATION: missing required neutral surface '+p)
    if not any(p.startswith('Engine/Network/') for p in files):raise BoundaryError('N1_REGISTRATION: Network root missing')
    parsed={p:forms(text) for p,text in files.items()}
    def dependencies(p):
        result=[];comments,symbols=parsed[p]
        for match in INCLUDE.finditer(comments):
            raw=match[2].strip();header=re.fullmatch(r'[<"]([^>"\r\n]+)[>"]\s*',raw)
            if match[1]=='include_next' or not header:raise BoundaryError('N1_INCLUDE: nonliteral include '+p+': '+raw)
            name=header[1].replace('\\','/')
            if BACKEND_INCLUDE.search(name):raise BoundaryError('N1_BACKEND_INCLUDE: '+p+' -> '+name)
            options=[(pathlib.PurePosixPath(p).parent/name).as_posix(),name]
            # Normalize ../ without needing files on disk (also used by canaries).
            import posixpath
            options=[posixpath.normpath(n).lower() for n in options]
            selected=next((lower[n] for n in options if n in lower),None)
            if selected:result.append(selected)
            else:
                candidates=[q for q in files if q.lower().endswith('/'+name.lower())]
                if not candidates:candidates=names.get(pathlib.PurePosixPath(name).name.lower(),[])
                result.extend(candidates)
        for name in IMPORT.findall(symbols):
            result.extend(q for q in files if pathlib.PurePosixPath(q).stem==name)
        return result
    visited=set();edges=set()
    def walk(p,role,trail):
        key=(role,p)
        if key in visited:return
        visited.add(key);comments,symbols=parsed[p]
        if BACKEND_SYMBOL.search(symbols):raise BoundaryError('N1_BACKEND_TYPE: '+' -> '.join(trail+[p]))
        if role=='network' and (re.search(r'\b(?:Authoring|Diagnostics)\s*::',symbols) or pathlib.PurePosixPath(p).name.startswith(('Authoring','ReflgenAuthoring','ReflectionYml','ReflectionTypedYml','DiagnosticDocument'))):
            raise BoundaryError('N1_NETWORK_ARCHIVE: '+' -> '.join(trail+[p]))
        for child in dependencies(p):
            edges.add((p,child))
            if role=='runtime' and child in FACADES:continue
            walk(child,role,trail+[p])
    for p in roots:walk(p,'network' if p.startswith('Engine/Network/') else 'runtime',[])
    for p in NEUTRAL:walk(p,'neutral',[])
    callers=[]
    for p in roots:
        for m in re.finditer(r'\b(?:ParsedDocument|WriteDocument)\s*::\s*(ParseText|ParseFilePath|ParseFile|ParseCooked)\s*\(',parsed[p][1]):
            callers.append({'file':p,'line':parsed[p][1][:m.start()].count('\n')+1,'api':m[1]})
    return {'result':'N1_SOURCE_BOUNDARY_OK','runtimeSources':len(roots),'neutralRoots':list(NEUTRAL),
       'visitedRoleFiles':len(visited),'includeEdges':len(edges),'localFacadeBoundaries':sorted(FACADES),
       'runtimeFacadeCallers':callers,'sourceHashes':{p:hashlib.sha256(files[p].encode('utf-8',errors='surrogateescape')).hexdigest() for _,p in sorted(visited)}}

def check_json(path):
    d=json.loads(path.read_text(encoding='utf-8'))
    assert d['schemaVersion']==1 and d['source']=='product-live'
    assert d['flag'] is True and d['disabled'] is False and d['booleanText']=='true' and d['numberText']=='123'
    assert d['empty']=='' and d['escape']=='quoted "line"\nslash\\'
    assert d['max']==2**64-1 and d['min']==-2**63 and d['fraction']==0.125
    assert d['temporary-key']=='temporary-value' and d['draws']==[{'meshId':'00000000-0000-0000-0000-000000000001','frameId':17,'visible':True}]
    assert d['emptyArray']==[] and d['emptyObject']=={}
    print('N1_DIAGNOSTIC_JSON_OK types=12 borrowedLifetime=1')
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=pathlib.Path);parser.add_argument('--output',type=pathlib.Path);parser.add_argument('--diagnostic',type=pathlib.Path);args=parser.parse_args()
    try:
        if args.diagnostic:check_json(args.diagnostic)
        else:
            result=check(snapshot(args.root));args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('N1_SOURCE_BOUNDARY_OK runtime='+str(result['runtimeSources'])+' closure='+str(result['visitedRoleFiles']))
    except (ValueError,AssertionError,OSError) as e:print(str(e),file=sys.stderr);sys.exit(1)
