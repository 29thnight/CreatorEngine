let MANIFEST=[];
const $=id=>document.getElementById(id), defs=[['프레임 시작','RenderBeginFrame','#6b737c'],['장면 준비','MaterialGraphScenePrepare','#24765e'],['그래프 선언','RenderGraphDeclare','#966000'],['컴파일','RenderGraphCompile','#355ba2'],['명령 기록','RenderCommandRecord','#177487'],['CPU 제출','RenderSubmit','#a13d31'],['완료 단계','RenderComplete','#6b737c'],['DXGI Present','DXGIPresent','#746047'],['Present wait','DX12PresentWait','#777777']];
defs.push(...[["준비 전체", "MaterialGraphScenePrepare", "#24765e"], ["프레임 초기화", "MaterialSceneFrameSetup", "#24765e"], ["효과·CPU 준비 합류", "MaterialSceneEffectsPrepare", "#24765e"], ["기하 테이블·뷰 상수", "MaterialSceneGeometryTableSetup", "#24765e"], ["Draw 전체", "MaterialSceneDrawsPrepare", "#24765e"], ["Draw 준비", "MaterialSceneDrawPrepare", "#24765e"], ["카메라 준비", "MaterialSceneCameraPrepare", "#24765e"], ["표면 단계 준비", "MaterialSceneSurfaceStagesPrepare", "#24765e"], ["그림자 전체·설정", "MaterialSceneShadowSetup", "#24765e"], ["그림자 준비", "MaterialSceneShadowPrepare", "#24765e"], ["간접 명령 생성", "MaterialSceneIndirectCommandsPrepare", "#24765e"], ["최종 준비", "MaterialSceneFinalizePrepare", "#24765e"], ["게시·자원 해제", "MaterialScenePublishAndRelease", "#24765e"], ["프로그램·바인딩", "MaterialSceneDrawProgramBindings", "#177487"], ["Draw 상수", "MaterialSceneDrawConstants", "#177487"], ["청크 준비", "MaterialSceneDrawChunks", "#177487"], ["메시 GPU 준비", "MaterialMeshGpuPrepare", "#177487"], ["청크 레코드 작성", "MaterialSceneChunkRecordUpload", "#177487"], ["재질 바인딩", "MaterialBindingsPrepare", "#177487"], ["바인딩 CPU 설명", "MaterialBindingsCpuDescription", "#177487"], ["정적 캐시 키", "MaterialSceneShadowCacheKey", "#966000"], ["캐시 검색·할당", "MaterialSceneShadowCacheLookup", "#966000"], ["캐스케이드 전체", "MaterialSceneShadowCascades", "#966000"], ["캐스케이드 준비", "MaterialSceneShadowCascadePrepare", "#966000"], ["가시성 준비", "GeometryVisibilityPrepare", "#966000"], ["깊이 이력 준비", "GeometryHistoryPrepare", "#966000"], ["기하 상주 테이블 게시", "MaterialSceneGeometryPublish", "#24765e"], ["작업 패킷 준비", "MaterialScenePacketPrepare", "#177487"], ["재질 타일 패킷 준비", "MaterialSceneTilePacketPrepare", "#177487"], ["간접 준비 전체", "MaterialSceneIndirectPrepare", "#177487"], ["풀 획득", "SceneBufferPoolAcquire", "#177487"], ["풀 검색", "SceneBufferPoolLookup", "#177487"], ["획득 락", "SceneBufferPoolAcquireLock", "#177487"], ["GPU 버퍼 생성", "SceneBufferPoolCreateResource", "#177487"], ["풀 반납", "SceneBufferPoolReturn", "#177487"], ["반납 락", "SceneBufferPoolReturnLock", "#177487"], ["GPU 버퍼 해제", "SceneBufferPoolReleaseResource", "#177487"], ["패킷 계획 업로드", "MaterialScenePacketPlanUpload", "#177487"], ["패킷 출력 바인딩", "MaterialScenePacketOutputBindings", "#177487"], ["노드 본문", "RenderGraphDeclareNodeBody", "#966000"], ["노드 후처리", "RenderGraphDeclareAfterNode", "#966000"], ["ImportBuffer", "RenderGraphDeclareImportBuffer", "#966000"], ["ImportTexture", "RenderGraphDeclareImportTexture", "#966000"], ["CreateBuffer", "RenderGraphDeclareCreateBuffer", "#966000"], ["CreateTexture", "RenderGraphDeclareCreateTexture", "#966000"], ["InitializePass", "RenderGraphDeclareInitializePass", "#966000"], ["AdvanceVersion", "RenderGraphDeclareAdvanceVersion", "#966000"], ["AddPass", "RenderGraphDeclareAddPass", "#966000"], ["AddSplitPass", "RenderGraphDeclareAddSplitPass", "#966000"], ["AddRepeatedPass", "RenderGraphDeclareAddRepeatedPass", "#966000"], ["카메라 분기 실행", "MaterialSceneCameraVisibilityTask", "#24765e"], ["정렬 레이어 분기", "MaterialSceneBlendedVisibilityTask", "#24765e"], ["그림자 캐스케이드 실행", "MaterialSceneShadowVisibilityTask", "#966000"], ["독립 패킷 실행", "MaterialScenePacketTask", "#177487"], ["카메라 배포", "MaterialSceneCameraDispatch", "#24765e"], ["카메라 합류", "MaterialSceneCameraJoin", "#6b737c"], ["그림자 합류", "MaterialSceneShadowJoin", "#6b737c"], ["패킷 합류", "MaterialScenePacketJoin", "#6b737c"], ["기하·캐스터 입력 확정", "MaterialSceneCasterSnapshot", "#24765e"], ["그림자 설정 분기", "MaterialSceneShadowSetupTask", "#966000"], ["그림자 설정 합류", "MaterialSceneShadowSetupJoin", "#6b737c"], ["합류 중 작업 지원", "MaterialSceneJoinAssist", "#6b737c"], ["메시 입력 검증", "MeshPrepareValidate", "#966000"], ["메시 소유권 구성", "MeshPrepareOwner", "#966000"], ["메시 정적 식별자", "MeshPrepareIdentity", "#966000"], ["클러스터 준비", "MeshPrepareClusters", "#966000"], ["요청 순서 조회", "MeshClusterRequests", "#966000"], ["스트림 상주 준비", "MeshClusterResidencyPrepare", "#966000"], ["정적 GPU 캐시", "MeshPrepareStaticCache", "#966000"], ["변환 캐시 검색", "MeshPrepareTransformLookup", "#966000"], ["메시 출력 확보", "MeshPrepareOutput", "#966000"], ["바인딩 검증", "BindingPrepareValidate", "#177487"], ["프레임 바인딩 캐시", "BindingPrepareFrameCache", "#177487"], ["바인딩 소유권", "BindingPrepareOwner", "#177487"], ["텍스처 확인", "BindingPrepareTextures", "#177487"], ["description 조회", "BindingPrepareDescriptionLookup", "#177487"], ["핸들 갱신", "BindingPreparePatch", "#177487"], ["샘플러 준비", "BindingPrepareSamplers", "#177487"], ["bindless 등록", "BindingPrepareBindless", "#177487"], ["바인딩 업로드", "BindingPrepareUploads", "#177487"], ["바인딩 게시", "BindingPreparePublish", "#177487"], ["프레임 캐시 비우기", "BindingFrameClear", "#177487"], ["프레임 객체 생성", "BindingFrameCreate", "#177487"], ["준비 패킷 조회", "BindingFrameLookup", "#177487"], ["GPU 텍스처 확인", "BindingTextureResidentLookup", "#966000"], ["CPU 이미지 조회", "BindingTextureImageResolve", "#966000"], ["준비 이미지 업로드", "BindingTexturePreparedUpload", "#966000"], ["DX12 캐시 접근", "DX12TextureCacheAccess", "#966000"], ["DX12 상주 확인·사용 갱신", "DX12TextureResidentValidateTouch", "#966000"], ["Draw 바인딩 분기", "MaterialSceneDrawBindingsTask", "#177487"], ["Draw 바인딩·상수 준비", "MaterialSceneDrawBindingsPrepare", "#177487"], ["Draw 바인딩 합류", "MaterialSceneDrawBindingsJoin", "#6b737c"], ["패킷 계획 재사용", "MaterialPacketPlanReuse", "#177d67"], ["패킷 계획 생성", "MaterialPacketPlanBuild", "#a06f20"], ["선언 Join", "RenderGraphDeclareJoin", "#966000"], ["선언 MeshFill", "RenderGraphDeclareMeshFill", "#966000"], ["선언 MeshPlan", "RenderGraphDeclareMeshPlan", "#966000"], ["RHI 소프트웨어 번역", "RHISoftwareTranslate", "#177487"], ["RHI 작업 실행", "RHIExecuteWork", "#746047"], ["DX12 제출 접수", "DX12EnqueueCommands", "#966000"], ["SSAO 선언 분기", "RenderDeclare.SSAO", "#177487"], ["LightTiles 선언 분기", "RenderDeclare.METEOR.Scene.LightTiles", "#177487"], ["노드 입력 복사", "RenderGraphDeclareNodeSnapshot", "#966000"], ["노드 개별 합류", "RenderGraphDeclareNodeJoin", "#6b737c"], ["노드 결과 게시", "RenderGraphDeclareNodePublish", "#966000"], ["Compile 합류", "RenderGraphSetupCompileJoin", "#6b737c"], ["Execute 합류", "RenderGraphSetupExecuteJoin", "#6b737c"], ["최종 상태 검증", "RenderGraphCompileFinalStates", "#355ba2"], ["반복 패스 검증", "RenderGraphCompileRepeated", "#355ba2"], ["즉시 명령 봉인", "RHIImmediateSeal", "#24765e"], ["RHI native 즉시 실행", "RHIImmediateExecute", "#b0783b"], ["접근 계획 조회", "MaterialPassReadPlanAcquire", "#24765e"], ["접근 계획 적중", "MaterialPassReadPlanHit", "#24765e"], ["접근 계획 생성", "MaterialPassReadPlanBuild", "#24765e"], ["접근 계획 핸들 연결", "MaterialPassReadPlanResolve", "#24765e"]]);
defs.push(...[["기하·캐스터 입력 확정", "MaterialSceneCasterSnapshot", "#24765e"], ["그림자 설정 분기", "MaterialSceneShadowSetupTask", "#966000"], ["그림자 설정 합류", "MaterialSceneShadowSetupJoin", "#6b737c"]]);
defs.push(["합류 중 작업 지원","MaterialSceneJoinAssist","#6b737c"]);
const phaseGroups={"scene": ["MaterialGraphScenePrepare", "MaterialSceneFrameSetup", "MaterialSceneEffectsPrepare", "MaterialSceneGeometryTableSetup", "MaterialSceneDrawsPrepare", "MaterialSceneDrawPrepare", "MaterialSceneCameraPrepare", "MaterialSceneSurfaceStagesPrepare", "MaterialSceneShadowSetup", "MaterialSceneShadowPrepare", "MaterialSceneIndirectCommandsPrepare", "MaterialSceneFinalizePrepare", "MaterialScenePublishAndRelease", "MaterialSceneGeometryPublish", "MaterialScenePacketPrepare", "MaterialSceneTilePacketPrepare", "MaterialSceneIndirectPrepare"], "draw": ["MaterialSceneDrawPrepare", "MaterialSceneDrawProgramBindings", "MaterialSceneDrawConstants", "MaterialSceneDrawChunks", "MaterialMeshGpuPrepare", "MaterialSceneChunkRecordUpload", "MaterialBindingsPrepare", "MaterialBindingsCpuDescription"], "shadow": ["MaterialSceneShadowPrepare", "MaterialSceneShadowCacheKey", "MaterialSceneShadowCacheLookup", "MaterialSceneShadowCascades", "MaterialSceneShadowCascadePrepare", "GeometryVisibilityPrepare", "GeometryHistoryPrepare"]};
phaseGroups.pool=["SceneBufferPoolAcquire", "SceneBufferPoolLookup", "SceneBufferPoolAcquireLock", "SceneBufferPoolCreateResource", "SceneBufferPoolReturn", "SceneBufferPoolReturnLock", "SceneBufferPoolReleaseResource", "MaterialScenePacketPlanUpload", "MaterialScenePacketOutputBindings"];
phaseGroups.declare=["RenderGraphDeclare", "RenderGraphDeclareNodeBody", "RenderGraphDeclareAfterNode", "RenderGraphDeclareImportBuffer", "RenderGraphDeclareImportTexture", "RenderGraphDeclareCreateBuffer", "RenderGraphDeclareCreateTexture", "RenderGraphDeclareInitializePass", "RenderGraphDeclareAdvanceVersion", "RenderGraphDeclareAddPass", "RenderGraphDeclareAddSplitPass", "RenderGraphDeclareAddRepeatedPass"];
phaseGroups.parallel=["MaterialSceneCameraVisibilityTask", "MaterialSceneBlendedVisibilityTask", "MaterialSceneShadowVisibilityTask", "MaterialScenePacketTask", "MaterialSceneCameraDispatch", "MaterialSceneCameraJoin", "MaterialSceneShadowJoin", "MaterialScenePacketJoin", "MaterialGraphScenePrepare", "MaterialSceneShadowSetup", "MaterialSceneSurfaceStagesPrepare", "MaterialSceneTilePacketPrepare"];
phaseGroups.assist=['MaterialSceneJoinAssist','MaterialSceneCameraJoin','MaterialSceneShadowJoin','MaterialScenePacketJoin','MaterialSceneShadowSetupJoin','MaterialSceneCameraVisibilityTask','MaterialSceneShadowVisibilityTask','MaterialScenePacketTask','MaterialSceneShadowSetupTask','MaterialGraphScenePrepare'];
phaseGroups.parallel.push('MaterialSceneJoinAssist');
phaseGroups.parallel.push('MaterialSceneCasterSnapshot','MaterialSceneDrawsPrepare','MaterialSceneShadowSetupTask','MaterialSceneShadowSetupJoin');
phaseGroups.scene.push('MaterialSceneCasterSnapshot','MaterialSceneShadowSetupTask','MaterialSceneShadowSetupJoin');
phaseGroups.shadow.push('MaterialSceneShadowSetupTask','MaterialSceneShadowSetupJoin');
defs.push(...[["SSAO 선언 분기", "RenderDeclare.SSAO", "#177487"], ["LightTiles 선언 분기", "RenderDeclare.METEOR.Scene.LightTiles", "#177487"], ["노드 입력 복사", "RenderGraphDeclareNodeSnapshot", "#966000"], ["노드 개별 합류", "RenderGraphDeclareNodeJoin", "#6b737c"], ["노드 결과 게시", "RenderGraphDeclareNodePublish", "#966000"], ["Compile 합류", "RenderGraphSetupCompileJoin", "#6b737c"], ["Execute 합류", "RenderGraphSetupExecuteJoin", "#6b737c"], ["최종 상태 검증", "RenderGraphCompileFinalStates", "#355ba2"], ["반복 패스 검증", "RenderGraphCompileRepeated", "#355ba2"]]);
phaseGroups.declare.push(...["RenderDeclare.SSAO", "RenderDeclare.METEOR.Scene.LightTiles", "RenderGraphDeclareNodeSnapshot", "RenderGraphDeclareNodeJoin", "RenderGraphDeclareNodePublish", "RenderGraphSetupCompileJoin", "RenderGraphSetupExecuteJoin", "RenderGraphCompileFinalStates", "RenderGraphCompileRepeated"]);
const stateNames=['Unknown','Running','Ready','Waiting','Ended'],stateColors=['#90969d','#34734f','#b47a0a','#466b9b','#d1d4d8'];
defs.push(...[["메시 입력 검증", "MeshPrepareValidate", "#966000"], ["메시 소유권 구성", "MeshPrepareOwner", "#966000"], ["메시 정적 식별자", "MeshPrepareIdentity", "#966000"], ["클러스터 준비", "MeshPrepareClusters", "#966000"], ["요청 순서 조회", "MeshClusterRequests", "#966000"], ["스트림 상주 준비", "MeshClusterResidencyPrepare", "#966000"], ["정적 GPU 캐시", "MeshPrepareStaticCache", "#966000"], ["변환 캐시 검색", "MeshPrepareTransformLookup", "#966000"], ["메시 출력 확보", "MeshPrepareOutput", "#966000"], ["바인딩 검증", "BindingPrepareValidate", "#177487"], ["프레임 바인딩 캐시", "BindingPrepareFrameCache", "#177487"], ["바인딩 소유권", "BindingPrepareOwner", "#177487"], ["텍스처 확인", "BindingPrepareTextures", "#177487"], ["description 조회", "BindingPrepareDescriptionLookup", "#177487"], ["핸들 갱신", "BindingPreparePatch", "#177487"], ["샘플러 준비", "BindingPrepareSamplers", "#177487"], ["bindless 등록", "BindingPrepareBindless", "#177487"], ["바인딩 업로드", "BindingPrepareUploads", "#177487"], ["바인딩 게시", "BindingPreparePublish", "#177487"]]);
phaseGroups.mesh=["MaterialGraphScenePrepare", "MaterialSceneCasterSnapshot", "MaterialMeshGpuPrepare", "MeshPrepareValidate", "MeshPrepareOwner", "MeshPrepareIdentity", "MeshPrepareClusters", "MeshClusterRequests", "MeshClusterResidencyPrepare", "MeshPrepareStaticCache", "MeshPrepareTransformLookup", "MeshPrepareOutput"];
phaseGroups.binding=["MaterialGraphScenePrepare", "MaterialBindingsPrepare", "BindingPrepareValidate", "BindingPrepareFrameCache", "BindingPrepareOwner", "BindingPrepareTextures", "BindingPrepareDescriptionLookup", "BindingPreparePatch", "BindingPrepareSamplers", "BindingPrepareBindless", "BindingPrepareUploads", "BindingPreparePublish"];
defs.push(...[["프레임 캐시 비우기", "BindingFrameClear", "#177487"], ["프레임 객체 생성", "BindingFrameCreate", "#177487"], ["준비 패킷 조회", "BindingFrameLookup", "#177487"], ["GPU 텍스처 확인", "BindingTextureResidentLookup", "#966000"], ["CPU 이미지 조회", "BindingTextureImageResolve", "#966000"], ["준비 이미지 업로드", "BindingTexturePreparedUpload", "#966000"], ["DX12 캐시 접근", "DX12TextureCacheAccess", "#966000"], ["DX12 상주 확인·사용 갱신", "DX12TextureResidentValidateTouch", "#966000"]]);
phaseGroups.bindingDetail=["MaterialGraphScenePrepare", "MaterialBindingsPrepare", "BindingPrepareFrameCache", "BindingPrepareTextures", "BindingFrameClear", "BindingFrameCreate", "BindingFrameLookup", "BindingTextureResidentLookup", "BindingTextureImageResolve", "BindingTexturePreparedUpload", "DX12TextureCacheAccess", "DX12TextureResidentValidateTouch"];
phaseGroups.binding.push(...["BindingFrameClear", "BindingFrameCreate", "BindingFrameLookup", "BindingTextureResidentLookup", "BindingTextureImageResolve", "BindingTexturePreparedUpload", "DX12TextureCacheAccess", "DX12TextureResidentValidateTouch"]);
let D=null,A=0,B=50,frames=[],phaseHits=[],threadHits=[],visibleLanes=[],version=0;
const overlap=(a,b,c,d)=>Math.max(0,Math.min(b,d)-Math.max(a,c)),fmt=x=>Number(x).toFixed(3),lower=(arr,x)=>{let a=0,b=arr.length;while(a<b){let m=(a+b)>>1;if(arr[m][0]<x)a=m+1;else b=m}return a};
const escapeHtml=x=>String(x).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');
const labelLane=l=>[...l.names].sort((a,b)=>b.includes('RenderThread')-a.includes('RenderThread'))[0]||'이름 없음';
function canvas(id,h){let c=$(id),w=Math.max(id==='overview'?300:800,c.parentElement.clientWidth-2),ratio=devicePixelRatio||1;c.width=Math.round(w*ratio);c.height=Math.round(h*ratio);c.style.width=w+'px';c.style.height=h+'px';let ctx=c.getContext('2d');ctx.scale(ratio,ratio);ctx.font='12px "Geist",sans-serif';return {c,ctx,w,h}}
function ink(){return getComputedStyle(document.documentElement).getPropertyValue('--text').trim()||'#172027'}
function ranged(arr,a,b,maxDuration=0){let out=[];for(let i=Math.max(0,lower(arr,a-maxDuration)-1);i<arr.length&&arr[i][0]<=b;i++){let x=arr[i];if(x[0]+x[1]>=a)out.push(x)}return out}
function setRange(a,b){if(!D)return;let span=Math.min(D.end-D.begin,Math.max(.01,b-a));A=Math.max(D.begin,Math.min(a,D.end-span));B=A+span;$('start').value=A.toFixed(3);$('span').value=span.toFixed(3);draw()}
function setupFrames(){frames=$('unit').value==='loop'?D.loops:D.measurements.filter(x=>x[3]===Number($('unit').value));$('frame').innerHTML=frames.map((x,i)=>`<option value="${i}">${$('unit').value==='loop'?'CPU tick '+x[2]:'realFrame '+x[2]} · ${fmt(x[1])}ms · t=${fmt(x[0])}</option>`).join('')}
function chooseFrame(mode){if(!frames.length){setRange(D.begin,Math.min(D.end,D.begin+50));return}let sorted=frames.map((x,i)=>({x,i})).sort((a,b)=>a.x[1]-b.x[1]);let item=mode==='worst'?sorted.at(-1):sorted[Math.floor(sorted.length/2)];$('frame').value=item.i;setRange(item.x[0],item.x[0]+item.x[1])}
defs.push(...[["Draw 바인딩 분기", "MaterialSceneDrawBindingsTask", "#177487"], ["Draw 바인딩·상수 준비", "MaterialSceneDrawBindingsPrepare", "#177487"], ["Draw 바인딩 합류", "MaterialSceneDrawBindingsJoin", "#6b737c"]]);
phaseGroups.preparation=['MaterialGraphScenePrepare','MaterialSceneCasterSnapshot','MaterialMeshGpuPrepare','MaterialSceneDrawBindingsTask','MaterialSceneDrawBindingsPrepare','MaterialSceneDrawBindingsJoin','MaterialSceneDrawsPrepare','MaterialSceneDrawPrepare','MaterialSceneDrawProgramBindings','MaterialSceneDrawConstants','MaterialBindingsPrepare','BindingPrepareTextures','MaterialSceneDrawChunks','MaterialSceneChunkRecordUpload','MaterialSceneShadowSetup','MaterialSceneCameraJoin','MaterialSceneIndirectCommandsPrepare','MaterialScenePacketPrepare','MaterialSceneTilePacketPrepare'];
defs.push(...[["패킷 계획 재사용", "MaterialPacketPlanReuse", "#177d67"], ["패킷 계획 생성", "MaterialPacketPlanBuild", "#a06f20"]]);
phaseGroups.preparation.push("MaterialPacketPlanReuse","MaterialPacketPlanBuild");
defs.push(...[["METEOR.Scene.Geometry", "RenderDeclare.METEOR.Scene.Geometry", "#177487"], ["METEOR.Scene.Cull.First", "RenderDeclare.METEOR.Scene.Cull.First", "#177487"], ["METEOR.Scene.Occluder", "RenderDeclare.METEOR.Scene.Occluder", "#177487"], ["METEOR.Scene.Cull.Final", "RenderDeclare.METEOR.Scene.Cull.Final", "#177487"], ["METEOR.Scene.LayerWork", "RenderDeclare.METEOR.Scene.LayerWork", "#177487"], ["METEOR.Shadow.StaticCache", "RenderDeclare.METEOR.Shadow.StaticCache", "#966000"], ["METEOR.Shadow.Cull.First", "RenderDeclare.METEOR.Shadow.Cull.First", "#966000"], ["METEOR.Shadow.Occluder", "RenderDeclare.METEOR.Shadow.Occluder", "#966000"], ["METEOR.Shadow.Cull.Final", "RenderDeclare.METEOR.Shadow.Cull.Final", "#966000"], ["METEOR.Shadow.Raster", "RenderDeclare.METEOR.Shadow.Raster", "#966000"], ["METEOR.Shadow.History", "RenderDeclare.METEOR.Shadow.History", "#966000"], ["METEOR.Scene.Visibility", "RenderDeclare.METEOR.Scene.Visibility", "#177487"], ["METEOR.Scene.MaterialWork", "RenderDeclare.METEOR.Scene.MaterialWork", "#177487"], ["METEOR.Scene.SurfaceResolve", "RenderDeclare.METEOR.Scene.SurfaceResolve", "#177487"], ["METEOR.Scene.Decal", "RenderDeclare.METEOR.Scene.Decal", "#177487"], ["METEOR.Scene.History", "RenderDeclare.METEOR.Scene.History", "#177487"], ["METEOR.Scene.Color", "RenderDeclare.METEOR.Scene.Color", "#177487"], ["METEOR.Scene.Volume", "RenderDeclare.METEOR.Scene.Volume", "#177487"], ["METEOR.Scene.Blended", "RenderDeclare.METEOR.Scene.Blended", "#177487"]]);
phaseGroups.declare.push(...["RenderDeclare.METEOR.Scene.Geometry", "RenderDeclare.METEOR.Scene.Cull.First", "RenderDeclare.METEOR.Scene.Occluder", "RenderDeclare.METEOR.Scene.Cull.Final", "RenderDeclare.METEOR.Scene.LayerWork", "RenderDeclare.METEOR.Shadow.StaticCache", "RenderDeclare.METEOR.Shadow.Cull.First", "RenderDeclare.METEOR.Shadow.Occluder", "RenderDeclare.METEOR.Shadow.Cull.Final", "RenderDeclare.METEOR.Shadow.Raster", "RenderDeclare.METEOR.Shadow.History", "RenderDeclare.METEOR.Scene.Visibility", "RenderDeclare.METEOR.Scene.MaterialWork", "RenderDeclare.METEOR.Scene.SurfaceResolve", "RenderDeclare.METEOR.Scene.Decal", "RenderDeclare.METEOR.Scene.History", "RenderDeclare.METEOR.Scene.Color", "RenderDeclare.METEOR.Scene.Volume", "RenderDeclare.METEOR.Scene.Blended"]);
async function loadDataset(id) {
    const requestedVersion = ++version;
    $('status').textContent = '측정 데이터 JSON을 읽는 중…';
    window.CETimelineReady = false;
    const entry = MANIFEST.find(item => item.id === id);
    if (!entry) throw Error('측정 항목 없음: ' + id);
    const value = await readDatasetJson(entry.cpu || id + '.json');
    if (entry.windows) value.windows = entry.windows;
    if (entry.traceUtc) value.traceUtc = entry.traceUtc;
    hydrateDataset(value);
    if (requestedVersion !== version) return;
    D = value;
    await attachGpuData(D);
    if (requestedVersion !== version) return;
    D.laneList = Object.values(D.lanes);
    for (const lane of D.laneList) {
        lane.minDepth = Math.min(...lane.scopes.slice(0, 5000).map(scope => scope[3]), 0);
        lane.maxDepth = lane.scopes.reduce((maximum, scope) => Math.max(maximum, scope[3]), 0);
    }
    setupFrames();
    chooseFrame('median');
    const gpuStatus = D.gpu.summary.available === false ? 'GPU timestamp 미수집' : `GPU ${D.gpu.events.length.toLocaleString()} spans`;
    $('status').textContent = `${id} · CPU 이벤트 ${D.nativeEvents.toLocaleString()}개 · 뷰 구간 ${D.measurements.length}개 · ${D.laneList.length} TID · ${D.etwAvailable ? 'ETW 상태 포함' : 'OS 상태 미수집'} · QPC 정렬 · ${gpuStatus}`;
    const sourceLabel = D.sourceLabel || '원본 CEPROF';
    const sourceHash = D.nativeSha256 ? 'CEPROF SHA256 ' + D.nativeSha256 : 'Frame JSON SHA256 ' + D.frameBoundarySourceSha256;
    $('sources').innerHTML = `<a href="${escapeHtml(sourceUrl(D.source))}">${escapeHtml(sourceLabel)}</a>${D.etwSource ? ' · <a href="' + escapeHtml(sourceUrl(D.etwSource)) + '">원본 ETL</a>' : ''}<br>${sourceHash}<br>QPC ${D.hz}Hz · 부분 스코프 ${D.partialScopeCount}개 · 중복 제거 ${D.duplicateScopesRemoved}개`;
    window.CETimelineReady = true;
    window.CETimelineDataId = id;
}
function drawOverview(){let {ctx,w,h}=canvas('overview',80);ctx.clearRect(0,0,w,h);let x=t=>(t-D.begin)/(D.end-D.begin)*w;ctx.fillStyle=ink();ctx.fillText(`전체 ${fmt(D.end-D.begin)}ms`,4,13);for(let z of D.windows){ctx.fillStyle=z.name==='off'?'#8b9299':'#34734f';ctx.fillRect(x(z.begin),18,Math.max(1,x(z.end)-x(z.begin)),13);ctx.fillStyle=ink();ctx.fillText(z.name+' '+fmt(z.end-z.begin)+'ms',Math.max(4,x(z.begin)),44)}for(let m of D.measurements){ctx.fillStyle=m[3]===2?'#177487':'#966000';ctx.fillRect(x(m[0]),m[3]===2?62:51,Math.max(.6,x(m[0]+m[1])-x(m[0])),8)}ctx.strokeStyle='#a13d31';ctx.lineWidth=2;ctx.strokeRect(x(A),16,Math.max(2,x(B)-x(A)),58)}
function grid(ctx,w,h,left=170){ctx.strokeStyle='#9aa1a64f';ctx.fillStyle=ink();for(let i=0;i<=5;i++){let x=left+(w-left)*i/5;ctx.beginPath();ctx.moveTo(x,20);ctx.lineTo(x,h);ctx.stroke();ctx.fillText(fmt(A+(B-A)*i/5),Math.min(w-64,x+2),13)}ctx.fillText('t · ms',8,13)}
function phaseAllowed(p){let view=Number($('view').value);return !view||p[3]===view||[5,6,7,8].includes(p[2])}
function detailAllowed(p,mode){if(!['draw','shadow'].includes(mode))return true;if(!D.detailParents){D.detailParents={};for(let k of ['draw','shadow']){let name=k==='draw'?'MaterialSceneDrawPrepare':'MaterialSceneShadowPrepare',rows=D.phases.filter(z=>D.markers[z[5]]===name);D.detailParents[k]={rows,max:rows.reduce((a,z)=>Math.max(a,z[1]),0)}}}let group=D.detailParents[mode];return ranged(group.rows,p[0]-.0002,p[0]+p[1]+.0002,group.max).some(z=>z[6]===p[6]&&z[0]<=p[0]+.0002&&p[0]+p[1]<=z[0]+z[1]+.0002)}
function drawPhases(){let mode=$('detail').value,indices=defs.map((_,i)=>i).filter(i=>defs.findIndex(t=>t[1]===defs[i][1])===i).filter(i=>mode==='all'||(mode==='pipeline'?i<9:phaseGroups[mode].includes(defs[i][1]))),rowMap=new Map(indices.map((i,j)=>[i,j]));let {ctx,w,h}=canvas('phases',30+indices.length*25);ctx.clearRect(0,0,w,h);grid(ctx,w,h);phaseHits=[];let stats=defs.map(()=>({sum:0,count:0,os:[0,0,0,0,0],dpc:0})),x=t=>170+(t-A)/(B-A)*(w-170);for(let [row,i] of indices.entries()){ctx.fillStyle=ink();ctx.fillText(defs[i][0],8,40+row*25)}for(let p of ranged(D.phases,A,B,D.maxPhaseDuration)){if(!phaseAllowed(p)||!rowMap.has(p[2])||!detailAllowed(p,mode))continue;let a=Math.max(A,p[0]),b=Math.min(B,p[0]+p[1]),left=x(a),width=Math.max(.7,x(b)-left),y=25+rowMap.get(p[2])*25;ctx.fillStyle=defs[p[2]][2];ctx.fillRect(left,y,width,18);if(width>86){ctx.fillStyle='#fff';ctx.fillText((p[3]?'V'+p[3]+' ':'')+fmt(p[1])+'ms',left+3,y+13)}let os=D.etwAvailable&&D.lanes[String(p[6])]?intervalStats(D.lanes[String(p[6])],a,b):null;phaseHits.push({x:left,y,w:width,h:18,text:`${defs[p[2]][0]} · ${D.markers[p[5]]}\nTID ${p[6]} · ${p[3]?'view '+p[3]+' / realFrame '+p[4]:'뷰 귀속 미확정/전역'}\n${fmt(p[0])} → ${fmt(p[0]+p[1])} ms\n전체 ${fmt(p[1])}ms · 현재 범위 ${fmt(b-a)}ms${os?'\nRunning '+fmt(os.s[1])+' / Ready '+fmt(os.s[2])+' / Waiting '+fmt(os.s[3])+' / Unknown '+fmt(os.s[0])+'ms\nDPC/ISR 겹침 '+fmt(os.dpc)+'ms':'\nETW 없음'}`});let s=stats[p[2]];s.sum+=Math.max(0,b-a);s.count++;if(os){s.os=s.os.map((v,i)=>v+os.s[i]);s.dpc+=os.dpc}}$('phaseStats').innerHTML=indices.map(i=>{let s=stats[i];return `<tr><td title="${defs[i][1]}">${defs[i][0]}</td><td>${D.markers.includes(defs[i][1])?fmt(s.sum):'미계측'}</td><td>${s.count}</td><td>${D.etwAvailable?fmt(s.os[1])+' / '+fmt(s.os[2])+' / '+fmt(s.os[3]):'ETW 없음'}</td></tr>`}).join('')}
function intervalStats(l,a,b){let s=[0,0,0,0,0],dpc=0;for(let z of ranged(l.states,a,b)){let left=Math.max(a,z[0]),right=Math.min(b,z[0]+z[1]);s[z[2]]+=Math.max(0,right-left);if(z[2]===1){for(let d of ranged(D.dpc[String(z[3])]||[],left,right))dpc+=overlap(left,right,d[0],d[0]+d[1])}}return {s,dpc}}

