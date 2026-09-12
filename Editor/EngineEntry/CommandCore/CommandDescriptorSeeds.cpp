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
            { "animator.param", CommandCost::Frames, "<오브젝트> <파라미터> <bool|float|int|trigger>", "Animator 파라미터를 저작한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,name,type", true },
            { "animator.status", CommandCost::Frames, "", "Read live Animator palettes and product publication metrics", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "assets.decodeab", CommandCost::Long, "[root] [limit]", "Compare PNG decoder bytes", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "assets.decodeabhdr", CommandCost::Long, "[root]", "Compare HDR decoder values", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "assets.modeldiag", CommandCost::Immediate, "", "모델 소비 계수를 읽는다(상태를 바꾸지 않는다)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "assets.scenemodel", CommandCost::Frames, "[reload <모델 이름>]", "활성 씬의 모델 소비가 typed generation handle로 서 있는지 본다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "assets.texturebench", CommandCost::Long, "[limit]", "Measure texture decode mip and compression stages", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "assets.unload", CommandCost::Frames, "", "사용하지 않는 에셋 캐시 정리", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "blackboard.authoring.probe", CommandCost::Frames, "<이름> [empty|noname]", "Blackboard 저장·재로드 왕복으로 키 값이 살아 돌아오는지 본다", CommandClass::RawFixture, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "bt.status", CommandCost::Immediate, "", "행동 트리 지표(트리 수·틱 누계·프레임당 경계 통과)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "camera.editor", CommandCost::Frames, "match | follow [on|off] | status", "에디터 카메라를 게임 카메라와 같은 시점으로", CommandClass::EngineService, CommandLiveness::Live },
            { "cli.drain.budget", CommandCost::Immediate, "[<시간ms> <개수>]", "서비스 큐 드레인 예산을 읽거나 바꾼다(LC5 · SLO 게이트의 변이용)", CommandClass::EngineService, CommandLiveness::Live },
            { "cli.echo.args", CommandCost::Immediate, "<인자>", "tokenizer가 만든 토큰을 길이와 함께 되비춘다(LC0 parser golden)", CommandClass::EngineService, CommandLiveness::Live },
            { "collisionmatrix.authoring.probe", CommandCost::Frames, "[escape]", "충돌 행렬 저장·재로드 왕복과 설정 루트 이탈 거부를 본다", CommandClass::RawFixture, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "commands.describe", CommandCost::Immediate, "<이름>", "명령 하나의 descriptor 상세를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "commands.list", CommandCost::Immediate, "[경로]", "등록 명령 snapshot 을 TSV 로 낸다(소스 스크래핑 대체)", CommandClass::EngineService, CommandLiveness::Live },
            { "commands.selftest", CommandCost::Immediate, "", "registry 무결성을 판정한다(이름 중복·요약 누락·descriptor 부재)", CommandClass::EngineService, CommandLiveness::Live },
            { "component.add", CommandCost::Frames, "<오브젝트 이름> <컴포넌트 타입>", "오브젝트에 컴포넌트를 붙인다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,type", true },
            { "component.list", CommandCost::Immediate, "[filter]", "등록된 컴포넌트 타입을 로그에 남긴다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "filter=" },
            { "component.remove", CommandCost::Frames, "<target> <component>", "Remove an optional component with Undo", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,component", true },
            { "crash.status", CommandCost::Immediate, "", "크래시 덤프 기록자 등록 여부와 덤프 경로를 확인한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "crash.test", CommandCost::Immediate, "[av|abort|terminate|throw]", "의도적인 프로세스 종료로 덤프 경로를 검증한다", CommandClass::Probe, CommandLiveness::TerminatesProcess, false, CommandRoles::Editor, "", false, true },
            { "dump.list", CommandCost::Immediate, "[limit]", "크래시 덤프 목록을 최대 limit개 조회한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "limit:integer=10" },
            { "dump.show", CommandCost::Immediate, "[limit]", "덤프 목록과 최신 덤프 요약을 조회한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "limit:integer=10" },
            { "dx12.forward", CommandCost::Long, "", "DX12 forward 패스를 격리 씬에서 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.forwardshade", CommandCost::Long, "", "DX12 forward 셰이딩 결과를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.gbuffer", CommandCost::Frames, "", "GBuffer 패스 검증(입력조립·MRT5·깊이·그래프 배리어)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.gizmoscene", CommandCost::Frames, "", "Gizmo 씬 연결 검증(밀봉 복사·4패스 체인·타깃 공유)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.live", CommandCost::Immediate, "on|status", "EnhancedRenderer 메인 런타임 상태", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "action=status" },
            { "dx12.post", CommandCost::Frames, "", "DX12 후처리 패스를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.scene", CommandCost::Long, "", "씬 연결 검증(카메라 스냅샷·메시 업로드·실제 드로우)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.selftest", CommandCost::Frames, "<texture-path> [output]", "DX12 브링업 자가 검증(삼각형 렌더 → PNG)", CommandClass::Probe, CommandLiveness::RequiresRestart, false, CommandRoles::Editor, "", false, true },
            { "dx12.shadowquality", CommandCost::Frames, "", "그림자 품질 검증(경사 비례 편향·캐스케이드 경계 블렌딩 A/B)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.skinning", CommandCost::Frames, "", "GBuffer 스키닝 검증(본 이동·가중 혼합·비스킨드 불변)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "dx12.ui", CommandCost::Frames, "", "DX12 UI 패스를 리드백으로 판정한다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "editor.dock", CommandCost::Immediate, "", "살아 있는 도크 노드 트리를 TSV로 내고 배치 고아를 판정한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "editor.layout", CommandCost::Immediate, "", "활성 레이아웃과 imgui.ini 항목을 선언 표와 맞대 본다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "editor.menu", CommandCost::Immediate, "", "선언된 에디터 메뉴 표를 TSV로 내고 배선 충돌을 판정한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "editor.selftest", CommandCost::Immediate, "", "editor:: 선언 배선 자가 검사(창 표·메뉴 표)를 돌린다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "editor.theme", CommandCost::Immediate, "", "적용된 ImGui 스타일 값을 TSV로 내고 배율 출처를 판정한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "editor.windows", CommandCost::Immediate, "", "선언된 에디터 창 표를 TSV로 내고 배선 고아를 판정한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "experiment.cooked", CommandCost::Long, "[경로]", "쿠킹 포맷 왕복 무손실·거부 동작(경로를 주면 실자산 왕복까지)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "foliage.authoring.probe", CommandCost::Frames, "<이름> [escape]", "Foliage 저작 트랜잭션 왕복과 루트 이탈 거부를 본다", CommandClass::RawFixture, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "game.pak", CommandCost::Long, "", "Release Player 패키지를 빌드·검증 후 Build/Staging에 게시한다", CommandClass::EngineService, CommandLiveness::Live },
            { "gc.collect", CommandCost::Frames, "", "관리 힙 확정 수집(씬 전환이 자동으로 부르는 그 경로)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "gc.stats", CommandCost::Immediate, "", "관리 힙 지표를 낸다(gc.delta는 직전 대비 증감)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "label=" },
            { "gpu.baseline", CommandCost::Frames, "", "현재 상태를 기준선으로 삼는다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            // LC8 — 두 호스트가 같은 뜻으로 갖는 둘 중 하나다. 요약은 이미 호스트 중립이다.
            { "gpu.census", CommandCost::Frames, "[라벨]", "VRAM과 엔진 에셋 수를 로그에 기록", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "label=" },
            { "help", CommandCost::Immediate, "[명령]", "명령 목록 또는 명령 하나의 상세를 낸다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Both },
            { "inputmap.authoring.probe", CommandCost::Frames, "<save|verify> <이름>", "입력 액션맵 저장·재기동 왕복으로 payload 복원을 본다", CommandClass::RawFixture, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "inputmap.corpus.probe", CommandCost::Long, "", "입력 액션맵 코퍼스를 전수로 읽어 계수를 낸다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "lifecycle.dump", CommandCost::Immediate, "[파일]", "기록을 TSV로 쓴다(기록 0건이면 실패로 끝난다)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "path=lifecycle_trace.tsv" },
            { "lifecycle.registry", CommandCost::Frames, "", "생명주기 등록 수와 대기 중인 초기화 수를 조회한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "lifecycle.trace", CommandCost::Frames, "on [틱프레임]|off|clear|status", "생명주기 호출 순서를 받아 적는다", CommandClass::EngineService, CommandLiveness::Live },
            { "light.proxy", CommandCost::Frames, "", "Read live light proxy values and publication counters", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()", false, false },
            { "log.flush", CommandCost::Immediate, "", "로그를 디스크에 즉시 반영", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "material.corpus.probe", CommandCost::Long, "<이름> ...", "standalone material identity/reference 왕복", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "mem.delta", CommandCost::Immediate, "[라벨]", "기준선 대비 CRT 블록·바이트 증감(기준선이 없으면 지금을 기준선으로 삼는다)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "label=" },
            { "mem.hook", CommandCost::Immediate, "on|stack|off|top|status", "CRT 할당 훅 — 호출 계수, stack 은 귀속까지(디버그 CRT 전용)", CommandClass::EngineService, CommandLiveness::Live },
            { "mem.reset", CommandCost::Immediate, "", "churn 누계와 기준선을 0으로 — 구간 측정용", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "mem.stats", CommandCost::Immediate, "[라벨]", "CRT 현재 live 블록·바이트를 찍는다(누계는 mem.hook 이 따로 센다)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "label=" },
            { "model.async", CommandCost::Long, "<path>|status|wait|probe <guard-path>", "Incremental model placement and cancellation checks", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "model.load", CommandCost::Long, "<경로>", "모델을 에셋으로 임포트한다(fbx/gltf/glb/obj)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "path" },
            { "model.loadcached", CommandCost::Long, "<모델 경로>", "에디터 드롭 경로(LoadCachedModelShared)로 모델을 연다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "path" },
            { "model.place", CommandCost::Frames, "<이름>", "임포트한 모델을 활성 씬에 배치한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "model", true },
            { "object.create", CommandCost::Frames, "<이름> [타입]", "빈 오브젝트를 만든다(Empty/Light/Camera/Mesh)", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "name,type=Empty", true },
            { "object.delete", CommandCost::Frames, "<target>", "Delete an object subtree with Undo", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target", true },
            { "object.describe", CommandCost::Immediate, "<name-or-id>", "Read object identity and name", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "target", false },
            { "object.duplicate", CommandCost::Frames, "<오브젝트> [새 이름]", "오브젝트를 복제한다(에디터 Ctrl+D와 같은 원시 함수)", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,name=", true },
            { "object.parent", CommandCost::Frames, "<자식> <부모 | ->", "오브젝트의 부모를 바꾼다(-는 씬 루트로 올린다)", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,parent", true },
            { "object.properties", CommandCost::Immediate, "<target> <component>", "Read reflected component fields and values", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "target,component", false },
            { "object.property", CommandCost::Frames, "<오브젝트> <컴포넌트> <필드> <값>", "리플렉션으로 프로퍼티를 설정한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,component,field,value:value", true },
            { "object.rename", CommandCost::Immediate, "<name-or-id> <new-name>", "Rename through the shared editor undo transaction", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,name", true },
            { "object.rootref", CommandCost::Frames, "<오브젝트> [루트|-]", "Bone형 same-scene root 참조를 설정/조회한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "object.transform", CommandCost::Frames, "<이름> <px py pz> [rx ry rz] [sx sy sz]", "변환을 지정한다(회전은 도)", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,position:vec3,rotation:vec3=0 0 0,scale:vec3=1 1 1", true },
            { "pipeline.nodes", CommandCost::Immediate, "", "라이브 파이프라인의 노드 조립 결과를 한 줄씩 낸다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "pix.capture", CommandCost::Frames, "begin|end|status", "PIX 주입 실행의 명시적 GPU 캡처 경계", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "action=status" },
            { "play", CommandCost::Frames, "", "에디터의 재생·정지와 같은 동작", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
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
            { "prefab.corpus.digest", CommandCost::Long, "<라벨> <이름> ...", "prefab identity/override 왕복 digest", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "prefab.create", CommandCost::Frames, "<오브젝트 이름> <프리팹 이름>", "오브젝트로 프리팹을 만들어 저장한다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.instantiate", CommandCost::Frames, "<프리팹 이름> [인스턴스 이름]", "프리팹을 씬에 소환한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "prefab,name=", true },
            { "prefab.objectguid", CommandCost::Immediate, "<오브젝트 이름>", "오브젝트의 프리팹 objectGuid를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.overrides", CommandCost::Immediate, "<오브젝트>", "프리팹 인스턴스에 기록된 오버라이드를 나열한다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.status", CommandCost::Immediate, "", "프리팹 등록·캐시 상태를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "prefab.update", CommandCost::Frames, "<소스 오브젝트> <프리팹 이름>", "기존 프리팹을 소스 오브젝트로 갱신한다", CommandClass::EngineService, CommandLiveness::Live },
            { "profile.selftest", CommandCost::Frames, "", "CPU 프로파일러 특성화 검사(중첩·멀티스레드·프레임경계·용량초과)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "profile.stats", CommandCost::Immediate, "", "프로파일러 자체 비용과 용량 소진(교란 없음)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            // LC8 — 요약을 **호스트 중립으로 고쳤다.** 두 registry 가 같은 seed 를
            // 나눠 쓰므로 "에디터 종료" 는 Player 의 help 에서 거짓이 된다.
            { "quit", CommandCost::Immediate, "", "호스트를 종료한다", CommandClass::EngineService, CommandLiveness::TerminatesProcess, false, CommandRoles::Both },
            { "render.backend", CommandCost::Immediate, "status", "부팅 시 고정된 scene/ImGui RHI 조회(변경은 Settings)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "action=status" },
            { "render.livecheck", CommandCost::Immediate, "[너비 높이]", "resize·다중 뷰·표시 슬롯 회전 회귀 판정", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.matmode", CommandCost::Immediate, "<오브젝트> <opaque|transparent>", "오브젝트 재질의 렌더링 모드를 바꾼다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,mode", true },
            { "render.pbr.capture", CommandCost::Long, "<new-absolute-directory> [game|editor]", "PBR capture verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.coverage", CommandCost::Long, "", "PBR coverage verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.emission", CommandCost::Long, "", "PBR emission verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.mip", CommandCost::Long, "", "PBR mip verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.occlusion", CommandCost::Long, "", "PBR occlusion verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.parity", CommandCost::Long, "", "PBR parity verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.transform", CommandCost::Long, "", "PBR transform verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.pbr.uv", CommandCost::Long, "", "PBR uv verification", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "render.rtinfo", CommandCost::Immediate, "", "창·뷰포트·추종 텍스처 크기를 나란히 찍는다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "render.shadowinfo", CommandCost::Immediate, "", "그림자 캐스케이드 계산 결과를 출력한다(스냅샷 검증용)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "scene.bonedump", CommandCost::Frames, "[개수]", "대조 덤프 — 뼈 오브젝트 이름 vs 스켈레톤 뼈 이름(조회 실패 진단)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.ddol", CommandCost::Frames, "<이름>", "오브젝트를 DontDestroyOnLoad로 — 씬 이송 경로 시험용", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.dump", CommandCost::Immediate, "[라벨]", "활성 씬의 오브젝트 계층을 로그에 남긴다", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.flag", CommandCost::Immediate, "[<dirtytraversal|bonecache> [0|1]]", "씬 진단 플래그를 읽거나 바꾼다(인자 없으면 전부 조회)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.hierarchycheck", CommandCost::Frames, "", "씬 계층의 불변식을 전수 점검한다(고아·쌍불일치·순회미도달)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "scene.load", CommandCost::Long, "<경로>", "씬을 로드한다(활성 씬은 그대로)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.new", CommandCost::Frames, "[이름]", "빈 씬을 만들어 활성화한다(기능 테스트 씬 저작용)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.save", CommandCost::Frames, "<경로>", "활성 씬을 .creator로 저장한다", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.select", CommandCost::Immediate, "<오브젝트 이름>", "오브젝트를 에디터 선택으로 지정한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target", true },
            { "scene.selection", CommandCost::Immediate, "<라벨>", "단일 선택과 복수 선택을 따로 낸다(둘의 어긋남을 드러낸다)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.sparseresolver", CommandCost::Frames, "0|1|print", "X5 dirty-root sparse resolve·A/B 검사", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.transformdigest", CommandCost::Frames, "[라벨]", "활성 씬 전체의 트랜스폼 값 다이제스트(저장·재로드 대조용)", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.transformpull", CommandCost::Frames, "[print]", "X6 C# 즉시 pull 계측 스냅샷을 조회한다", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.transformstats", CommandCost::Frames, "[0|1|print]", "X0 UI/Spatial·단계·구성·프레임 topology 계측", CommandClass::EngineService, CommandLiveness::Live },
            { "scene.transformwritestats", CommandCost::Frames, "[0|1|print]", "X1 로컬 쓰기 publish 출처 계측", CommandClass::EngineService, CommandLiveness::Live },
            { "script.add", CommandCost::Frames, "<오브젝트> <타입>", "C# 스크립트를 오브젝트에 부착한다", CommandClass::EngineService, CommandLiveness::Live },
            { "script.fields", CommandCost::Frames, "<id>", "스크립트의 노출 필드와 현재 값을 확인한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "instance:integer" },
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
            { "script.set", CommandCost::Frames, "<id> <인덱스> <값>", "노출 필드 값을 바꾼다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "instance:integer,index:integer,value" },
            { "script.status", CommandCost::Immediate, "", "CLR 상태와 활성 스크립트 수를 확인한다", CommandClass::EngineService, CommandLiveness::Live },
            { "shadermeta.probe", CommandCost::Frames, "", "ShaderMeta 실자산 수용과 잘못된 문서 거절을 함께 판정한다", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "tag.add", CommandCost::Immediate, "<name>", "Add and persist a project tag with Undo", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "name", true },
            { "tag.has", CommandCost::Immediate, "<name>", "Query a project tag", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "name" },
            { "tag.list", CommandCost::Immediate, "", "Read project tags and layers", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "tag.remove", CommandCost::Immediate, "<name>", "Remove and persist a project tag with Undo", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "name", true },
            { "terrain.authoring.probe", CommandCost::Frames, "<이름> <텍스처|->", "Terrain writer 트랜잭션 회귀 검사", CommandClass::RawFixture, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "ui.anchor", CommandCost::Frames, "<오브젝트> <minX> <minY> <maxX> <maxY>", "앵커를 직접 지정한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,minX:number,minY:number,maxX:number,maxY:number", true },
            { "ui.hitbox", CommandCost::Frames, "", "버튼의 rect와 클릭 판정 상자를 나란히 출력한다", CommandClass::EngineService, CommandLiveness::Live },
            { "ui.navprobe", CommandCost::Frames, "", "UI 내비게이션 저작 계층을 세워 탐색 결과를 판정한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "ui.pos", CommandCost::Frames, "<target> <x> <y>", "UI anchored position을 편집한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,x:number,y:number", true },
            { "ui.rect", CommandCost::Frames, "<오브젝트|*>", "오브젝트 이하의 worldRect·sizeDelta·앵커·배율을 출력한다", CommandClass::EngineService, CommandLiveness::Live },
            { "ui.screenpos", CommandCost::Frames, "<target> <x> <y>", "UI 화면 위치를 편집한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,x:number,y:number", true },
            { "ui.size", CommandCost::Frames, "<target> <x> <y>", "UI 크기를 편집한다", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "target,x:number,y:number", true },
            { "ui.status", CommandCost::Immediate, "", "UI 계층·캔버스 연결 상태를 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "undo", CommandCost::Frames, "", "에디터의 Ctrl+Z / Ctrl+Y와 같은 호출", CommandClass::EditorOperation, CommandLiveness::Live, false, CommandRoles::Editor, "()", true },
            { "undo.state", CommandCost::Immediate, "<라벨>", "편집 스택과 게임 스택의 Undo 깊이를 따로 낸다", CommandClass::EngineService, CommandLiveness::Live },
            { "vk.decal", CommandCost::Long, "", "Decal 공용 패스 — GBuffer snapshot·depth-read·MRT blend 대조", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "vk.deferred", CommandCost::Frames, "", "Deferred 공용 패스 — GBuffer consume·fullscreen DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "vk.forward", CommandCost::Frames, "", "Forward+ 공용 패스 — compute·buffer·blend·mesh DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "vk.gbuffer", CommandCost::Long, "", "GBuffer 공용 패스 — MRT5·texture·sampler·mesh DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "vk.grid", CommandCost::Frames, "", "그리드 패스를 Vulkan 으로 — dx12.grid 와 픽셀 대조(5d)", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "vk.shadow", CommandCost::Frames, "", "Shadow 공용 패스 — depth array·mesh DX12/Vulkan 대조", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "vk.texturecodec", CommandCost::Frames, "", "Validate neutral texture bytes across DX12 and Vulkan", CommandClass::Probe, CommandLiveness::Live, false, CommandRoles::Editor, "", false, true },
            { "wait", CommandCost::Immediate, "<프레임>", "지정 프레임만큼 다음 명령을 미룬다", CommandClass::EngineService, CommandLiveness::Live },
            { "window.info", CommandCost::Immediate, "", "엔진이 인식하는 클라이언트 크기를 출력한다", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "()" },
            { "window.resize", CommandCost::Frames, "<너비> <높이>", "창 클라이언트 크기를 바꾼다(해상도 검증용)", CommandClass::EngineService, CommandLiveness::Live, false, CommandRoles::Editor, "width:integer,height:integer" },
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
