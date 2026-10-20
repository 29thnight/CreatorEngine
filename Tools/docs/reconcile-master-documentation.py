"""Write source-grounded current-state sections, preserving dated evidence as history.

This is a documentation operation. It never manufactures runtime receipts or rewrites
old measurements/hashes to imply that an old execution tested the current master.
"""
import collections
import hashlib
import html
import json
import pathlib
import posixpath
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = pathlib.Path(__file__).resolve().parents[2]
sha = subprocess.check_output(['git','rev-parse','master'],cwd=ROOT,text=True).strip()
paths = set(subprocess.check_output(['git','ls-tree','-r','--name-only',sha],cwd=ROOT,text=True).splitlines())
cache = {}
source_batch = subprocess.Popen(['git','cat-file','--batch'],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE)
def read_source(path):
    if path not in cache:
        source_batch.stdin.write((sha+':'+path+'\n').encode());source_batch.stdin.flush()
        header=source_batch.stdout.readline().decode().split()
        if header[-1]=='missing':raise ValueError('Missing fixed-master source: '+path)
        raw=source_batch.stdout.read(int(header[-1]));source_batch.stdout.read(1)
        if raw.startswith((b'\xff\xfe',b'\xfe\xff')): text=raw.decode('utf-16')
        else:
            try: text=raw.decode('utf-8-sig')
            except UnicodeDecodeError:
                try: text=raw.decode('cp949')
                except UnicodeDecodeError: text=raw.decode('latin-1')
        cache[path]=(raw,text)
    return cache[path][1]

source_paths = {p for p in paths if p.startswith(('Engine/','Editor/','ScriptCore/','BuildTool/','Player/','Lattice/','Tools/','Dynamic_CPP/Assets/Shaders/','ports/')) and pathlib.Path(p).suffix.lower() in ('.h','.hpp','.cpp','.inl','.cs','.slang','.ps1','.py','.mjs','.cmake','.vcxproj','.json')}
by_name = collections.defaultdict(list)
by_stem = collections.defaultdict(list)
for p in sorted(source_paths): by_name[pathlib.PurePosixPath(p).name].append(p)
for p in sorted(source_paths):
    if pathlib.Path(p).suffix.lower() in ('.h','.hpp','.cs','.cpp','.ps1','.py'):
        by_stem[pathlib.Path(p).stem.casefold()].append(p)