function stateStats(l){let s=[0,0,0,0,0],dpc=0;for(let z of ranged(l.states,A,B)){let a=Math.max(A,z[0]),b=Math.min(B,z[0]+z[1]);s[z[2]]+=Math.max(0,b-a);if(z[2]===1){for(let d of ranged(D.dpc[String(z[3])]||[],a,b))dpc+=overlap(a,b,d[0],d[0]+d[1])}}return {s,dpc}}
function drawThreads(){let all=$('allThreads').checked,full=$('allScopes').checked,query=$('search').value.toLowerCase();visibleLanes=D.laneList.filter(l=>all||l.scopes.length||l.names.some(x=>/RenderThread|JobWorker|Dispatcher|Presentation|MainThread|Collector|Creator\.RHI\./.test(x)));visibleLanes.sort((a,b)=>{const rank=l=>{let n=l.names.join(' ');return n.includes('RenderThread')?0:/Worker/.test(n)?1:n.includes('Dispatcher')?2:n.includes('Presentation')?3:4};return rank(a)-rank(b)||a.tid-b.tid});let layouts=[],height=24;for(let l of visibleLanes){let scopes=ranged(l.scopes,A,B,l.maxDuration).filter(z=>{let n=D.markers[z[2]];return (!query||n.toLowerCase().includes(query))&&(full||query||defs.some(x=>x[1]===n)||n==='RenderThreadFrame'||n==='RenderGraphBuild'||n.startsWith('RenderCommandRecord.Worker'))});let min=scopes.reduce((a,z)=>Math.min(a,z[3]),65535),max=scopes.reduce((a,z)=>Math.max(a,z[3]),min);let h=Math.max(36,Math.min(210,26+(max-min+1)*10));layouts.push({l,scopes,min,y:height,h});height+=h}let {ctx,w}=canvas('threads',height+5);ctx.clearRect(0,0,w,height+5);grid(ctx,w,height+5);threadHits=[];let x=t=>170+(t-A)/(B-A)*(w-170),rows=[];for(let row of layouts){let {l,y,h,scopes,min}=row;ctx.fillStyle=ink();let title=labelLane(l);ctx.fillText(title.length>21?title.slice(0,20)+'…':title,5,y+11);ctx.fillText('TID '+l.tid,5,y+26);let seen=new Set();for(let z of ranged(l.states,A,B)){let left=x(Math.max(A,z[0])),width=Math.max(.7,x(Math.min(B,z[0]+z[1]))-left);let key=Math.floor(left)+':'+z[2];if(!seen.has(key)){ctx.fillStyle=stateColors[z[2]];ctx.fillRect(left,y,width,10);seen.add(key)}threadHits.push({x:left,y,w:width,h:10,text:`${title} · TID ${l.tid}\n${stateNames[z[2]]}${z[2]===1?' · CPU '+z[3]:''}\n${fmt(z[0])} → ${fmt(z[0]+z[1])} ms\n원래 ${fmt(z[1])}ms · 현재 범위 ${fmt(overlap(A,B,z[0],z[0]+z[1]))}ms`})}if(!D.etwAvailable){ctx.fillStyle='#90969d';ctx.fillRect(170,y,w-170,9)}seen.clear();for(let z of scopes){let name=D.markers[z[2]],phase=defs.findIndex(t=>t[1]===name),left=x(Math.max(A,z[0])),width=Math.max(.7,x(Math.min(B,z[0]+z[1]))-left),top=y+14+Math.min(17,z[3]-min)*10;let key=Math.floor(left)+':'+top+':'+phase;if(!seen.has(key)){ctx.fillStyle=phase>=0?defs[phase][2]:'#55717c';ctx.globalAlpha=z[4]&3?.45:.9;ctx.fillRect(left,top,width,8);ctx.globalAlpha=1;seen.add(key)}threadHits.push({x:left,y:top,w:width,h:8,text:`${name}\n${title} · TID ${l.tid} · depth ${z[3]} · flags ${z[4]}\n${fmt(z[0])} → ${fmt(z[0]+z[1])} ms\n전체 ${fmt(z[1])}ms · 현재 범위 ${fmt(overlap(A,B,z[0],z[0]+z[1]))}ms\nCPU tick ${z[5]} / task ${z[6]}${z[4]&3?'\n부분 스코프: 캡처 경계가 잘렸을 수 있음':''}`})}ctx.strokeStyle='#9aa1a633';ctx.beginPath();ctx.moveTo(0,y+h-2);ctx.lineTo(w,y+h-2);ctx.stroke();if(D.etwAvailable){let st=stateStats(l);rows.push(`<tr title="${escapeHtml(title)}"><td>${l.tid}</td><td>${fmt(st.s[1])}</td><td>${fmt(st.s[2])}</td><td>${fmt(st.s[3])}</td><td>${fmt(st.s[0])}</td><td>${D.dpcAvailable === false ? '미수집' : fmt(st.dpc)}</td></tr>`)}}$('stateStats').querySelector('tbody').innerHTML=D.etwAvailable?rows.join(''):'<tr><td colspan="6">비추적 기록 · ETW 상태 없음</td></tr>';window.CETimelineVisibleThreads=layouts.length}
function draw(){if(!D)return;drawOverview();drawPhases();drawGpuTimeline();drawThreads();window.CETimelineRange=[A,B];drawRhiIdentity();drawReadyOwners()}
function hit(canvasId,event){let c=$(canvasId),rect=c.getBoundingClientRect(),x=event.clientX-rect.left,y=event.clientY-rect.top,list=canvasId==='phases'?phaseHits:canvasId==='contention'?contentionHits:threadHits;for(let i=list.length-1;i>=0;i--){let h=list[i];if(x>=h.x&&x<=h.x+Math.max(2,h.w)&&y>=h.y&&y<=h.y+h.h)return h.text}return null}
for(let id of ['phases','threads','contention']){$(id).addEventListener('mousemove',e=>{let text=hit(id,e);if(!text){$('tip').style.display='none';return}$('tip').textContent=text;$('tip').style.display='block';$('tip').style.left=Math.max(4,Math.min(innerWidth-340,e.clientX+12))+'px';$('tip').style.top=Math.max(4,Math.min(innerHeight-200,e.clientY+12))+'px'});$(id).addEventListener('mouseleave',()=>$('tip').style.display='none');$(id).addEventListener('click',e=>{let t=hit(id,e);if(t)$('inspect').textContent=t});$(id).addEventListener('wheel',e=>{if(!e.ctrlKey||!D)return;e.preventDefault();let c=$(id),x=(e.clientX-c.getBoundingClientRect().left-170)/(c.clientWidth-170),center=A+(B-A)*Math.max(0,Math.min(1,x)),span=(B-A)*(e.deltaY>0?1.4:.7);setRange(center-span/2,center+span/2)},{passive:false})}
let overviewDown=null;$('overview').addEventListener('pointerdown',e=>{overviewDown=e.offsetX;$('overview').setPointerCapture(e.pointerId)});$('overview').addEventListener('pointerup',e=>{if(!D||overviewDown===null)return;let w=$('overview').clientWidth,map=x=>D.begin+Math.max(0,Math.min(w,x))/w*(D.end-D.begin),a=map(overviewDown),b=map(e.offsetX);if(Math.abs(e.offsetX-overviewDown)<4)setRange(b-(B-A)/2,b+(B-A)/2);else setRange(Math.min(a,b),Math.max(a,b));overviewDown=null});
$('dataset').onchange=()=>loadDataset($('dataset').value).catch(fail);$('unit').onchange=()=>{setupFrames();chooseFrame('median')};$('frame').onchange=()=>{let z=frames[Number($('frame').value)];if(z)setRange(z[0],z[0]+z[1])};$('median').onclick=()=>chooseFrame('median');$('worst').onclick=()=>chooseFrame('worst');$('full').onclick=()=>setRange(D.begin,D.end);$('apply').onclick=()=>setRange(Number($('start').value),Number($('start').value)+Number($('span').value));for(let b of document.querySelectorAll('[data-span]'))b.onclick=()=>{let center=(A+B)/2,span=Number(b.dataset.span);setRange(center-span/2,center+span/2)};for(let id of ['view','allThreads','allScopes','detail'])$(id).onchange=draw;let searchTimer;$('search').oninput=()=>{clearTimeout(searchTimer);searchTimer=setTimeout(draw,180)};let resizeTimer;addEventListener('resize',()=>{clearTimeout(resizeTimer);resizeTimer=setTimeout(draw,150)});function fail(e){$('status').textContent='데이터 오류: '+e.message;console.error(e)}

