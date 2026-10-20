"""Reconcile the curated API reference with declarations in a fixed Git revision."""
import hashlib
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
sha = subprocess.check_output(['git', 'rev-parse', 'master'], cwd=ROOT, text=True).strip()
target = ROOT / 'docs/api/reference.json'
data = json.loads(target.read_text(encoding='utf-8'))
source_cache = {}
changes = []

def source(path):
    if path not in source_cache:
        raw = subprocess.check_output(['git', 'show', sha + ':' + path], cwd=ROOT)
        try: text = raw.decode('utf-8-sig')
        except UnicodeDecodeError: text = raw.decode('cp949')
        source_cache[path] = (raw, text)
    return source_cache[path][1]

def type_region(text, name):
    match = re.search(r'\b(?:class|struct|enum)\s+' + re.escape(name) + r'\b[^;{]*\{', text)
    if not match: return text, 0
    start = match.end(); depth = 1; i = start
    while i < len(text) and depth:
        if text[i] == '{': depth += 1
        elif text[i] == '}': depth -= 1
        i += 1
    return text[start:i-1], start

def declaration(text, name, kind, scope):
    region, offset = type_region(text, scope)
    if scope == 'RenderBackend': region, offset = text, 0
    needle = name.split('<')[0].split('(')[0]
    if needle == 'constructor': needle = scope
    # A constructor is outside its own body, so search the type region normally.
    matches = []
    for m in re.finditer(r'(?m)^\s*([^\n]*\b' + re.escape(needle) + r'\b[^\n]*)', region):
        line = m.group(1).strip()
        prefix = re.split(r'=>|\{', line, maxsplit=1)[0]
        token_pattern = r'\b' + re.escape(needle) + (r'\s*<' if '<' in name else r'\b')
        if not re.search(token_pattern, prefix): continue
        if needle == scope and not re.search(re.escape(needle) + r'\s*\(', prefix): continue
        if line.startswith(('/', '*', '#')) or (scope not in ['EnhancedRenderGraph','RGHandle','RGTextureDesc','RenderBackend'] and not line.startswith('public ')):
            continue
        if scope in ['EnhancedRenderGraph','RGHandle','RGTextureDesc','RenderBackend'] and (line.startswith(('return ', 'if ', 'const auto ', 'using ')) or ('(' in line and not re.search(re.escape(needle)+r'\s*(?:<[^>]+>)?\s*\(',line))):
            continue
        position = offset + m.start(1)
        tail = text[position:]
        if kind in ('enum','value'):
            raw = tail.split('\n',1)[0].strip().rstrip(',')
        elif re.search(re.escape(needle) + r'\s*(?:<[^>]+>)?\s*\(', prefix):
            count = 0; seen = False; end = 0
            for end,c in enumerate(tail):
                if c == '(': count += 1; seen = True
                elif c == ')':
                    count -= 1
                    if seen and count == 0: break
            raw = tail[:end+1]
            rest = tail[end+1:]
            constraint = re.match(r'\s*(where\s+[^\n{;=]+)',rest)
            if constraint: raw += ' ' + constraint.group(1).strip()
            if scope in ['EnhancedRenderGraph','RGHandle','RGTextureDesc','RenderBackend']:
                suffix = re.match(r'\s*(const\b[^;{]*|noexcept\b[^;{]*)',rest)
                if suffix: raw += ' '+suffix.group(1).strip()
                raw += ';'
        elif kind == 'field':
            raw = tail.split(';',1)[0].strip()+';'
        else:
            raw = re.split(r'=>|\{',tail,maxsplit=1)[0].strip().split('\n')[0].strip()
            arrow = tail.find('=>'); brace = tail.find('{')
            accessor = 'get;'
            if brace >= 0 and (arrow < 0 or brace < arrow):
                depth = 1; end = brace + 1
                while end < len(tail) and depth:
                    if tail[end] == '{': depth += 1
                    elif tail[end] == '}': depth -= 1
                    end += 1
                body = tail[brace+1:end-1]
                setter = re.search(r'(?:(private|protected|internal)\s+)?\bset\b',body)
                if setter: accessor += ' ' + ((setter.group(1)+' ') if setter.group(1) else '')+'set;'
            raw += ' { '+accessor+' }'
        raw = re.sub(r'\s+',' ',raw).strip()
        matches.append({'signature':raw,'line':text.count('\n',0,position)+1,'sourceDeclaration':text[position:position+len(m.group(1))].strip()})
    return matches