plan_data=json.loads((ROOT/'docs/plans/MasterPlanAudit20261020.json').read_text(encoding='utf-8'))
fallbacks={g:[x['path'] for x in e['sources']][:3] for g,e in plan_data['evidence'].items()}
fallbacks['core']=['Engine/Utility_Framework/JobScheduler.h','Engine/Utility_Framework/ThreadPool.h']
facts={
'core':['공용 인프라의 작업 실행·완료와 컨테이너 저장 수명은 호출자 계약을 따른다. job handle의 완료·join과 저장소/자원 owner를 구분한다.','과거 PPL/독립 thread pool 이행 제안은 현재 공용 구현과 실제 소비자를 먼저 대조한다. 기반 구현의 존재가 모든 병렬 소비자의 취소·종료·비용 수용을 뜻하지 않는다.'],
'scene':['SceneManager는 GCCE domain과 Scene root를 소유한다. Scene→Entity→Component의 trace 참조와 논리적 제거·핸들 무효화를 분리한다.','일반 new/unique_ptr 기반 Scene 그래프를 새 이행 목표로 사용하지 않는다. 자산·SDK·GPU 수명은 독립 owner/완료 계약을 따른다.'],
'asset':['DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.','ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.'],
'material':['live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.','GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.'],
'graph':['제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.','소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.','queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.'],
'temporal':['camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.','SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.'],
'profile':['CPU engine frame·render submission·GPU pass·presenter 관측은 서로 다른 식별 축이다. bounded producer 기록과 손실/출처를 보존한다.','집계 메모리 표본은 객체별 native/CLR/GPU 소유 그래프가 아니다. 과거 .ceprof/ETW 기록은 그 실행 신원에만 유효하다.'],
'editor':['창·메뉴/명령과 scene 편집은 값/세대 핸들을 경계로 전달한다. 실제 Component 변경·dirty publication·Undo 수용을 분리해 확인한다.','Workspace W9 설정/프로필·Rendering 진단과 완료된 PBR-W9는 다른 작업이다. 미구현 설정을 동작하는 제품 스위치로 표시하지 않는다.'],
'animation':['Animator/AnimInstance와 공용 작업 스케줄러·pose/palette 경로가 실행 기반이다. Graph 정의와 인스턴스 재생 상태를 분리한다.','AnimationGraph 저작·재바인딩·이벤트/LOD/Player 수용과 별도 VM 채택은 기존 스케줄러 구현 완료로 대신하지 않는다.'],
'physics':['물리 쿼리는 sceneAnchor Entity, 호출자 버퍼와 PhysicsQueryResult를 사용한다. 현재 hit 타입은 PhysicsHit이며 LayerId는 신원 값이다.','접촉은 OnBeginSimulation에서 ContactStream을 등록하고 PostPhysics에서 소비한다. OnTrigger*/OnCollision* 훅과 RaycastAll을 현행 공개 API로 사용하지 않는다.'],
'audio':['AudioHost/PlaybackService·clip directory와 miniaudio/Null backend가 재생 수명을 소유한다. resident/stream 및 개별 PlaybackHandle을 구분한다.','FMOD 채택을 필수 선행으로 재도입하지 않는다. 과거 선택 씬의 종료/stream 수용과 현재 CI·설치 제품 수용은 별개다.'],
'script':['현재 managed 표면은 Component·Entity와 세대 핸들이다. OnInitialized~OnUninitializing 수명 훅과 PrePhysics/PostPhysics를 구분한다.','native 객체 수명, CLR GC, Scope 취소·reload와 자산 owner는 별도 계약이다. 미래 PrSM 효과/저작 체계를 기존 ContactStream 구현으로 판정하지 않는다.'],
'ui':['UI는 현행 Canvas/UIManager·Component와 값 기반 렌더 준비 경계를 소비한다. GC 객체와 렌더 스냅샷의 수명을 분리한다.','scene-local 입력 routing·layout dirty·UIDrawSnapshot·전역 UIManager 퇴역은 UI 계획의 실제 소비자/제품 수용으로 판정한다.'],
'build':['Editor/Player Host와 build/cook·manifest/pak·bootstrap을 분리한다. runtime 소비와 Editor authoring writer를 같은 책임으로 합치지 않는다.','로컬 package 생성은 Launcher/MSI·서명 update/rollback·clean VM 제품 수용의 증거가 아니다.'],
'math':['저장/연산 타입의 정본은 math::*와 Mathematics다. 제거된 Mathf/DirectX 수학 별칭을 새 이행 목표로 사용하지 않는다.','PhysX·GPU·managed ABI·직렬화 경계의 변환과 실제 projection/physics 제품 수용을 구분한다.'],
'serialize':['현행 서술 정본은 reflgen::schema_of/type_descriptor다. 저작 문서와 cooked archive는 공유 서술의 서로 다른 소비 정책이다.','모델/AssetSet 신원은 구현된 UUIDv8/CEMF 계약을 따른다. 다른 저작 객체의 신원까지 일괄 치환하거나 과거 UUIDv4 이주 단계를 재개하지 않는다.'],
'light':['현행 LightMapping 인터페이스와 scene 자원을 출발점으로 UV/BVH/bake의 실제 구현·소비 경계를 확인한다.','기반 인터페이스의 존재를 전체 baker 구현이나 픽셀/성능 수용으로 판정하지 않는다.'],
'network':['현재 씬·고정 물리 스텝·managed 생명주기는 로컬 실행 기반이다. wire identity·replication·transport는 별도 계약이다.','로컬 EntityHandle과 typeID를 wire 신원으로 사용하거나 물리 fixed step을 네트워크 simulation 완료로 판정하지 않는다.'],
}
def domain(file):
    n=pathlib.Path(file).stem.lower()
    if n in ('technicalguide','rhigpumemorylifetimedesign'):return 'graph'
    if n=='sdftextrendering':return 'ui'
    if re.match(r'mat\d|lx',n):return 'material'
    if re.search(r'ownedboundedpresentation|playernativepresentation|sharedvisibility|enginesynchronization|renderrg',n):return 'graph'
    if re.search(r'jobsystem|jobscheduler|pplcontainer|containerlibrary|codingconventions|githubissueremediation',n):return 'core'
    if 'hierarchyorder' in n:return 'scene'
    if 'automaticresourceediting' in n or 'phase475axis' in n:return 'asset'
    if 'phase14_5' in n:return 'ui'
    if '/measurements/' in file:return 'profile'
    if '/reac-' in file:return 'graph'
    for pattern,g in [
      ('temporal|dlss|upscal|framegeneration','temporal'),('physics|contact|physx','physics'),('audio|sound','audio'),
      ('gcce|scenecache|scenegraph|transformupdate|recttransform|entityownership|lifecycle','scene'),
      ('texture|asset|model|artifact|cooked|publish','asset'),('profil|memory|capture|measurement|etw|timing','profile'),
      ('animation|animator|socket|scheduler','animation'),('refl|serial','serialize'),('math|frustum','math'),
      ('lightmap|baker','light'),('network|replication','network'),('material|principled|ibl|blender|lattice','material'),
      ('render|rhi|queue|graph|reac|meshlet|csm|gpu|shader|frame','graph'),
      ('editor|theme|font|icon|emoji|menu|inspector|window|command','editor'),('script|component|prsm','script'),
      ('ui|canvas|text|sdf','ui'),('build|player|launch|version|packag|website','build')]:
        if re.search(pattern,n): return g
    return 'build'

