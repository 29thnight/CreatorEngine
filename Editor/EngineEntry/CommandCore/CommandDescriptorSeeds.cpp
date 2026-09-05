#include "CommandDescriptorSeeds.h"

#include <algorithm>
#include <iterator>
#include <string_view>

// LC3 (PHASE 14.5) — 명령 schema 의 seed 표.
//
// ── 왜 등록 줄이 아니라 별도 표인가 ─────────────────────────────────────
//
// 요약을 등록 줄에 붙이면 208 줄이 전부 바뀌고, 그 diff 안에서 거동 변경 하나를
// 아무도 못 본다(§15 의 위험 표가 같은 이유로 서명 일괄 변경을 금지한다).
// 표를 따로 두면 등록 줄은 그대로고, schema 는 한 파일에서 통째로 읽힌다.
//
// ── 요약은 어디서 왔나 ──────────────────────────────────────────────────
//
//   · 131 개 — 현행 `PrintHelp()` 문자열. 그것이 오늘의 정본 문서다.
//   · 77 개 — 핸들러의 주석과 `printf` 문안을 읽고 적었다. help 에 한 번도
//     실린 적 없던 것들이고, 그래서 LC0 이 잰 help coverage 가 63%% 였다.
//
// help 가 안내하지만 등록돼 있지 않던 이름 6 개(`experiment.anim` ·
// `bench` · `fbx` · `gltf` · `import` · `model`)는 여기 없다. 등록에 없는
// 이름은 schema 에도 없어야 한다 — 그것이 §3.4 drift 를 닫는 방식이다.
//
// ★ 이 파일은 손으로 유지한다. 새 명령을 등록하면 여기에 항목을 더해야 하고,
//   더하지 않으면 registry 가 등록을 거부한다(CommandRegistry::Add).