defs.push(...[["선언 Join", "RenderGraphDeclareJoin", "#966000"], ["선언 MeshFill", "RenderGraphDeclareMeshFill", "#966000"], ["선언 MeshPlan", "RenderGraphDeclareMeshPlan", "#966000"]]);
phaseGroups.declare.push(...["RenderGraphDeclareJoin", "RenderGraphDeclareMeshFill", "RenderGraphDeclareMeshPlan"]);

defs.push(...[]);
phaseGroups.preparation.push(...[]);

defs.push(...[["RHI 소프트웨어 번역", "RHISoftwareTranslate", "#177487"], ["RHI 작업 실행", "RHIExecuteWork", "#746047"], ["DX12 제출 접수", "DX12EnqueueCommands", "#966000"]]);
defs.push(['기하 준비·연속 인덱스', 'MaterialSceneGeometryPreparation', '#24765e']);
phaseGroups.scene.push('MaterialSceneGeometryPreparation');
phaseGroups.mesh.push('MaterialSceneGeometryPreparation');
defs.push(['피드백 되읽기 생성', 'GeometryFeedbackReadbackCreate', '#a46a2c']);
phaseGroups.scene.push('GeometryFeedbackReadbackCreate');
phaseGroups.mesh.push('GeometryFeedbackReadbackCreate');
defs.push(['요청 순서 미스 재구성', 'MaterialClusterRequestSort', '#a46a2c']);
phaseGroups.mesh.push('MaterialClusterRequestSort');
phaseGroups.scene.push('MaterialClusterRequestSort');
defs.push(...[
    ['테스트 입력 준비 포함', 'GeometryScaleFixtureSeal', '#6b737c'],
    ['Scene 입력 봉인', 'MaterialSceneInputSeal', '#24765e'],
    ['기하 identity 검증', 'MaterialSceneSealGeometryIdentities', '#966000'],
    ['descriptor·payload pin 검증', 'MaterialSceneSealPinValidation', '#966000'],
    ['불변 기하 계획·pose 바인딩', 'MaterialMeshPlanForScene', '#177487']
]);
phaseGroups.seal = ['GeometryScaleFixtureSeal', 'MaterialSceneInputSeal', 'MaterialSceneSealGeometryIdentities',
    'MaterialSceneSealPinValidation', 'MaterialMeshPlanForScene'];