def norm(file, link):
    return posixpath.normpath(posixpath.join(posixpath.dirname(file),unquote(link).replace('\\','/')))

def sources_for(file,text,g):
    found=[]
    for path in re.findall(r'(?:Engine|Editor|ScriptCore|BuildTool|Player|Lattice|Tools|Dynamic_CPP|ports)/[\w./+ -]+\.(?:h|hpp|cpp|inl|cs|slang|ps1|py|mjs|cmake|json)',text):
        if path in source_paths and path not in found: found.append(path)
    for link in re.findall(r'\]\(([^)\s]+)\)',text):
        p=norm(file,urlsplit(link).path)
        if p in source_paths and p not in found: found.append(p)
    for name in re.findall(r'\b([A-Za-z_][A-Za-z0-9_.-]+\.(?:h|hpp|cpp|inl|cs|slang|ps1|py))\b',text):
        matches=by_name.get(name,[])
        if len(matches)==1 and matches[0] not in found:found.append(matches[0])
    explicit=set(found)
    for token in dict.fromkeys(re.findall(r'\b(?:[A-Z][A-Za-z0-9_]{4,}|verify-[a-z0-9-]+)\b',text)):
        if token in ('Debug','Native','Resource','Assets','Input','Output','Window','Application','Object','Entity','Component','Transform','Camera','Sound','Texture','Material','Scene','Render','Main','Program'):continue
        matches=by_stem.get(token.casefold(),[])
        if 0 < len(matches) <= 4:
            for candidate in matches:
                if candidate not in found:found.append(candidate)
    primary=pathlib.Path(file).stem.replace('Validation','').replace('Design','')
    special=[]
    if 'RHISoftware' in file:special=['Engine/RenderEngine/RHI/RHICommandTranslator.cpp','Engine/RenderEngine/RHI/RHISubmissionThread.cpp','Engine/RenderEngine/RHI/RHISoftwareBatch.h','Engine/RenderEngine/RHI/IRHICommandTranslator.h']
    if 'CodingConventions' in file:special=['.clang-format','Engine/SceneRuntime/Entity.h','Lattice/Lattice.h']
    if 'TextureCodec' in file:special=['BuildTool/TextureCooking.cs','Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp']
    for p in special:
        if p in paths and p not in found:found.insert(0,p)
    found.sort(key=lambda p:(0 if p in special else 1,0 if primary.lower() in pathlib.Path(p).stem.lower() else 1,0 if p in explicit else 1,0 if pathlib.Path(p).suffix in ('.h','.hpp','.cs') else 1,len(p)))
    direct=bool(found)
    if not found:found=[p for p in fallbacks[g] if p in source_paths]
    return found[:8],direct

def cite(path, original):
    text=read_source(path);tokens=set(re.findall(r'`([A-Za-z_][A-Za-z0-9_:]{3,})`',original))
    candidates=[]
    for i,line in enumerate(text.splitlines(),1):
        line=line.strip()
        if not line or line.startswith(('//','/*','*','#','using System','namespace ','return ','{','}')):continue
        meaningful=re.search(r'\b(class|struct|enum|public|private|own::|gc::|math::)|\w+::\w+\s*\(|\b[A-Z]\w+\s*\(',line)
        if meaningful:
            score=sum(4 for t in tokens if t in line)+ (3 if re.search(r'gc::|own::|class .*:|public (?:static |readonly )*(?:class|struct)|::\w+\(',line) else 0)
            candidates.append((score,i,line))
    candidates.sort(key=lambda x:(-x[0],x[1]));selected=sorted(candidates[:3],key=lambda x:x[1])
    if path=='.clang-format':selected=[(0,i,line.strip()) for i,line in enumerate(text.splitlines(),1) if re.match(r'^(InsertBraces|BreakBeforeBraces|IndentWidth|NamespaceIndentation|PointerAlignment):',line)]
    if not selected:selected=[(0,1,text.splitlines()[0])]
    raw=cache[path][0]
    blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    return {'path':path,'blob':blob,'sha256':hashlib.sha256(raw).hexdigest(),'lines':[{'line':i,'text':line} for _,i,line in selected]}