namespace CommandCore
{
    namespace
    {
        // 정렬된 표. 이름으로 이진 탐색한다.
        constexpr DescriptorSeed kSeeds[] = {
            { "ai.status", CommandCost::Immediate, "[오브젝트]", "AI 레지스트리 등록 수를 낸다(오브젝트를 주면 그 하나)", CommandClass::EngineService, CommandLiveness::Live },
            { "animator.exit", CommandCost::Immediate, "<오브젝트>", "Animator의 현재 상태를 빠져나가게 한다", CommandClass::EngineService, CommandLiveness::Live },
            { "animator.param", CommandCost::Frames, "<오브젝트> <파라미터> <bool|float|int|trigger>", "Animator 파라미터를 저작한다", CommandClass::EngineService, CommandLiveness::Live },
            { "animator.scene.probe", CommandCost::Frames, "", "Animator 컨트롤러 그래프를 scene reflection YAML로 왕복시킨다", CommandClass::Probe, CommandLiveness::Live },
            { "animator.state", CommandCost::Frames, "<오브젝트> <상태 이름> <스크립트 타입>", "Animator 컨트롤러에 상태와 행동을 저작한다", CommandClass::EngineService, CommandLiveness::Live },
            { "asset.guid.rename.probe", CommandCost::Frames, "", "자산 GUID가 이름 변경을 건너 보존되는지 본다(재질 왕복 포함)", CommandClass::Probe, CommandLiveness::Live },
            { "assets.generation", CommandCost::Long, "<project root>", "UUIDv8 model generation closure·원자 cache publish/retire 검증", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.generationcorpus", CommandCost::Long, "<content root>", "현재 MBC4 model corpus generation cold-load 검증", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.identity", CommandCost::Long, "", "자산 identity(UUID·epoch·프로필) 전수 계약을 판정한다", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.modelbench", CommandCost::Long, "<dir|-> <반복> [cooked|author]", "모델 로드·저작 트랜잭션 시간과 peak working set을 잰다", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.modeldiag", CommandCost::Immediate, "", "모델 소비 계수를 읽는다(상태를 바꾸지 않는다)", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.modelrender", CommandCost::Frames, "", "모델 렌더 배선(mesh·재질 해석)을 판정한다", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.scenemodel", CommandCost::Frames, "[reload <모델 이름>]", "활성 씬의 모델 소비가 typed generation handle로 서 있는지 본다", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.sidecar", CommandCost::Long, "", "모델 sidecar v2 스키마를 전수로 판정한다", CommandClass::EngineService, CommandLiveness::Live },
            { "assets.unload", CommandCost::Frames, "", "사용하지 않는 에셋 캐시 정리", CommandClass::EngineService, CommandLiveness::Live },
            { "blackboard.authoring.probe", CommandCost::Frames, "<이름> [empty|noname]", "Blackboard 저장·재로드 왕복으로 키 값이 살아 돌아오는지 본다", CommandClass::RawFixture, CommandLiveness::Live },
            { "bt.status", CommandCost::Immediate, "", "행동 트리 지표(트리 수·틱 누계·프레임당 경계 통과)", CommandClass::EngineService, CommandLiveness::Live },
            { "camera.editor", CommandCost::Frames, "match|follow on|off|status", "에디터 카메라를 게임 카메라와 같은 시점으로", CommandClass::EngineService, CommandLiveness::Live },
            { "cli.drain.budget", CommandCost::Immediate, "[<시간ms> <개수>]", "서비스 큐 드레인 예산을 읽거나 바꾼다(LC5 · SLO 게이트의 변이용)", CommandClass::EngineService, CommandLiveness::Live },
            { "cli.echo.args", CommandCost::Immediate, "<인자>", "tokenizer가 만든 토큰을 길이와 함께 되비춘다(LC0 parser golden)", CommandClass::EngineService, CommandLiveness::Live },
            { "cli.probe.timing", CommandCost::Immediate, "[reset|off|경로]", "프레임 시간 분포와 명령 왕복 지연(LC0, 기본 off)", CommandClass::Probe, CommandLiveness::Live },
            { "collisionmatrix.authoring.probe", CommandCost::Frames, "[escape]", "충돌 행렬 저장·재로드 왕복과 설정 루트 이탈 거부를 본다", CommandClass::RawFixture, CommandLiveness::Live },
            { "commands.describe", CommandCost::Immediate, "<이름>", "명령 하나의 descriptor 상세를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "commands.dump", CommandCost::Immediate, "[경로]", "등록 명령 표를 TSV로 덤프한다(LC0 기준선, 소스 스크래핑 대체)", CommandClass::EngineService, CommandLiveness::Live },
            { "commands.list", CommandCost::Immediate, "[경로]", "등록 명령 snapshot 을 TSV 로 낸다(소스 스크래핑 대체)", CommandClass::EngineService, CommandLiveness::Live },
            { "commands.selftest", CommandCost::Immediate, "", "registry 무결성을 판정한다(이름 중복·요약 누락·descriptor 부재)", CommandClass::Probe, CommandLiveness::Live },
            { "component.add", CommandCost::Frames, "<오브젝트 이름> <컴포넌트 타입>", "오브젝트에 컴포넌트를 붙인다", CommandClass::EditorOperation, CommandLiveness::Live },
            { "component.list", CommandCost::Immediate, "", "등록된 컴포넌트 타입을 로그에 남긴다", CommandClass::EngineService, CommandLiveness::Live },
            { "crash.status", CommandCost::Immediate, "", "크래시 덤프 기록자 등록 여부와 덤프 경로를 확인한다", CommandClass::Probe, CommandLiveness::Live },
            { "crash.test", CommandCost::Immediate, "<종류>", "일부러 죽여 덤프 경로를 검증한다(av|abort|terminate|throw)", CommandClass::Probe, CommandLiveness::TerminatesProcess },
            { "dump.list", CommandCost::Immediate, "[이름]", "크래시 덤프 목록 또는 요약 하나를 낸다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.bench11", CommandCost::Long, "", "DX11 vs DX12 API 오버헤드 실측(전제 검증 · Release 전용)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.decal", CommandCost::Frames, "", "데칼 패스 검증(상자 판정·하늘 게이트·원본 혼합 3종·배칭)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.descriptorheap", CommandCost::Frames, "", "descriptor version recycler 검증(completion·Abort·격리·넘침)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.encoderbench", CommandCost::Long, "", "인코더 오버헤드 실측(R3 착수 조건 · Release 전용)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.fog", CommandCost::Frames, "", "볼류메트릭 포그 검증(산란·누적 투과율·시간축 히스토리·합성)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.forward", CommandCost::Frames, "", "DX12 forward 패스를 격리 씬에서 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.forwardscale", CommandCost::Frames, "", "DX12 forward 해상도 스케일을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.forwardshade", CommandCost::Frames, "", "DX12 forward 셰이딩 결과를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.gbuffer", CommandCost::Frames, "", "GBuffer 패스 검증(입력조립·MRT5·깊이·그래프 배리어)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.gizmoicon", CommandCost::Frames, "", "기즈모 아이콘 패스 검증(빌보드 회전·알파 상한·배칭)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.gizmoline", CommandCost::Frames, "", "기즈모 라인 패스 검증(도형 정점 수·픽셀·드로우 병합)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.gizmoscene", CommandCost::Frames, "", "Gizmo 씬 연결 검증(밀봉 복사·4패스 체인·타깃 공유)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.grid", CommandCost::Frames, "", "그리드 패스 검증(라인·셀 내부·밀도·카메라 반응)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.ibl", CommandCost::Frames, "", "IBL 생성 체인 검증(rect→cube·조도·프리필터·BRDF LUT)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.iblshade", CommandCost::Frames, "", "IBL 앰비언트 소비 검증(끔=검정·조도 방향성·금속 정반사)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.live", CommandCost::Frames, "on|status", "EnhancedRenderer 메인 런타임 상태", CommandClass::EngineService, CommandLiveness::Live },
            { "dx12.parallel", CommandCost::Frames, "", "커맨드 기록 병렬화 검증(링 원자성·순차 대비 동일성)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.post", CommandCost::Frames, "", "DX12 후처리 패스를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.postscale", CommandCost::Frames, "", "DX12 후처리 해상도 스케일을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.psocache", CommandCost::Frames, "[파일]", "PSO 캐시 자가 검증(2회차 컴파일 0건)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.rendergraph", CommandCost::Frames, "", "렌더 그래프 검증(순서·흐름·배리어·컬링·실행)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.resize", CommandCost::Frames, "", "크기 추종 검증(DX11 정책·DX12 리사이즈·리사이즈 후 렌더)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.scene", CommandCost::Frames, "", "씬 연결 검증(카메라 스냅샷·메시 업로드·실제 드로우)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.selftest", CommandCost::Frames, "[파일]", "DX12 브링업 자가 검증(삼각형 렌더 → PNG)", CommandClass::Probe, CommandLiveness::RequiresRestart },
            { "dx12.shadowquality", CommandCost::Frames, "", "그림자 품질 검증(경사 비례 편향·캐스케이드 경계 블렌딩 A/B)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.skinning", CommandCost::Frames, "", "GBuffer 스키닝 검증(본 이동·가중 혼합·비스킨드 불변)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.skybox", CommandCost::Frames, "", "스카이박스 패스 검증(면 방향·원평면 밀어넣기·전면 커버)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.ssao", CommandCost::Frames, "", "DX12 SSAO 패스를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.ssaoscale", CommandCost::Frames, "", "DX12 SSAO 해상도 스케일을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.ssgi", CommandCost::Frames, "", "DX12 SSGI 패스를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.ssr", CommandCost::Frames, "", "SSR 패스 검증(반사 발생·금속 마스크·두께 게이트·비트플래그)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.sss", CommandCost::Frames, "", "SSS 패스 검증(번짐·축 분리·표면 추종·에너지)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.ui", CommandCost::Frames, "", "DX12 UI 패스를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.uploadring", CommandCost::Frames, "", "구 명령 별칭(DX12 업로드 세그먼트 검증)", CommandClass::Probe, CommandLiveness::Live },
            { "dx12.wireframe", CommandCost::Frames, "", "와이어프레임 패스 검증(변·내부 비채움·인스턴싱·메시 캐시)", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.animevent", CommandCost::Frames, "seed|verify", "애니메이션 이벤트·루프 오버라이드의 소유 이관을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.animlive", CommandCost::Frames, "", "라이브 팔레트 digest를 낸다(두 번 불러 재생 중인지 가른다)", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.animmask", CommandCost::Frames, "", "AvatarMask 트리 생성을 legacy 재귀와 A/B로 대조한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.animpose", CommandCost::Frames, "<0..1> [클립]", "클립의 특정 지점 포즈를 고정 계산해 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.animtick", CommandCost::Frames, "", "Animator 틱 경로의 패리티를 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.boneresolve", CommandCost::Frames, "", "본 이름 해석 창구를 전수 A/B로 대조한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.cacheopt", CommandCost::Frames, "", "정점 캐시/페치 순서 — ACMR 이 실제로 낮아지는가 + 기하 보존", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.catalog", CommandCost::Frames, "[Derived부모]", "CEMF catalog — 전 GUID 해석·내용 검증·폐포 위상 순서", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.cooked", CommandCost::Long, "[경로]", "쿠킹 포맷 왕복 무손실·거부 동작(경로를 주면 실자산 왕복까지)", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.editorsurface", CommandCost::Frames, "", "에디터 표면 질의(클립 축·mesh 축)의 A/B 동치를 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.foliage", CommandCost::Frames, "seed <자산디렉터리> | verify", "Foliage 메시의 experiment 핸들 합류를 합성으로 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matcodec", CommandCost::Frames, "", "재질 저작 값 인코딩 왕복을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matcook", CommandCost::Long, "[루트 재질 모델]", "재질 의존 폐포 — standalone 재질 + 모델의 임베디드 texture 추출", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matinstance", CommandCost::Frames, "", "MaterialInstance 생성·프로퍼티 블록을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matmigrate", CommandCost::Frames, "", "legacy 재질의 experiment 왕복 이관을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matparity", CommandCost::Frames, "", "재질 해석의 legacy 대 experiment 패리티를 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matresolve", CommandCost::Frames, "", "제품 resolver가 cooked를 고르는지 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matruntime", CommandCost::Frames, "[edit]", "런타임 MaterialInstance 경로를 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matscript", CommandCost::Frames, "", "스크립트가 보는 재질 표면을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.matseal", CommandCost::Frames, "", "재질 sealing 결과를 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.scenecook", CommandCost::Long, "[루트 씬]", "scene/prefab 의존 추출 — 자기참조 제외·못 그린 참조 계수", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.skinbounds", CommandCost::Frames, "", "스킨 팔레트의 유한성·인덱스 범위·크기 상한을 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.smcook", CommandCost::Long, "[루트 메타]", "ShaderMeta 쿠킹 — 정본 파서 검증·source 해소(실자산엔 거부 사례 0)", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.texcook", CommandCost::Long, "[루트 텍스처]", "텍스처 쿠킹 — GUID 주소·내용 해시·fail-closed(실자산은 .dds 미포함)", CommandClass::Probe, CommandLiveness::Live },
            { "experiment.vertexlayout", CommandCost::Frames, "", "typed 정점 레이아웃 디코드를 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "foliage.authoring.probe", CommandCost::Frames, "<이름> [escape]", "Foliage 저작 트랜잭션 왕복과 루트 이탈 거부를 본다", CommandClass::RawFixture, CommandLiveness::Live },
            { "game.pak", CommandCost::Long, "", "Release Player 패키지를 빌드·검증 후 Build/Staging에 게시한다", CommandClass::EngineService, CommandLiveness::Live },
            { "gc.collect", CommandCost::Frames, "", "관리 힙 확정 수집(씬 전환이 자동으로 부르는 그 경로)", CommandClass::Probe, CommandLiveness::Live },
            { "gc.stats", CommandCost::Immediate, "", "관리 힙 지표를 낸다(gc.delta는 직전 대비 증감)", CommandClass::Probe, CommandLiveness::Live },
            { "gpu.baseline", CommandCost::Frames, "", "현재 상태를 기준선으로 삼는다", CommandClass::Probe, CommandLiveness::Live },
            { "gpu.census", CommandCost::Frames, "[라벨]", "VRAM과 엔진 에셋 수를 로그에 기록", CommandClass::Probe, CommandLiveness::Live },
            // LC8 — 두 호스트가 같은 뜻으로 갖는 둘 중 하나다. 요약은 이미 호스트 중립이다.
            { "help", CommandCost::Immediate, "[명령]", "명령 목록 또는 명령 하나의 상세를 낸다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Both },
            { "inputmap.authoring.probe", CommandCost::Frames, "<save|verify> <이름>", "입력 액션맵 저장·재기동 왕복으로 payload 복원을 본다", CommandClass::RawFixture, CommandLiveness::Live },
            { "inputmap.corpus.probe", CommandCost::Long, "", "입력 액션맵 코퍼스를 전수로 읽어 계수를 낸다", CommandClass::Probe, CommandLiveness::Live },
            { "lifecycle.dump", CommandCost::Immediate, "[파일]", "기록을 TSV로 쓴다(기록 0건이면 실패로 끝난다)", CommandClass::EngineService, CommandLiveness::Live },
            { "lifecycle.registry", CommandCost::Frames, "on|off|status", "생명주기 디스패치 경로 전환(9-1, 씬 재로드 필요)", CommandClass::EngineService, CommandLiveness::Live },
            { "lifecycle.stress", CommandCost::Frames, "destroy|churn|reentrant [개수]", "수명 경로를 흔든다(reentrant는 순회 한복판)", CommandClass::EngineService, CommandLiveness::Live },
            { "lifecycle.trace", CommandCost::Frames, "on [틱프레임]|off|clear|status", "생명주기 호출 순서를 받아 적는다", CommandClass::EngineService, CommandLiveness::Live },
            { "log.flush", CommandCost::Immediate, "", "로그를 디스크에 즉시 반영", CommandClass::EngineService, CommandLiveness::Live },
            { "material.corpus.probe", CommandCost::Long, "<이름> ...", "standalone material identity/reference 왕복", CommandClass::Probe, CommandLiveness::Live },
            { "mem.stats", CommandCost::Frames, "", "churn 누계와 기준선을 0으로 — 구간 측정용", CommandClass::Probe, CommandLiveness::Live },
            { "model.load", CommandCost::Long, "<경로>", "모델을 에셋으로 임포트한다(fbx/gltf/glb/obj)", CommandClass::EngineService, CommandLiveness::Live },
            { "model.loadcached", CommandCost::Long, "<모델 경로>", "에디터 드롭 경로(LoadCachedModelShared)로 모델을 연다", CommandClass::EngineService, CommandLiveness::Live },
            { "model.place", CommandCost::Frames, "<이름>", "임포트한 모델을 활성 씬에 배치한다", CommandClass::EngineService, CommandLiveness::Live },
            { "object.create", CommandCost::Frames, "<이름> [타입]", "빈 오브젝트를 만든다(Empty/Light/Camera/Mesh)", CommandClass::EditorOperation, CommandLiveness::Live },
            { "object.duplicate", CommandCost::Frames, "<오브젝트> [새 이름]", "오브젝트를 복제한다(에디터 Ctrl+D와 같은 원시 함수)", CommandClass::EditorOperation, CommandLiveness::Live },
            { "object.parent", CommandCost::Frames, "<자식> <부모 | ->", "오브젝트의 부모를 바꾼다(-는 씬 루트로 올린다)", CommandClass::EditorOperation, CommandLiveness::Live },
            { "object.property", CommandCost::Frames, "<오브젝트> <컴포넌트> <필드> <값>", "리플렉션으로 프로퍼티를 설정한다", CommandClass::EditorOperation, CommandLiveness::Live },
            { "object.rename", CommandCost::Frames, "<이전> <새>", "오브젝트 이름을 바꾼다(같은 모델 여러 번 배치용)", CommandClass::EditorOperation, CommandLiveness::Live },
            { "object.rootref", CommandCost::Frames, "<오브젝트> [루트|-]", "Bone형 same-scene root 참조를 설정/조회한다", CommandClass::EditorOperation, CommandLiveness::Live },
            { "object.transform", CommandCost::Frames, "<이름> <px py pz> [rx ry rz] [sx sy sz]", "변환을 지정한다(회전은 도)", CommandClass::EditorOperation, CommandLiveness::Live },
            { "perf.reflect", CommandCost::Long, "[반복]", "씬 직렬화와 프리팹 역직렬화 시간을 잰다", CommandClass::Probe, CommandLiveness::Live },
            { "pipeline.nodes", CommandCost::Immediate, "", "라이브 파이프라인의 노드 조립 결과를 한 줄씩 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "pix.capture", CommandCost::Frames, "begin|end|status", "PIX 주입 실행의 명시적 GPU 캡처 경계", CommandClass::Probe, CommandLiveness::Live },
            { "play", CommandCost::Frames, "", "에디터의 재생·정지와 같은 동작", CommandClass::EditorOperation, CommandLiveness::Live },
            { "play.state", CommandCost::Immediate, "", "재생 상태(gameStart·paused·씬 로드)를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            // ── Player registry (PHASE 14.5 LC8 · §11.2) ────────────────────
            //
            // ★ 이 다섯은 **Editor 에 등록되지 않는다.** `roles` 가 `Player` 뿐이라
            //   Editor 표에는 부재하고, `commands.selftest` 의 "seed 는 있는데 등록되지
            //   않았다" 검사도 role 을 보고 건너뛴다.
            //
            // ★★ 왜 `player.*` 라는 **새 이름**인가 — Editor 명령을 재사용하지 않는다.
            //
            //   Editor 핸들러는 도메인 TU 안에서 `static` 이고 에디터 시스템(자산
            //   저작·선택·Undo)에 매여 있어 Player 에서 링크할 수 없다. 그러면 같은
            //   이름에 **다른 구현**을 다는 수밖에 없는데, 그것은 §9 가 없애려는
            //   drift 를 이름 단위로 새로 만드는 일이다. 두 호스트가 같은 이름으로
            //   다른 일을 하는 것보다, 다른 이름으로 자기 일을 하는 편이 정직하다.
            //   의미를 맞춰 이름을 합치는 것은 §9 동등성 작업이지 이 슬라이스가 아니다.
            //
            //   예외는 `help`·`quit` 둘이다(아래 `Both`). 그 둘은 호스트에 무관하게
            //   같은 뜻이고, 그래서 요약도 호스트 중립으로 고쳤다.
            // ★★★ `player.move` 가 §11.3 의 "값이 게임을 재시작하지 않고 반영된다" 를
            //   **관측 가능하게** 만드는 자리다. 쓰기만 있고 읽기가 없으면 반영을
            //   주장만 할 수 있다 — `player.object` 로 되읽어야 판정이 된다.
            { "player.move", CommandCost::Frames, "<이름> <x> <y> <z>", "오브젝트의 로컬 위치를 옮긴다(재시작 없이 반영된다)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Player },
            { "player.object", CommandCost::Immediate, "<이름>", "오브젝트 하나의 위치·회전·크기를 낸다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Player },
            { "player.objects", CommandCost::Immediate, "[이름 조각]", "활성 씬의 오브젝트 이름을 나열한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Player },
            { "player.scene", CommandCost::Immediate, "", "활성 씬 이름과 오브젝트 수를 낸다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Player },
            { "player.status", CommandCost::Immediate, "", "프레임 수·재생 상태·명령 큐 깊이를 낸다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Player },
            { "prefab.corpus.digest", CommandCost::Long, "<라벨> <이름> ...", "prefab identity/override 왕복 digest", CommandClass::Probe, CommandLiveness::Live },
            { "prefab.create", CommandCost::Frames, "<오브젝트 이름> <프리팹 이름>", "오브젝트로 프리팹을 만들어 저장한다", CommandClass::EditorOperation, CommandLiveness::Live },
            { "prefab.instantiate", CommandCost::Frames, "<프리팹 이름> [인스턴스 이름]", "프리팹을 씬에 소환한다", CommandClass::EditorOperation, CommandLiveness::Live },
            { "prefab.objectguid", CommandCost::Immediate, "<오브젝트 이름>", "오브젝트의 프리팹 objectGuid를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.overrides", CommandCost::Immediate, "<오브젝트>", "프리팹 인스턴스에 기록된 오버라이드를 나열한다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.status", CommandCost::Immediate, "", "프리팹 등록·캐시 상태를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.update", CommandCost::Frames, "<소스 오브젝트> <프리팹 이름>", "기존 프리팹을 소스 오브젝트로 갱신한다", CommandClass::EditorOperation, CommandLiveness::Live },
            { "profile.selftest", CommandCost::Frames, "", "CPU 프로파일러 특성화 검사(중첩·멀티스레드·프레임경계·용량초과)", CommandClass::Probe, CommandLiveness::Live },
            { "profile.stats", CommandCost::Immediate, "", "프로파일러 자체 비용과 용량 소진(교란 없음)", CommandClass::Probe, CommandLiveness::Live },
            // LC8 — 요약을 **호스트 중립으로 고쳤다.** 두 registry 가 같은 seed 를
            // 나눠 쓰므로 "에디터 종료" 는 Player 의 help 에서 거짓이 된다.
            { "quit", CommandCost::Immediate, "", "호스트를 종료한다", CommandClass::EngineService, CommandLiveness::TerminatesProcess, false, CommandRoles::Both },
            { "reflect.golden", CommandCost::Long, "[출력 경로]", "등록된 전 타입의 직렬화 출력을 골든 문서로 쓴다", CommandClass::Probe, CommandLiveness::Live },
            { "render.backend", CommandCost::Frames, "status", "부팅 시 고정된 scene/ImGui RHI 조회(변경은 Settings)", CommandClass::EngineService, CommandLiveness::Live },
            { "render.livecheck", CommandCost::Frames, "[너비 높이]", "resize·다중 뷰·표시 슬롯 회전 회귀 판정", CommandClass::Probe, CommandLiveness::Live },
            { "render.matmode", CommandCost::Frames, "<오브젝트> <opaque|transparent>", "오브젝트 재질의 렌더링 모드를 바꾼다", CommandClass::EngineService, CommandLiveness::Live },
            { "render.rtinfo", CommandCost::Frames, "", "창·뷰포트·추종 텍스처 크기를 나란히 찍는다", CommandClass::EngineService, CommandLiveness::Live },
            { "render.shadowinfo", CommandCost::Frames, "", "그림자 캐스케이드 계산 결과를 출력한다(스냅샷 검증용)", CommandClass::EngineService, CommandLiveness::Live },
            { "rhi.uploadsegments", CommandCost::Frames, "", "DX12/Vulkan 완료점 기반 업로드 세그먼트 공통 검증", CommandClass::Probe, CommandLiveness::RequiresRestart },
            { "scene.bonecache", CommandCost::Frames, "[0|1]", "E7-b A/B 토글 — 뼈 인덱스 캐시(1,기본)/매 프레임 FindBone(0)", CommandClass::Probe, CommandLiveness::Live },
            { "scene.bonedump", CommandCost::Frames, "[개수]", "대조 덤프 — 뼈 오브젝트 이름 vs 스켈레톤 뼈 이름(조회 실패 진단)", CommandClass::Probe, CommandLiveness::Live },
            { "scene.ddol", CommandCost::Frames, "<이름>", "오브젝트를 DontDestroyOnLoad로 — 씬 이송 경로 시험용", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.dirtytraversal", CommandCost::Frames, "[0|1]", "S2 A/B 토글 — dirty만 재계산(1,기본)/항상 재계산(0)", CommandClass::Probe, CommandLiveness::Live },
            { "scene.dump", CommandCost::Immediate, "[라벨]", "활성 씬의 오브젝트 계층을 로그에 남긴다", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.executiongraph", CommandCost::Frames, "probe|bench <N> [samples]", "X4 packed projection 불변식·compile 비용 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.hierarchycheck", CommandCost::Frames, "", "씬 계층의 불변식을 전수 점검한다(고아·쌍불일치·순회미도달)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.hierarchymutation", CommandCost::Frames, "probe", "X3 reparent 검증·대칭성·topology version 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.load", CommandCost::Long, "<경로>", "씬을 로드한다(활성 씬은 그대로)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.new", CommandCost::Frames, "[이름]", "빈 씬을 만들어 활성화한다(기능 테스트 씬 저작용)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.proxybench", CommandCost::Long, "<프레임수> [등록수]", "X8 정지 dirty-queue 커밋 비용(선택: 합성 등록수)", CommandClass::Probe, CommandLiveness::Live },
            { "scene.proxydirty", CommandCost::Frames, "probe", "X8 frame-persistent render dirty mask·generation 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.save", CommandCost::Frames, "<경로>", "활성 씬을 .creator로 저장한다", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.select", CommandCost::Immediate, "<오브젝트 이름>", "오브젝트를 에디터 선택으로 지정한다", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.selection", CommandCost::Immediate, "<라벨>", "단일 선택과 복수 선택을 따로 낸다(둘의 어긋남을 드러낸다)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.sparseresolver", CommandCost::Frames, "0|1|print|probe|bench <N> <frames>", "X5 dirty-root sparse resolve·A/B 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.transformbulk", CommandCost::Frames, "probe", "X7 Animator pose·Physics world batch·Socket barrier 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.transformdigest", CommandCost::Frames, "[라벨]", "활성 씬 전체의 트랜스폼 값 다이제스트(저장·재로드 대조용)", CommandClass::Probe, CommandLiveness::Live },
            { "scene.transformdomains", CommandCost::Frames, "probe", "X2 UI/Spatial 독립 dirty gate·paused/subtree 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.transformpull", CommandCost::Frames, "print|probe", "X6 C# 즉시 pull·sibling global 전파 검사", CommandClass::Probe, CommandLiveness::Live },
            { "scene.transformstats", CommandCost::Frames, "[0|1|print]", "X0 UI/Spatial·단계·구성·프레임 topology 계측", CommandClass::Probe, CommandLiveness::Live },
            { "scene.transformwritestats", CommandCost::Frames, "[0|1|print|probe]", "X1 로컬 쓰기 publish 출처 계측", CommandClass::Probe, CommandLiveness::Live },
            { "scene.traversalbench", CommandCost::Long, "<오브젝트수> <프레임수> [flat|wide|deep|skeleton]", "X0 Release 벤치(0=현재 씬)", CommandClass::Probe, CommandLiveness::Live },
            { "script.add", CommandCost::Frames, "<오브젝트> <타입>", "C# 스크립트를 오브젝트에 부착한다", CommandClass::EngineService, CommandLiveness::Live },
            { "script.fields", CommandCost::Frames, "<id>", "스크립트의 노출 필드와 현재 값을 확인한다", CommandClass::EngineService, CommandLiveness::Live },
            // ★ 이 표에서 `executesUserCode` 가 참인 **유일한 줄**이다(LC7 · §10.2).
            //
            //   cost 가 `Long` 인 것도 이 하나뿐인 성질에서 온다. 나머지 211 개의
            //   소요는 엔진이 쓴 코드가 정하지만, 이것은 **엔진이 쓰지 않은 코드**가
            //   정한다 — 표식된 메서드가 1ms 일지 10 초일지 표가 알 방법이 없다.
            //   descriptor 머리말의 규칙("틀릴 때는 비싼 쪽으로")이 정확히 이런
            //   경우를 위한 것이라, 기본을 202 로 두고 빠른 호출은 `mode:"sync"` 로
            //   명시하게 한다.
            { "script.invoke", CommandCost::Long, "<타입> <메서드> [인자]...", "표식된 static 메서드를 호출한다([EngineCallable] 없는 것은 거부)", CommandClass::EngineService, CommandLiveness::Live, true },
            { "script.reload", CommandCost::Frames, "", "게임 스크립트 어셈블리를 다시 로드한다(핫리로드)", CommandClass::EngineService, CommandLiveness::Live },
            { "script.set", CommandCost::Frames, "<id> <인덱스> <값>", "노출 필드 값을 바꾼다", CommandClass::EngineService, CommandLiveness::Live },
            { "script.status", CommandCost::Immediate, "", "CLR 상태와 활성 스크립트 수를 확인한다", CommandClass::EngineService, CommandLiveness::Live },
            { "serialize.bench", CommandCost::Long, "boot | scene <경로> [반복] | prefab <이름> [반복]", "직렬화 경로별 시간을 잰다", CommandClass::Probe, CommandLiveness::Live },
            { "serialize.nodeequal", CommandCost::Frames, "", "저작 노드 구조 비교 규칙을 판정한다(맵 키 순서 무시가 의도)", CommandClass::Probe, CommandLiveness::Live },
            { "serialize.rymlerror", CommandCost::Frames, "", "ryml 에러 정책이 abort를 막는지 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "shadermeta.probe", CommandCost::Frames, "", "ShaderMeta 실자산 수용과 잘못된 문서 거절을 함께 판정한다", CommandClass::Probe, CommandLiveness::Live },
            { "tag.authoring.probe", CommandCost::Frames, "list | <add|has|remove> <이름>", "태그·레이어 저작 자산을 읽고 고친다", CommandClass::RawFixture, CommandLiveness::Live },
            { "terrain.authoring.probe", CommandCost::Frames, "<이름> <텍스처|->", "Terrain writer 트랜잭션 회귀 검사", CommandClass::RawFixture, CommandLiveness::Live },
            { "ui.anchor", CommandCost::Frames, "<오브젝트> <minX> <minY> <maxX> <maxY>", "앵커를 직접 지정한다", CommandClass::EngineService, CommandLiveness::Live },
            { "ui.hitbox", CommandCost::Frames, "", "버튼의 rect와 클릭 판정 상자를 나란히 출력한다", CommandClass::EngineService, CommandLiveness::Live },
            { "ui.navprobe", CommandCost::Frames, "", "UI 내비게이션 저작 계층을 세워 탐색 결과를 판정한다", CommandClass::EngineService, CommandLiveness::Live },
            { "ui.rect", CommandCost::Frames, "<오브젝트|*>", "오브젝트 이하의 worldRect·sizeDelta·앵커·배율을 출력한다", CommandClass::EngineService, CommandLiveness::Live },
            { "ui.status", CommandCost::Immediate, "", "UI 계층·캔버스 연결 상태를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "undo", CommandCost::Frames, "", "에디터의 Ctrl+Z / Ctrl+Y와 같은 호출", CommandClass::EditorOperation, CommandLiveness::Live },
            { "undo.state", CommandCost::Immediate, "<라벨>", "편집 스택과 게임 스택의 Undo 깊이를 따로 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "vk.decal", CommandCost::Frames, "", "Decal 공용 패스 — GBuffer snapshot·depth-read·MRT blend 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.deferred", CommandCost::Frames, "", "Deferred 공용 패스 — GBuffer consume·fullscreen DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.fog", CommandCost::Frames, "", "VolumetricFog 공용 패스 — 3D scatter/history/composite 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.forward", CommandCost::Frames, "", "Forward+ 공용 패스 — compute·buffer·blend·mesh DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.gbuffer", CommandCost::Frames, "", "GBuffer 공용 패스 — MRT5·texture·sampler·mesh DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.gizmoicon", CommandCost::Frames, "", "실제 Camera Gizmo PNG — 2D SRV·root instance 픽셀 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.gizmoline", CommandCost::Frames, "", "GizmoLine 공용 패스 — line-list 전체 RGBA DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.grid", CommandCost::Frames, "", "그리드 패스를 Vulkan 으로 — dx12.grid 와 픽셀 대조(5d)", CommandClass::Probe, CommandLiveness::Live },
            { "vk.ibl", CommandCost::Frames, "", "IBL 생성기를 Vulkan 으로 — 면·밉·LUT DX12 픽셀 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.parallel", CommandCost::Frames, "", "Vulkan RenderGraph 병렬 command pool·제출·픽셀 검증", CommandClass::Probe, CommandLiveness::Live },
            { "vk.post", CommandCost::Frames, "", "PostChain 공용 패스 — bloom/tonemap/vignette/FXAA 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.selftest", CommandCost::Frames, "[파일]", "Vulkan 골격 자가 검증(디바이스·중립 서비스 경로·스왑체인 → PNG)", CommandClass::Probe, CommandLiveness::RequiresRestart },
            { "vk.shadow", CommandCost::Frames, "", "Shadow 공용 패스 — depth array·mesh DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.skybox", CommandCost::Frames, "", "스카이박스를 Vulkan 으로 — 큐브 SRV·정적 샘플러 픽셀 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.ssao", CommandCost::Frames, "", "SSAO 공용 패스 — depth/normal compute·filter DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.ssgi", CommandCost::Frames, "", "SSGI 공용 패스 — Hi-Z·temporal·filter·composite 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.ssr", CommandCost::Frames, "", "SSR 공용 패스 — ray hit·metal/thickness/bitmask 픽셀 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.sss", CommandCost::Frames, "", "SSS 공용 패스 — 2축 blur·depth gate 전체 픽셀 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.ui", CommandCost::Frames, "", "UI 공용 패스 — layer·blend·texture batch DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "vk.wireframe", CommandCost::Frames, "", "WireFrame 공용 패스 — non-solid fill·skinning DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live },
            { "wait", CommandCost::Immediate, "<프레임>", "지정 프레임만큼 다음 명령을 미룬다", CommandClass::EngineService, CommandLiveness::Live },
            { "window.info", CommandCost::Immediate, "", "엔진이 인식하는 클라이언트 크기를 출력한다", CommandClass::EngineService, CommandLiveness::Live },
            { "window.resize", CommandCost::Frames, "<너비> <높이>", "창 클라이언트 크기를 바꾼다(해상도 검증용)", CommandClass::EngineService, CommandLiveness::Live },
        };
    }

    // ★ 정렬을 컴파일 타임에 못박는다.
    //
    //   `FindDescriptorSeed` 는 이진 탐색이라 표가 정렬돼 있어야 한다. 그런데
    //   이 표는 손으로 유지하므로, 행 하나를 엉뚱한 자리에 끼워 넣어도 아무도
    //   안 죽는다 — `lower_bound` 가 그 근처 이름들에 대해 조용히 nullptr 를
    //   돌려주고, 그 명령은 "요약 없음"으로 등록을 거부당한다. seed 행이 분명히
    //   있는데 없다고 하는 상태가 되고, 원인을 찾는 데 오래 걸린다.
    //
    //   불변식을 지키는 비용이 0 이므로(컴파일 타임) 주석이 아니라 단정으로 둔다.
    static_assert(
        std::is_sorted(std::begin(kSeeds), std::end(kSeeds),
                       [](const DescriptorSeed& a, const DescriptorSeed& b)
                       { return std::string_view(a.name) < std::string_view(b.name); }),
        "CommandDescriptorSeeds: kSeeds must stay sorted by name (binary search)");

    const DescriptorSeed* FindDescriptorSeed(std::string_view canonical)
    {
        const auto found = std::lower_bound(
            std::begin(kSeeds), std::end(kSeeds), canonical,
            [](const DescriptorSeed& seed, std::string_view name)
            { return std::string_view(seed.name) < name; });

        if (found == std::end(kSeeds)) return nullptr;
        if (std::string_view(found->name) != canonical) return nullptr;
        return found;
    }

    std::size_t DescriptorSeedCount() noexcept
    {
        return sizeof(kSeeds) / sizeof(kSeeds[0]);
    }

    const DescriptorSeed* DescriptorSeedAt(std::size_t index) noexcept
    {
        if (index >= DescriptorSeedCount()) return nullptr;
        return &kSeeds[index];
    }
}