phaseGroups.rhi=["RenderCommandRecord", "RenderSubmit", "RHISoftwareTranslate", "RHIExecuteWork", "DX12EnqueueCommands"];


function drawRhiIdentity(){const all=D.rhiIdentities||[],rows=all.filter(x=>x[0]<B&&x[1]>A),status=$('rhiIdentityStatus');status.textContent=all.length?`선택 범위 ${rows.length.toLocaleString()}개 / 전체 ${all.length.toLocaleString()}개 · 앞 200개 표시 · 원본 전체는 측정 JSON에 보존`:'이 데이터셋에는 RHI identity 계측이 없습니다.';$('rhiIdentityRows').innerHTML=rows.slice(0,200).map(x=>`<tr><td>${x[0].toFixed(4)}</td><td>${(x[1]-x[0]).toFixed(4)}</td><td>${escapeHtml(D.markers[x[2]])}<br>TID ${x[3]}</td><td>${x[4]}</td><td>${x[5]} / V${x[6]}</td><td>${x[7]}:${x[9]} / ${['Graphics','Compute','Copy'][x[10]]||x[10]}</td><td>${x[8]} / ${x[11]===4294967295?'미지정':x[11]}</td><td>${x[12]===null?'미제공':x[12]}</td></tr>`).join('');window.CERhiIdentityCount=all.length;window.CERhiVisibleCount=rows.length;}