changes=[]
aliases={'Engine/RenderEngine/GpuMeshletVisibility.cpp':'Engine/RenderEngine/GpuGeometryVisibility.cpp',
         'docs/analysis/reac-20261019/baseline/provenance/report.html':'docs/analysis/measurements/20261019-remeasure-10/report.html',
         'docs/analysis/reac-20261019/baseline/provenance/declare-analysis/index.html':'docs/analysis/measurements/20261019-remeasure-10/declare-analysis/index.html',
         'Engine/RenderEngine/MaterialGraphSceneLod.h':'Engine/RenderEngine/MaterialGraphSceneGeometryStage.inl'}
def resolve(file,link):
    if link.startswith(('http:','https:','mailto:','data:','javascript:','codex:','vscode:','#')):return link,None
    try:u=urlsplit(html.unescape(link))
    except ValueError:return link,None
    if u.scheme or not u.path or u.path.startswith('/'):return link,None
    dest=norm(file,u.path)
    # Every portable link must exist in the checkout or in the fixed-master tree.
    local_run = any(x in dest.split('/') for x in ('Build','Artifacts','artifacts')) and dest not in paths
    if (ROOT/dest).exists() and not local_run:return link,None
    replacement=aliases.get(dest)
    if not replacement:
        same=by_name.get(pathlib.PurePosixPath(dest).name,[])
        if len(same)==1 and dest.startswith(('Engine/','Editor/','ScriptCore/','Tools/','ScriptBinder/')):replacement=same[0]
    if replacement:
        new=posixpath.relpath(replacement,posixpath.dirname(file))
        if u.fragment:new+='#'+u.fragment
        changes.append({'file':file,'before':link,'after':new,'reason':'current source relocation'})
        return new,None
    reason='로컬 실행 산출물 미보존' if any(x in dest.split('/') for x in ('Build','Artifacts','artifacts')) or pathlib.Path(dest).suffix in ('.png','.log','.json','.ceprof','.etw','.rdc') else '현행 master에 없는 이전/제안 경로'
    changes.append({'file':file,'before':link,'after':None,'reason':reason})
    return None,reason

def repair_markdown(file,text):
    # Do not mistake C++ lambdas or samples in fenced code for Markdown links.
    chunks=re.split(r'(^```[^\n]*\n.*?^```[^\n]*$)',text,flags=re.M|re.S)
    def fix(m):
        label,target=m.group(1),m.group(2).strip()
        link=target[1:target.find('>')] if target.startswith('<') else target.split(' "',1)[0]
        new,reason=resolve(file,link)
        if new is None:return label+' (`'+link+'` — '+reason+'; 당시 기록으로만 참조)'
        return m.group(0).replace(link,new,1)
    for i in range(0,len(chunks),2):chunks[i]=re.sub(r'!?\[([^\]\n]*)\]\(([^)\n]+)\)',fix,chunks[i])
    return ''.join(chunks)

def repair_html(file,text):
    def anchor(m):
        new,reason=resolve(file,html.unescape(m.group(2)))
        if new is None:return m.group(3)+' <small class="missing-evidence">('+html.escape(reason)+' · <code>'+html.escape(m.group(2))+'</code>)</small>'
        return m.group(0).replace(m.group(2),html.escape(new,quote=True),1)
    text=re.sub(r'<a\b([^>]*?\bhref=["\'])([^"\']+)["\'][^>]*>(.*?)</a>',anchor,text,flags=re.S|re.I)
    def image(m):
        new,reason=resolve(file,html.unescape(m.group(2)))
        if new is None:return '<span class="missing-evidence">'+html.escape(reason)+' · <code>'+html.escape(m.group(2))+'</code></span>'
        return m.group(0).replace(m.group(2),html.escape(new,quote=True),1)
    return re.sub(r'<img\b([^>]*?\bsrc=["\'])([^"\']+)["\'][^>]*>',image,text,flags=re.I)