physics = next(p for p in data['pages'] if p['id']=='script/Physics')
physics['summary'] = '씬을 선택하는 Entity와 호출자 버퍼를 사용하는 물리 쿼리·접촉 관측 API입니다.'
physics['note'] = 'sceneAnchor는 살아 있는 씬의 Entity입니다. 방향은 단위 벡터여야 합니다. Written 범위만 읽고 Error·RequiredCapacity·Truncated를 확인합니다. 접촉 관측과 역할 등록은 OnBeginSimulation에서 수행합니다.'
physics['members'] = [[n,'',desc,kind] for n,desc,kind in [
 ('AllLayers','쿼리의 전체 레이어 마스크 상수.','field'),('MaxQueryCapacity','단일 쿼리 버퍼 상한.','field'),
 ('MaxBatchRequests','배치 요청 상한.','field'),('MaxBatchHitCapacity','배치 hit 저장소 상한.','field'),
 ('Raycast','sceneAnchor의 씬에서 광선 쿼리. 결과와 용량/잘림 상태를 함께 반환.','method'),
 ('OverlapSphere','sceneAnchor의 씬에서 구 겹침 쿼리.','method'),('QueryBatch','범위가 겹치지 않는 요청들을 한 owner read window에서 처리.','method'),
 ('ObserveContacts','OnBeginSimulation에서 접촉 스트림을 등록. owner-thread 수명 계약을 따른다.','method'),
 ('BindContactRole','현재 시뮬레이션에 한정된 명시적 shape 역할 등록.','method')]]
hit = next(p for p in data['pages'] if p['id'] in ('script/RaycastHit','script/PhysicsHit'))
hit['id'] = 'script/PhysicsHit'
data.setdefault('aliases', {})['script/RaycastHit'] = 'script/PhysicsHit'
hit.update(title='PhysicsHit',signature='public struct PhysicsHit',summary='현재 물리 쿼리의 값 결과입니다. ObjectHandle은 내부 필드이며 Entity로 관찰합니다.',note='이전 RaycastHit 문서 주소를 보존합니다. 현재 공개 타입은 PhysicsHit이며 Layer는 LayerId(ulong)로 대체됐습니다. Written 범위 밖 결과를 읽지 않습니다.')
hit['members'] = [[n,'',desc,kind] for n,desc,kind in [('ComponentId','컴포넌트 신원.','field'),('ShapeId','shape 신원.','field'),('Face','face 인덱스.','field'),('LayerId','레이어 신원. 비트 마스크와 구분.','field'),('Point','교차 위치.','field'),('Normal','교차 법선.','field'),('Distance','교차 거리.','field'),('Entity','네이티브 핸들의 비소유 Entity 관찰 값.','property'),('HasLocation','위치 정보가 있는지 확인.','property')]]
component = next(p for p in data['pages'] if p['id']=='script/Component')
retired = {'OnTriggerEnter','OnTriggerStay','OnTriggerExit','OnCollisionEnter','OnCollisionStay','OnCollisionExit'}
component['members'] = [m for m in component['members'] if m[0] not in retired]
contact_note = '제거된 OnTrigger*/OnCollision* 훅 대신 OnBeginSimulation에서 Physics.ObserveContacts를 등록하고 PostPhysics에서 ContactStream을 읽습니다.'
if contact_note not in component.get('note',''): component['note'] = component.get('note','') + ' ' + contact_note
sound = next(p for p in data['pages'] if p['id']=='script/SoundComponent')
for n,desc,k in [('PlayInstance','개별 PlaybackHandle을 받는 재생 요청.','method'),('PlayOneShotInstance','개별 one-shot 재생 핸들.','method'),('Source','명시적인 AudioSource 설정.','property'),('Settings','AudioPlaySettings 설정.','property')]:
    if not any(m[0]==n for m in sound['members']): sound['members'].append([n,'',desc,k])
rg = next(p for p in data['pages'] if p['id']=='native/RGHandle')
for n in ['version','kind','epoch']:
    if not any(m[0]==n for m in rg['members']): rg['members'].append([n,'','버전·종류·graph epoch를 보존하는 핸들 필드.','field'])
rg['note'] = 'index만으로 다른 버전이나 Reset 뒤 graph의 자원을 식별하지 않습니다. version·kind·epoch를 포함한 핸들 계약을 따릅니다.'