// Integrated into the existing CEPROF CPU/ETW viewer, which owns D and A/B.
let gpuHits = [];

async function attachGpuData(dataset) {
    const entry = MANIFEST.find(item => item.id === dataset.id);
    dataset.gpu = await readDatasetJson(entry.gpu || dataset.id + '-gpu.json');
    if (dataset.gpu.summary.sourceSha256 !== dataset.nativeSha256) {
        throw new Error('CPU/GPU source capture mismatch');
    }
}

function gpuUnion(intervals) {
    const merged = [];
    for (const interval of intervals.sort((a, b) => a[0] - b[0])) {
        const last = merged.at(-1);
        if (last && interval[0] <= last[1]) {
            last[1] = Math.max(last[1], interval[1]);
        } else {
            merged.push([...interval]);
        }
    }
    return merged.reduce((sum, interval) => sum + interval[1] - interval[0], 0);
}

function gpuPassName(marker) {
    return D.markers[marker].replace(/ \[gpu scope \d+\]$/, '');
}

function drawGpuTimeline() {
    if (!D.gpu) {
        return;
    }
    const view = Number($('view').value);
    const query = $('gpuSearch').value.toLowerCase();
    const records = ranged(D.gpu.events, A, B, D.gpu.maxDuration).filter(event =>
        (!view || event[4] === view) && (!query || gpuPassName(event[2]).toLowerCase().includes(query)));
    const groups = new Map();
    for (const event of records) {
        const key = event[3] + ':' + event[4];
        if (!groups.has(key)) {
            groups.set(key, []);
        }
        groups.get(key).push(event);
    }
    const layouts = [];
    let height = 28;
    for (const [key, events] of groups) {
        const ends = [];
        const lanes = events.map(event => {
            let lane = ends.findIndex(end => end <= event[0]);
            if (lane === -1) {
                lane = ends.length;
            }
            ends[lane] = event[0] + event[1];
            return lane;
        });
        const rowHeight = Math.max(36, ends.length * 16 + 20);
        layouts.push({key, events, lanes, y: height, height: rowHeight});
        height += rowHeight;
    }
    const {ctx, w} = canvas('gpuPasses', Math.max(70, height));
    ctx.clearRect(0, 0, w, Math.max(70, height));
    grid(ctx, w, Math.max(70, height));
    const coordinate = time => 170 + (time - A) / (B - A) * (w - 170);
    gpuHits = [];
    const totals = new Map();
    const queueIntervals = new Map();
    for (const row of layouts) {
        const [queue, viewId] = row.key.split(':');
        ctx.fillStyle = ink();
        ctx.fillText('GPU Q' + queue + ' · V' + viewId, 5, row.y + 12);
        ctx.fillText((D.gpu.queues[queue] || '').slice(0, 22), 5, row.y + 27);
        row.events.forEach((event, index) => {
            const name = gpuPassName(event[2]);
            const left = coordinate(Math.max(A, event[0]));
            const end = Math.min(B, event[0] + event[1]);
            const width = Math.max(1, coordinate(end) - left);
            const top = row.y + row.lanes[index] * 16;
            ctx.fillStyle = event[4] === 2 ? '#177487' : '#966000';
            ctx.fillRect(left, top, width, 12);
            if (width > 100) {
                ctx.save();
                ctx.beginPath();
                ctx.rect(left + 2, top, width - 4, 13);
                ctx.clip();
                ctx.fillStyle = '#ffffff';
                ctx.fillText(name + ' ' + fmt(event[1]) + 'ms', left + 3, top + 10);
                ctx.restore();
            }
            const clipped = Math.max(0, end - Math.max(A, event[0]));
            gpuHits.push({x: left, y: top, w: width, h: 13, text:
                `${name}\nGPU queue ${event[3]} · view ${event[4]} · submission ${event[5]}` +
                `\nsource engineFrame ${event[6]} · realFrame ${event[7]}` +
                `\nframeKind ${event[8]} · generatedOrdinal ${event[9]}` +
                `\n${fmt(event[0])} → ${fmt(event[0] + event[1])}ms` +
                `\n원본 ${fmt(event[1])}ms · 현재 범위 ${fmt(clipped)}ms` +
                `\nQPC ticks ${event[10]} → ${event[11]}\nCPU/GPU 공통 QPC · 수집 시점 아님`});
            const key = event[4] + ' / Q' + event[3] + ' / ' + name;
            const total = totals.get(key) || {sum: 0, calls: 0, max: 0};
            total.sum += clipped;
            total.calls++;
            total.max = Math.max(total.max, event[1]);
            totals.set(key, total);
            if (!queueIntervals.has(event[3])) {
                queueIntervals.set(event[3], []);
            }
            queueIntervals.get(event[3]).push([Math.max(A, event[0]), end]);
        });
    }
    if (!records.length) {
        ctx.fillStyle = ink();
        ctx.fillText('현재 범위에 대응 GPU 패스 없음 · CPU/GPU 확대 버튼으로 확인', 175, 47);
    }
    $('gpuStats').innerHTML = [...totals].sort((a, b) => b[1].sum - a[1].sum).map(([name, value]) =>
        `<tr><td>${escapeHtml(name)}</td><td>${fmt(value.sum)}</td><td>${value.calls}</td><td>${fmt(value.max)}</td></tr>`).join('') ||
        '<tr><td colspan="4">현재 시간 범위·뷰·검색 조건에 GPU 패스 없음</td></tr>';
    const queueText = [...queueIntervals].map(([queue, intervals]) => {
        const first = intervals.reduce((value, interval) => Math.min(value, interval[0]), Infinity);
        const last = intervals.reduce((value, interval) => Math.max(value, interval[1]), -Infinity);
        return `Q${queue}: 계측 합집합 ${fmt(gpuUnion(intervals))}ms / 첫~마지막 폭 ${fmt(last-first)}ms`;
    }).join(' · ');
    $('gpuStatus').textContent = D.gpu.summary.available === false ? D.gpu.summary.reason : `${D.gpu.events.length.toLocaleString()} 원본 GPU spans · 범위 내 ${records.length}개 · ` +
        `${D.gpu.summary.exactProvenanceMatches.toLocaleString()} exact 귀속 · ` + queueText;
    window.CETimelineGpu = {dataset: D.id, total: D.gpu.events.length, visible: records.length,
        queues: [...queueIntervals.keys()], exactMatches: D.gpu.summary.exactProvenanceMatches};
}