documents=[]
for path in sorted((ROOT/'docs').rglob('*.md')):
    file=path.relative_to(ROOT).as_posix()
    if '/dist/' in file or file.startswith('docs/plans/'):continue
    original=path.read_text(encoding='utf-8-sig').replace('\r\n','\n')
    if '<!-- master-doc-review:end -->' in original:
        title=original.splitlines()[0]
        historical=original.split('<!-- master-doc-review:end -->',1)[1].strip()
        if historical.startswith('<details data-master-history>'):
            historical=historical.split('</summary>',1)[1].rsplit('</details>',1)[0].strip()
        original=title+'\n\n'+historical
    original=repair_markdown(file,original)
    g=domain(file);source_list,direct=sources_for(file,original,g);evidence=[cite(p,original) for p in source_list]
    title=original.splitlines()[0] if original.startswith('# ') else '# '+path.stem
    body=original[len(title):].strip() if original.startswith('# ') else original
    prefix=posixpath.relpath('docs/analysis/MasterDocumentationAudit20261020.html',posixpath.dirname(file))
    current=[title,'','<!-- master-doc-review:start -->','## 2026-10-20 master 구현 대조','',f'기준: `{sha}`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.','',
             f'[전체 대조·링크 정리 원장]({prefix})','',
             '### 현행 계약과 이 문서의 적용 범위','']
    specific=[]
    if pathlib.Path(file).stem=='TechnicalGuide':specific=['현재 제품 기반은 C++23·DX12/Vulkan·CoreCLR/.NET 10·miniaudio/Null이다. live rendering은 C++ Pass와 명시적 graph 버전·dependency order를 소비한다.','SceneManager의 GCCE domain/root와 Scene→Entity→Component trace, native 자산 owner, CLR GC와 GPU retirement를 별도 경계로 다룬다. 리플렉션 서술 정본은 reflgen이다.','managed 물리 표면은 PhysicsHit·sceneAnchor·호출자 버퍼와 ContactStream을 사용한다. 구 SRP/C# pipeline 저작 전환은 취소됐으며 별도 제품 수용·패키징·성능 결과는 해당 신원의 기록으로 구분한다.']
    if 'CodingConventions' in file:
        specific=['포매팅 정본은 저장소 .clang-format이다. 현재는 InsertBraces=true, Allman 중괄호, 공백 4칸과 NamespaceIndentation=All을 사용한다. 이전의 짧은 if/for 또는 namespace 들여쓰기 생략 예시는 현행 규약으로 사용하지 않는다.','포인터·참조는 타입 왼쪽 정렬이다. 엔진 공개 객체와 기반 라이브러리의 기존 명명 책임은 구분하되, 소스 포맷 충돌은 현재 .clang-format을 우선한다.']
    if 'ArtifactRetention' in file:specific=['이 문서는 실행 산출물 보존 정책이다. 산출물 경로·실행 신원·해시는 작성 당시 실행의 기록이며, 파일이 없는 경우 현재 검증 근거로 사용할 수 없다.','현재 checkout에 없는 Build/Artifacts 링크는 비활성 기록으로 전환한다. 현재 master 재실행이나 원본 측정 복구를 의미하지 않는다.']
    if 'MaterialGraphScenePacket' in file or 'MaterialGraphMeshSurface' in file:specific=['GPU scene packet과 resident/visibility·REAC 경로는 live graph의 구현 기반이다. 과거 EnhancedSceneRenderer 설치 미완료·단계적 도입 표기는 이전 설계 기록으로만 보존한다.','공유 LX graph 기술과 METEOR material의 scene/view/geometry stage 소비 경계를 따른다. 구현된 설치를 후속 작업으로 중복 계획하지 않으며, 출력·오류·전체 비용 수용은 별도 검증한다.']
    if 'TextureCodecBoundary' in file:specific=['텍스처 cook와 GPU-ready 소비·typed 이미지 owner 경계는 구현돼 있다. 과거 미착수·raw DirectXTex 소유 노출 진단은 현재 상태로 재사용하지 않는다.','TextureAssetRuntime의 upload/retention과 BuildTool의 codec·포맷 선택을 분리한다. 지원 포맷과 실패 경로는 현재 선언·검증 도구를 기준으로 확인하고 당시 단일 포맷 실험을 전체 지원 범위로 확대하지 않는다.']
    for fact in specific or facts[g]:current.append('- '+fact)
    if re.search(r'\bSRP\b|CSharpRenderPipeline|ScriptableRenderPipeline',original):current.append('- 구 SRP 및 C# pipeline 저작 전환 제안은 취소됐다. 필요한 Pass는 현재 C++ 계약으로 작성하며, 별도 저작 체계는 실제 요구가 생기면 새 미래 계획으로 검토한다. 아래 원래 제안은 활성 계획이 아니다.')
    if not direct:current+=['','이 문서는 연구/운영/표현 가이드 성격이므로, 아래 파일은 관련 엔진 경계의 근거다. 문서의 제안 전체가 구현됐다는 증거로 해석하지 않는다.']
    current+=['','### 해당 구현에서 확인한 선언·소유 경계','']
    for e in evidence:
        for row in e['lines']:
            url=f'https://github.com/29thnight/CreatorEngine/blob/{sha}/{e["path"]}#L{row["line"]}'
            current+=['- ['+e['path']+':'+str(row['line'])+']('+url+') — `'+row['text'].replace('`','')+'`']
    current+=['','### 이전 근거의 유효 범위','',
              '기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.','',
              '<!-- master-doc-review:end -->','','<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>','',body,'','</details>','']
    path.write_text('\n'.join(line.rstrip() for line in current),encoding='utf-8')
    hp=path.with_suffix('.html');old_ids=[]
    if hp.exists():old_ids=sorted(set(html.unescape(x) for x in re.findall(r'\bid=["\']([^"\']+)',hp.read_text(encoding='utf-8-sig'))))
    baseline=read_source(file);baseline_raw=cache[file][0]
    documents.append({'path':file,'html':hp.relative_to(ROOT).as_posix(),'group':g,'sourceMode':'document references' if direct else 'related runtime boundary','sources':evidence,'previousAnchors':old_ids,'baselineRevision':sha,'baselineGitBlob':hashlib.sha1(b'blob '+str(len(baseline_raw)).encode()+b'\0'+baseline_raw).hexdigest(),'baselineSha256':hashlib.sha256(baseline_raw).hexdigest(),'reviewInputSha256':hashlib.sha256(original.encode()).hexdigest()})