for page in data['pages']:
    text = source(page['source'])
    scope = page['title'] if page.get('members') else None
    if scope and scope != 'PhysicsHit': scope = page['id'].split('/')[-1]
    type_match = re.search(r'(?m)^\s*((?:public\s+[^\n{]*\b(?:class|struct|enum)\s+' + re.escape(scope or 'NO_TYPE') + r'\b[^\n{]*)|(?:class|struct|enum class)\s+'+re.escape(scope or 'NO_TYPE')+r'\b[^\n{]*)',text)
    if type_match: page['signature'] = type_match.group(1).strip().rstrip(';')
    evidence = []
    for member in page.get('members',[]):
        actual_name = scope if member[0] in ['Constructor',scope] or member[0].startswith('EnhancedRenderGraph(') else member[0]
        found = declaration(text, actual_name, member[3], scope)
        if not found and member[3] in ('enum','field'):
            region, offset = type_region(text, scope)
            match = re.search(r'\b'+re.escape(member[0])+r'\b\s*(?:=\s*[^,}\n]+)?',region)
            if match: found=[{'signature':match.group(0).strip(),'line':text.count('\n',0,offset+match.start())+1,'sourceDeclaration':match.group(0).strip()}]
        if not found: raise RuntimeError(f'Unresolved API declaration: {page["id"]} / {member[0]}')
        if member[1] != found[0]['signature']: changes.append({'page':page['id'],'member':member[0],'before':member[1],'after':found[0]['signature']})
        member[1] = found[0]['signature']
        evidence.append({'member':member[0], 'matches':found})
    page['memberEvidence'] = evidence
    page['sourceBlob'] = subprocess.check_output(['git','rev-parse',sha+':'+page['source']],cwd=ROOT,text=True).strip()
    if page.get('sections'):
        for section in page['sections']:
            if any(token in section.get('text','') for token in ('OnTriggerEnter','OnCollisionEnter','RaycastAll')):
                section['text'] = '현재 접촉 처리는 OnBeginSimulation에서 등록한 ContactStream과 PostPhysics 소비를 사용합니다. 쿼리는 sceneAnchor·호출자 버퍼·PhysicsQueryResult의 현재 선언을 따릅니다.'

getting = next(p for p in data['pages'] if p['id']=='guide/getting-started')
for section in getting['sections']:
    if section['id']=='requirements': section['text']='Windows x64, Visual Studio 18 계열 C++ 워크로드·MSVC v145·Windows SDK와 x64 MSBuild, .NET 10 SDK·x64 Runtime, Git, vcpkg와 PowerShell 7을 준비합니다. 회귀 검사는 Python 3도 사용합니다.'
    if section['id'] in ('fmod','audio'):
        section.update(id='audio',title='오디오 의존성',text='현재 backend는 miniaudio/Null입니다. miniaudio 출처와 빌드 경계는 ThirdParty/miniaudio/PROVENANCE.md를 따릅니다. 구 FMOD SDK 복사는 필요하지 않습니다.')
        section.pop('code',None)
    if section['id']=='build':section['code']='dotnet build .\\BuildTool\\CreatorBuildTool.csproj -c Debug\nmsbuild .\\Editor\\CreatorEditor.vcxproj /m /t:Build /p:Configuration=Debug /p:Platform=x64 /v:minimal\n.\\Bin\\x64-Debug\\Editor\\CreatorEditor.exe'
packaging=next(p for p in data['pages'] if p['id']=='guide/packaging')
for section in packaging['sections']:
    if section['id']=='publish':section['text']='--build는 네이티브 호스트까지 빌드하여 배포본을 만들도록 요청합니다. v145 네이티브 빌드 환경과 manifest 의존성이 필요합니다. 현재 오디오 backend는 miniaudio/Null입니다.'
next(p for p in data['pages'] if p['id']=='script/SoundComponent')['summary']='AudioHost/PlaybackService를 통해 오디오 클립 재생과 개별 재생 handle·기본 재생 속성을 제어합니다.'
next(p for p in data['pages'] if p['id']=='native/RGHandle')['summary']='리소스 index·version·kind·graph epoch를 묶는 값 핸들입니다. native GPU 리소스 핸들과 구분합니다.'
next(p for p in data['pages'] if p['id']=='script/PhysicsHit')['summary']='씬 물리 쿼리의 충돌 결과 값입니다. ComponentId·ShapeId·LayerId와 위치 정보를 제공하며 Entity를 복원합니다.'

data['meta'].update(revision=sha,compatibilityCheckedAt=sha,reviewedDate='2026-10-20',coverage='curated; every documented member reconciled with fixed-master declarations')
data['meta']['sourceFiles'] = {f:subprocess.check_output(['git','rev-parse',sha+':'+f],cwd=ROOT,text=True).strip() for f in source_cache}
target.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(ROOT/'Build/full-docs-api-changes.json').write_text(json.dumps({'master':sha,'changes':changes,'removedMembers':sorted(retired),'pages':len(data['pages']),'members':sum(len(p.get('members',[])) for p in data['pages'])},ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'pages':len(data['pages']),'members':sum(len(p.get('members',[])) for p in data['pages']),'signatureChanges':len(changes)}))