function fitCpuAndGpu() {
    if (!D?.gpu) {
        return;
    }
    const view = Number($('view').value) || (Number($('unit').value) || 2);
    const middle = (A + B) / 2;
    const distance = interval => Math.max(interval[0] - middle, middle - interval[0] - interval[1], 0);
    const candidates = D.measurements.filter(measurement => measurement[3] === view);
    candidates.sort((a, b) => distance(a) - distance(b));
    const cpu = candidates[0];
    if (!cpu) {
        return;
    }
    const spans = D.gpu.events.filter(event => event[4] === view && event[7] === String(cpu[2]));
    if (!spans.length) {
        $('gpuStatus').textContent = `realFrame ${cpu[2]} / view ${view}: 원본 GPU 귀속 없음`;
        return;
    }
    const begin = Math.min(cpu[0], ...spans.map(event => event[0]));
    const end = Math.max(cpu[0] + cpu[1], ...spans.map(event => event[0] + event[1]));
    setRange(begin - .25, end + .25);
}

$('gpuFit').onclick = fitCpuAndGpu;
$('gpuSearch').oninput = drawGpuTimeline;
for (const type of ['mousemove', 'click']) {
    $('gpuPasses').addEventListener(type, event => {
        const bounds = $('gpuPasses').getBoundingClientRect();
        const x = event.clientX - bounds.left - $('gpuPasses').clientLeft;
        const y = event.clientY - bounds.top - $('gpuPasses').clientTop;
        const selected = [...gpuHits].reverse().find(hit => x >= hit.x && x <= hit.x + hit.w && y >= hit.y && y <= hit.y + hit.h);
        if (type === 'click') {
            if (selected) {
                $('inspect').textContent = selected.text;
            }
            return;
        }
        $('tip').style.display = selected ? 'block' : 'none';
        if (selected) {
            $('tip').textContent = selected.text;
            $('tip').style.left = Math.max(4, Math.min(innerWidth - 340, event.clientX + 12)) + 'px';
            $('tip').style.top = Math.max(4, Math.min(innerHeight - 200, event.clientY + 12)) + 'px';
        }
    });
}
$('gpuPasses').addEventListener('mouseleave', () => { $('tip').style.display = 'none'; });