# Standalone diagrams/reports retain their authored interactions and historical data.
standalone=[]
for path in sorted((ROOT/'docs').rglob('*.html')):
    file=path.relative_to(ROOT).as_posix()
    if any(x in file for x in ('/dist/','/plans/')) or file in ('docs/RefactoringPlanDashboard.html','docs/index.html','docs/api/index.html','docs/analysis/MasterDocumentationAudit20261020.html') or file.endswith('.src.html') or path.with_suffix('.md').exists():continue
    src=path.with_name(path.stem+'.src.html');editable=src if src.exists() else path
    content=editable.read_text(encoding='utf-8-sig')
    content=repair_html(file,content)
    if 'data-full-master-review' not in content:
        relative=posixpath.relpath('docs/analysis/MasterDocumentationAudit20261020.html',posixpath.dirname(file))
        notice=f'<aside data-full-master-review style="padding:1rem;margin:1rem;border:1px solid #888"><strong>2026-10-20 master `{sha}` 대조</strong><p>이 화면의 날짜별 측정·구조 그림은 작성 당시 기록이다. 현행 객체·배선·API와 소스 근거는 <a href="{relative}">현재 master 전체 문서 대조</a>를 우선한다. 미보존 로컬 산출물은 실행 증거로 재사용하지 않는다.</p></aside>'
        content=re.sub(r'(<body\b[^>]*>)',lambda m:m.group(1)+notice,content,count=1,flags=re.I)
    editable.write_text(content,encoding='utf-8')
    standalone.append({'path':file,'source':editable.relative_to(ROOT).as_posix(),'group':domain(file)})

manifest={'master':sha,'date':'2026-10-20','dashboardSha256':hashlib.sha256((ROOT/'docs/RefactoringPlanDashboard.html').read_bytes()).hexdigest(),
          'documents':documents,'standalone':standalone,'linkChanges':changes,'api':'docs/api/reference.json',
          'policy':'Current declarations and ownership boundaries precede historical descriptions. Historical run receipts are never regenerated or rehashed as current acceptance.'}
(ROOT/'docs/analysis/MasterDocumentationAudit20261020.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'documents':len(documents),'standalone':len(standalone),'linkRepairs':len(changes),'sourceFiles':len(cache),'master':sha}))
source_batch.stdin.close();source_batch.wait()
