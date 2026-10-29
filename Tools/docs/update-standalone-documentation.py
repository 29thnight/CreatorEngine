"""Reconcile authored standalone reports without altering their dated receipts."""
import hashlib
import html
import json
import pathlib
import posixpath
import re
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[2]
file=ROOT/'docs/analysis/MasterDocumentationAudit20261020.json'
data=json.loads(file.read_text(encoding='utf-8'));sha=data['master']
committed=set(subprocess.check_output(['git','ls-tree','-r','--name-only',sha],cwd=ROOT,text=True).splitlines())
graph=['Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h','Engine/RenderEngine/RHI/RHICommandTranslator.cpp','Engine/RenderEngine/Render/Core/EnhancedLivePipelineDesc.cpp']
scene=['Engine/SceneRuntime/SceneManager.h','Engine/SceneRuntime/Scene.cpp','Engine/Scripting/Bindings/Components/Entity.fs']
rules={
'EngineStructureAnalysis':(scene+graph,['현재 제품 경로는 C++23·DX12/Vulkan·CoreCLR/.NET 10과 miniaudio/Null이다. 아래 7월의 DX11·C++20·C++ 스크립팅·FMOD 설명과 파일 수는 현재 구조를 나타내지 않는다.','SceneManager의 GCCE domain/root, Scene→Entity→Component trace, 세대 핸들과 별도 자산 owner가 현재 수명 경계다. 리플렉션 서술 정본은 reflgen이다.'], 'docs/TechnicalGuide.html'),
'RectTransformAnalysis':(['Engine/SceneRuntime/RectTransformComponent.h','Engine/SceneRuntime/RectTransformComponent.cpp','Engine/SceneRuntime/Canvas.cpp','Engine/SceneRuntime/UIManager.h'],['RectTransform은 MarkDirty로 Scene layout dirty를 게시하고 부모·화면 rect와 canvas 배율을 사용한다. m_worldRect와 m_layoutScale은 reflgen::ignore 파생값이다. 과거 F-1/F-2의 고정 해상도·파생값 직렬화 진단을 현행 결함으로 재사용하지 않는다.','현재 UIManager는 여전히 Singleton이다. scene-local 입력·dirty propagation·값 UIDrawSnapshot과 전역 UIManager 퇴역의 남은 범위는 PHASE 16 계획으로 통일한다.'],'docs/plans/UISystemRedesignPlan.html'),
'ResourceOwnershipDesign':(['Engine/SceneRuntime/EntityHandle.h','Engine/SceneRuntime/SceneManager.h','Engine/Scripting/Bindings/Values/Values.fs','Engine/Scripting/Bindings/Components/Entity.fs'],['ObjectHandle/EntityHandle과 Component·Entity 기반 managed 표면은 구현돼 있다. 별도 핸들 도입·CoreCLR 전환을 미착수 계획으로 다시 사용하지 않는다.','GCCE 객체 root/trace, native 자원 owner, CLR GC, Scope 취소와 GPU retirement는 서로 다른 수명이다. 과거 17항목 probe 통과는 당시 실행에만 유효하다.'],'docs/design/GCCEIntegration.html'),
'FrameLifecycle':(scene+graph,['시뮬레이션과 immutable frame 게시, render queue 소비·graph 기록/제출, 완료된 슬롯의 표시를 구분한다. frame ID와 CPU·GPU·Present 관측은 같은 시각이 아니다.','현재 소프트웨어 RHI는 frozen target의 병렬 번역과 sealed batch 조기 handoff를 지원한다. 아래 10월 5일 그림의 고정 작업자 수·용량·순서가 변경 뒤 모든 경로를 보증하지 않는다.'],'docs/plans/RenderGraphDependencySchedulingPlan.html'),
'LiveRenderWiring20261012':(graph+['Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp'],['현재 live pipeline은 C++ Pass·명시적 버전/슬롯·dependency order를 소비한다. 선언 metadata 재사용과 독립 target 병렬 번역·조기 제출이 구현 기반이다.','아래 노드 수·카메라/그림자 배선 그림과 CPU/GPU 수치는 명시된 스냅샷이다. 전체 비용·overlap·오류 수용은 최신 RG8 계획을 따른다. SRP/C# 저작 전환은 취소됐다.'],'docs/plans/RenderGraphDependencySchedulingPlan.html'),
'EngineWiringAudit20261020':(scene,['이 대장은 26영역·78작업의 객체별 엔진 감사 원장이다. 문서 전수 검증 완료가 이 대장의 객체별 실행·비용·수명 감사 완료를 뜻하지 않는다.','최신 master는 사용하지 않는 Scene 종류별 registry를 정리하고 proxy dispatch generation 확인과 실패 dirty 재시도를 유지한다. 파일 해시·검사 상태는 원래 감사 신원으로 보존한다.'],'docs/plans/MasterPlanAudit20261020.html'),
'MasterStructureReduction20261020':(scene+['Engine/RenderEngine/AssetDepot/TextureAssetRuntime.cpp'],['빈 초기화·미사용 registry/UI/model 배선 제거는 현재 master에 반영됐다. Scene proxy 발행 실패는 같은 generation에 dirty를 복원하며 교체 등록에 이전 작업을 복원하지 않는다.','cooked texture cache 신원에서 항상 기본값이던 과거 recipe를 제거했다. 원래 빌드·GPU 게이트 기록과 파일 해시는 당시 실행이며 이번 문서 검증의 런타임 통과가 아니다.'],'docs/analysis/EngineWiringAudit20261020.html'),
'RenderGraphDeclaration20261020':(graph,['선언 usage storage·import metadata 재사용과 좁은 visibility scratch 잠금은 구현됐다. frozen target 병렬 번역·early handoff도 현재 master의 기반이다.','아래 -4.90%와 120 FPS 미달 결과는 원래 Debug 입력의 측정이다. 더 새 구현의 성능이나 표시 FPS로 해석하지 않는다.'],'docs/analysis/GraphDeclarationReuse20261020.html'),
}
for row in data['standalone']:
    name=pathlib.Path(row['path']).stem
    sources,facts,primary=rules.get(name,(graph,['현재 live graph는 명시적 버전·dependency order와 frozen target 병렬 번역·조기 sealed-batch handoff를 사용한다. 과거 직렬 전용 단계나 C# SRP 저작 전환을 필수 후속으로 재도입하지 않는다.','이 문서의 비교/이식·측정 표와 실행 결과는 명시된 날짜·source identity의 기록이다. 최신 비용·오류·제품 수용은 현재 RG8 계획을 따른다.'],'docs/plans/RenderGraphDependencySchedulingPlan.html'))
    records=[]
    for source in sources:
        raw=subprocess.check_output(['git','show',sha+':'+source],cwd=ROOT)
        text=raw.decode('utf-8-sig')
        candidates=[(i,line.strip()) for i,line in enumerate(text.splitlines(),1) if re.search(r'\bclass\b|\bstruct\b|::(?:Translate|CommitRenderProxies|MarkDirty|MakeTextureImageKey)\(|m_worldRect;|m_layoutScale =|tracer\.',line) and not line.lstrip().startswith('//')]
        selected=candidates[:3] or [(1,text.splitlines()[0].strip())]
        records.append({'path':source,'blob':hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest(),'sha256':hashlib.sha256(raw).hexdigest(),'lines':[{'line':i,'text':s} for i,s in selected]})
    row['sources']=records;row['currentReference']=primary
    rel=lambda target:posixpath.relpath(target,posixpath.dirname(row['path']))
    content='<aside data-full-master-review style="padding:1rem;margin:1rem;border:1px solid #888;overflow-wrap:anywhere"><h2>2026-10-20 master 현행 대조</h2><p><code>'+sha+'</code></p><ul>'+''.join('<li>'+html.escape(s)+'</li>' for s in facts)+'</ul><p><a href="'+rel(primary)+'">현재 정본·후속 수용</a> · <a href="'+rel('docs/analysis/MasterDocumentationAudit20261020.html')+'">전체 문서 검증</a></p><details><summary>현행 소스 근거</summary><ul>'
    for e in records:
        for line in e['lines']:content+='<li><a href="https://github.com/29thnight/CreatorEngine/blob/'+sha+'/'+e['path']+'#L'+str(line['line'])+'">'+html.escape(e['path'])+':'+str(line['line'])+'</a> <code>'+html.escape(line['text'])+'</code></li>'
    content+='</ul></details><p>이 아래 날짜별 구조·수치·실행 통과는 당시 기록이며 현재 구현과 충돌하면 위 현행 대조를 우선한다. 보존되지 않은 원본은 현재 실행 증거로 사용하지 않는다.</p></aside>'
    target=ROOT/row['source'];text=target.read_text(encoding='utf-8-sig')
    text=re.sub(r'<aside data-full-master-review\b[^>]*>.*?</aside>',lambda m:content,text,count=1,flags=re.S)
    def portable(match):
        target=html.unescape(match[2])
        if re.match(r'[a-z]+:|/|#',target,re.I):return match[0]
        relative=posixpath.normpath(posixpath.join(posixpath.dirname(row['path']),target.split('#')[0].split('?')[0]))
        if relative.startswith('docs/'):
            canonical=posixpath.relpath(relative,posixpath.dirname(row['path']))
            suffix=target[len(target.split('#')[0].split('?')[0]):]
            return match[1]+canonical+html.escape(suffix,quote=True)+match[3]
        directory=any(p.startswith(relative.rstrip('/')+'/') for p in committed)
        if relative not in committed and not directory:return match[0]
        fragment='#'+target.split('#',1)[1] if '#' in target else ''
        return match[1]+'https://github.com/29thnight/CreatorEngine/'+('tree/' if directory else 'blob/')+sha+'/'+relative+fragment+match[3]
    text=re.sub(r'(<a\b[^>]*?\bhref=["\'])([^"\']+)(["\'])',portable,text,flags=re.I)
    if name=='EngineWiringAudit20261020':
        text=re.sub(r'^const portableSourcePaths = new Set\(.*?\);\n','',text,flags=re.M)
        names=json.dumps(sorted(p for p in committed if not p.startswith('docs/')),ensure_ascii=False)
        function="const portableSourcePaths = new Set("+names+");\nfunction href(target){\n if(/^https?:\\/\\//i.test(target))return target;\n if(/^[A-Za-z][\\w+.-]*:/.test(target)||target.startsWith('/')||target.includes('\\\\'))return null;\n const [file,fragment='']=target.split('#');\n if(file.startsWith('docs/'))return '../'+file.slice(5)+(fragment?'#'+fragment:'');\n if(!portableSourcePaths.has(file))return null;\n return 'https://github.com/29thnight/CreatorEngine/blob/"+sha+"/'+file+(fragment?'#'+fragment:'');\n}"
        text=re.sub(r'^function href\(target\)\{.*?^\}',lambda m:function,text,count=1,flags=re.M|re.S)
    target.write_text(text,encoding='utf-8')
file.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'master':sha,'standaloneReports':len(data['standalone'])}))