// Data files and measurement catalogs are separate from this reusable viewer.
let manifestLocation = null;
let localFiles = null;
let currentCollection = null;

async function readDatasetJson(path) {
    if (localFiles) {
        const file = localFiles.get(path) || localFiles.get(path.split('/').at(-1));
        if (!file) throw Error('선택한 폴더에 JSON이 없습니다: ' + path);
        return JSON.parse(await file.text());
    }
    const response = await fetch(new URL(path, manifestLocation), { cache: 'no-store' });
    if (!response.ok) throw Error('JSON 읽기 실패: ' + response.status + ' ' + path);
    return response.json();
}

function sourceUrl(path) {
    return localFiles ? '#' : new URL(path, new URL('../', manifestLocation)).href;
}

function hydrateDataset(data) {
    if (data.phases && data.loops) return;
    const index = new Map();
    defs.forEach((definition, position) => {
        if (!index.has(definition[1])) index.set(definition[1], position);
    });
    const measure = data.measurements;
    const preceding = stamp => {
        let lo = 0, hi = measure.length;
        while (lo < hi) {
            const mid = (lo + hi) >>> 1;
            if (measure[mid][0] <= stamp) lo = mid + 1;
            else hi = mid;
        }
        return lo - 1;
    };
    const phases = [], loops = [...(data.frameBoundaries || [])];
    for (const lane of Object.values(data.lanes)) {
        for (const scope of lane.scopes) {
            const [start, duration, marker] = scope;
            const name = data.markers[marker];
            if (name === 'RenderThreadFrame' && !(scope[4] & 3)) loops.push([start, duration, scope[5], 0]);
            const stage = index.get(name);
            if (stage === undefined) continue;
            let view = 0, frame = 0;
            if ((stage < 5 || stage >= 9) && !['RHISoftwareTranslate','RHIExecuteWork','DX12EnqueueCommands'].includes(name)) {
                const end = preceding(start + .0002);
                for (let candidate = end; candidate >= Math.max(0, end - 3); --candidate) {
                    const m = measure[candidate];
                    if (m[0] <= start + .0002 && start + duration <= m[0] + m[1] + .0002) {
                        view = m[3]; frame = m[2]; break;
                    }
                }
            }
            phases.push([start, duration, stage, view, frame, marker, lane.tid]);
        }
    }
    data.phases = phases.sort((a,b) => a[0]-b[0]);
    data.loops = loops.sort((a,b) => a[0]-b[0]);
    data.maxPhaseDuration = phases.reduce((maximum, phase) => Math.max(maximum, phase[1]), 0);
}

async function openManifest(path, desired) {
    version++;
    manifestLocation = localFiles ? null : new URL(path, location.href);
    const raw = localFiles ? JSON.parse(await localFiles.get('manifest.json').text()) : await readDatasetJson(manifestLocation.href);
    MANIFEST = Array.isArray(raw) ? raw : raw.datasets;
    if (!Array.isArray(MANIFEST) || !MANIFEST.length) throw Error('측정 목록이 비어 있습니다.');
    $('dataset').replaceChildren(...MANIFEST.map(item => new Option(
        `${item.label || item.variant} · ${item.mode === 'etw' ? 'ETW' : '비추적'} · ${item.events.toLocaleString()} scopes`, item.id)));
    const id = desired && MANIFEST.some(item => item.id === desired) ? desired : MANIFEST.at(-1).id;
    $('dataset').value = id;
    $('manifestUrl').value = localFiles ? '선택한 로컬 폴더' : manifestLocation.href;
    $('collectionStatus').textContent = (raw.title || currentCollection?.label || '측정 기록') + ' · JSON만 교체하여 사용하는 공용 뷰어';
    if (!localFiles) {
        $('reportLink').href = new URL(raw.report || '../index.html', manifestLocation).href;
        $('alignmentLink').href = new URL('alignment-verdict.json', manifestLocation).href;
        $('verdictLink').href = new URL('view-verdict.json', manifestLocation).href;
        const query = new URL(location.href);
        query.searchParams.set('manifest', manifestLocation.pathname);
        history.replaceState(null, '', query);
    }
    await loadDataset(id);
    const query = new URLSearchParams(location.search);
    if (query.has('detail')) $('detail').value = query.get('detail');
    if (query.has('start') && query.has('span')) setRange(Number(query.get('start')), Number(query.get('start')) + Number(query.get('span')));
    else draw();
}

$('openManifest').onclick = () => { localFiles = null; openManifest($('manifestUrl').value).catch(fail); };
$('localFolder').onchange = async event => {
    try {
        const files = [...event.target.files];
        const manifest = files.find(file => file.webkitRelativePath.endsWith('/timeline/viewer.json')) || files.find(file => file.name === 'viewer.json') || files.find(file => file.name === 'manifest.json');
        if (!manifest) throw Error('manifest.json이 있는 타임라인 폴더를 선택하세요.');
        localFiles = new Map(files.map(file => [file.name, file]));
        localFiles.set('manifest.json', manifest);
        await openManifest(null);
    } catch (error) { fail(error); }
};

async function bootViewer() {
    const query = new URLSearchParams(location.search);
    let catalog = [];
    if (location.protocol !== 'file:') {
        const response = await fetch('/Artifacts/timeline-viewer/catalog.json');
        if (response.ok) catalog = (await response.json()).measurements;
    }
    $('collection').replaceChildren(...catalog.map(item => new Option(item.label, item.manifest)));
    $('collection').onchange = () => {
        localFiles = null;
        currentCollection = catalog.find(item => item.manifest === $('collection').value);
        openManifest($('collection').value).catch(fail);
    };
    const selected = query.get('manifest') || catalog.at(-1)?.manifest;
    if (selected) {
        $('collection').value = selected;
        currentCollection = catalog.find(item => item.manifest === selected);
        await openManifest(selected, query.get('dataset'));
    }
}
bootViewer().catch(fail);

let contentionHits = [];
function drawReadyOwners() {
    const o=D.readyOwners,section=$('readyOwnership');
    if(!section)return;
    section.style.display=o?'block':'none';
    contentionHits=[];
    if(!o)return;
    const labels={render:'렌더',worker:'엔진 워커·배정',rhi_submission:'RHI 제출',rhi_translation:'RHI 번역',same_process_other:'동일 프로세스 기타',external:'외부',idle:'Idle',unknown:'미확인'};
    const colors={render:'#28785d',worker:'#355ba2',rhi_submission:'#177487',rhi_translation:'#775c9c',same_process_other:'#6b737c',idle:'#b0b0b0',unknown:'#909090'};
    const height=50+o.cores*26,{ctx,w}=canvas('contention',height);
    ctx.clearRect(0,0,w,height);grid(ctx,w,height);
    const x=t=>170+(t-A)/(B-A)*(w-170),totals=new Map();let ready=0,cpuTotal=0,external=0;
    ctx.fillStyle=ink();ctx.fillText('렌더 Ready',5,30);
    for(let c=0;c<o.cores;c++)ctx.fillText('CPU '+c,5,58+c*26);
    for(const z of o.ready){const d=overlap(A,B,z[0],z[0]+z[1]);if(!d)continue;ready+=d;ctx.fillStyle='#b47a0a';const left=x(Math.max(A,z[0])),width=Math.max(.7,x(Math.min(B,z[0]+z[1]))-left);ctx.fillRect(left,22,width,11);contentionHits.push({x:left,y:22,w:width,h:11,text:`렌더 TID ${o.mainTid} · Prepare Ready\n${fmt(z[0])} → ${fmt(z[0]+z[1])} ms · ${fmt(z[1])}ms`});}
    for(const z of o.occupancy){const d=overlap(A,B,z[0],z[0]+z[1]);if(!d)continue;const actor=o.actors[z[3]];cpuTotal+=d;if(actor.category==='external')external+=d;let row=totals.get(z[3]);if(!row){row={actor,ms:0,priorities:new Set()};totals.set(z[3],row)}row.ms+=d;row.priorities.add(z[4]);const left=x(Math.max(A,z[0])),width=Math.max(.7,x(Math.min(B,z[0]+z[1]))-left),y=49+z[2]*26;ctx.fillStyle=actor.category==='external'?`hsl(${25+(z[3]*37)%45} 58% 45%)`:colors[actor.category];ctx.fillRect(left,y,width,13);contentionHits.push({x:left,y,w:width,h:13,text:`CPU ${z[2]} · ${labels[actor.category]}\n${actor.image} · PID ${actor.pid} · TID ${actor.tid}\n${actor.name||'이름 미수집'} · 동적 우선순위 ${z[4]}\n${fmt(z[0])} → ${fmt(z[0]+z[1])} ms · 전체 ${fmt(z[1])} CPU-ms`});}
    $('readyOwnerStatus').textContent=`렌더 TID ${o.mainTid} · Ready ${fmt(ready)} wall-ms · ${o.cores} CPU 점유 합 ${fmt(cpuTotal)} CPU-ms · 외부 ${(cpuTotal?external/cpuTotal*100:0).toFixed(2)}%`;
    $('readyOwnerRows').innerHTML=[...totals.values()].sort((a,b)=>b.ms-a.ms).slice(0,24).map(r=>`<tr><td>${labels[r.actor.category]}</td><td>${r.actor.pid??'미확인'} · ${escapeHtml(r.actor.image)}</td><td>${r.actor.tid} · ${escapeHtml(r.actor.name||'이름 미수집')}</td><td>${[...r.priorities].sort((a,b)=>a-b).join(', ')}</td><td>${fmt(r.ms)}</td></tr>`).join('')||'<tr><td colspan="5">현재 범위에 Prepare Ready 없음</td></tr>';
}
