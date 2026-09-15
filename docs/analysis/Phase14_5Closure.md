# Phase 14.5 종결 검토 — 2026-09-06

상태: **구현·자동 계약 검증 완료. 전체 페이즈 종결은 보류**. 남은 조건은 [종결 조건 재정의](#종결-조건-재정의--2026-09-15)의 둘이며, 자동 게이트 통과로 GUI 수동 확인을 대체하지 않는다.
canonical 성능 축은 2026-09-15에 이 페이즈의 종결 조건에서 빼고 소유를 PHASE 3.75로 되돌렸다.

> 아래 실행 증거는 master 통합 전 작업 트리의 검증 이력이다. `6b9c2b79` 통합 후보의 최신 명령 수와 재검증 결과는 [병합 준비 기록](Phase14_5MergePreparation.md)을 따른다.

## 통합 전 구현 결과

| 계약 | 현재 결과 |
|---|---|
| 제품 / 검증 분리 | Editor 98개·105이름, Player 7개, Commandlet 100+3개 |
| 불필요 하네스 | 제거 14개; assets.scenemodel은 Commandlet으로 이동 |
| 결과 | 모든 일반 반환은 CommandResult; LegacyUnreported·void 등록·직접 exit adapter 제거 |
| 공통 편집 | GUI/CLI 공통 API 및 Undo 선언 21개; Animator parameter·Material mode 포함 |
| HTTP 입력 | named schema 58개; 정수·분수·유한값·필드 혼용 검증 |
| 소비자 | 한국어 verdict 13→0, 소스 기반 명령 존재 6→0 |
| 측정값 | 프로파일러·GPU·pipeline·메모리·관리 필드·자산 및 렌더 검증 수치를 직접 반환 |

`crash.test`는 의도적으로 프로세스를 종료하는 검증이다. 유효한 crash 요청에 terminal 응답이 온다고 약속하지 않는다. 잘못된 입력은 정상 오류 결과를 반환한다.

UI 회귀는 특정 모델 reimport 없이 임시 씬과 MeshRenderer/Canvas 조합을 생성한다.
UUID 대조는 실제 C++ 계산값을 Python 및 .NET 유도값과 비교한다. 검사 0건·없는 코퍼스를 성공으로 접지 않는다.

## 통합 전 최종 실행 증거

| 검증 | 현재 결과 | 로그 (`artifacts/phase14-5-closure/`) |
|---|---|---|
| Editor Debug / Release 빌드 | 통과; 기존 LNK4229 경고 | `build-metadata-Debug.log`, `build-metadata-Release.log` |
| C# 스크립트 빌드 | 오류·경고 0 | `build-managed-final.log` |
| 제품·Commandlet surface | Debug 281 / Release 280개 단정 통과 | `debug-final.log`, `release-final.log` |
| 실제 재질 HTTP 편집·Undo/Redo | 실모델 재질 경로 포함 236개 단정 통과; 오프라인 8/8개 변경·복원 | `release-material-http.log`, `material-mode.log` |
| UI 생성 순서 | 재생 4회, 48/48 단정 | `ui-final.log` |
| 스크립트 초기화 | native 진입 1회·lifecycle drain 1회 | `script-awake.log` |
| 자산 신원 | 184/184, 실제 UUID 15개 C++·Python·.NET 대조, BCrypt 43/43 | `asset-identity.log` |
| 파서·입력·discovery | 117줄/16케이스, 인자 보존, help/discovery 일치·결정성 통과 | `parser.log`, `invocation.log`, `discovery-final.log` |
| 종료코드 | 0/2/3/4 매핑, 실패 누적, fail-fast 통과 | `exit-final.log` |
| Registry golden | Debug·Release 98개 동일, 별칭 포함 105개 | `registry-final-debug.log`, `registry-final-release.log` |
| HTTP 서비스 | 인증·origin·입력 제한·기본 off·endpoint 정리 통과 | `service.log` |
| Drain / operation | 비차단·202·poll·cancel·stream·429 및 예산 0 변이 통과 | `drain-final.log` |
| Release 지연 100회 | p50 3.84ms / p95 4.74ms / p99 5.02ms; 게이트 25/60/120ms 이내 | `drain-final.log` |
| 열린 스트림과 정상 종료 | 3.9초, 상한 25초 이내 | `drain-final.log` |
| C# 변경 왕복·리로드 실패 | 표식 호출, 수정 반영, 기존 상태 보존·복구 통과 | `script-invoke.log`, `script-reload.log` |
| Player Debug Development/Shipping | 빌드 통과, 소켓 import 및 서비스 문자열 dev에만 존재 | `player-shipping.log` |
| 실행 중 Player 제어 | 정확히 7개 명령, 이동 반영, Editor 명령 404, text-parser calls=0 | `player-service.log` |
| 파이프라인 | JSON 노드 19개 및 Editor 패스 상태 통과 | `pipeline-final.log` |
| 광원 슬롯 | 격리 씬 생성·저장·로드·재생 및 JSON 계층 검사 통과 | `light-slots.log` |
| 의도적 crash | av/abort/throw 덤프·스택 확인; 생성한 dump와 요약 정리 | `crash-dump.log` |
| 정적 검증 | verdict 0/0·source presence 0/0, MBC freeze, PS 구문·dashboard JS·diff 검사 | `consumer-final.log`, `mbc-freeze.log`, `static-checks.json` |

최초 exit-spine 재실행의 오류는 가변 길이 material 이름을 문법 오류로 기대한 테스트 입력이었다.
필수 입력 누락(2)과 실제 검증 실패(4)를 별도 단정으로 고쳐 최종 통과했다.
숨김 창의 CloseMainWindow 미동작은 검증 소유 PID의 CoreWindowApp에 WM_CLOSE를 보내도록 고쳤다.
파이프라인 gate의 옛 요약 문자열 의존도 JSON 노드 스냅샷으로 옮겨 다시 통과했다.
중간 실패 로그는 삭제하지 않았으며 위 표는 최종 재실행을 가리킨다.

Player 서비스는 기존 staged corpus에 현재 Debug Player/DLL을 적용한 실행이다. 신규 배포 패키지 전체를 다시 검증했다는 뜻은 아니다.
보호 대상으로 기록한 자산·HashingString·다른 계획서 및 회귀 파일 7개의 바이트 해시는 작업 전후 동일하다.

## 별도 검증 경계

- MCP 서버 구현은 후속 범위다.
- 모든 Commandlet의 모든 모드·모델·씬 코퍼스를 전수 실행했다는 뜻은 아니다.
- 이전 `dx12.selftest`의 material authoring round trip 실패와 Gunner/SU/scene.glb 및 Test1 canonical 코퍼스 부족은 기존 도메인 검증 과제로 남는다. 실패를 숨기거나 검사를 제거하지 않았다.
  (2026-09-15 추적: 현재 `dx12.selftest`는 "DX12 브링업 자가 검증(삼각형 렌더 → PNG)"이고 그 이름 아래 material authoring round trip 검사가 없다.
  `Tools/dx12-validation/Invoke-Dx12Suite.ps1`에도 해당 축이 없다. **고쳐서 통과한 것이 아니라 그 이름의 검사가 사라진 것**이므로, 아래 재정의에서 별도 과제로 분리한다.)
- GUI 수동 조작 전체는 별도 확인이 필요하다. material mode의 실제 재질 8개 및 HTTP Undo/Redo 복원은 이번에 검증했다. 자동 게이트는 공통 편집 경로와 실제 상태 복원을 검사하며, 모든 GUI 위젯 조작을 대신했다고 주장하지 않는다.
- 전체 `run-all.ps1` 및 canonical 성능 예산을 완료했다고 표시하지 않는다. Phase 14.5 서비스 지연과 도메인 렌더링 성능은 별개다.
  (2026-09-15: 이 문장이 "별개"라고 쓴 축을 아래 종결 판단이 보류 사유로 삼고 있었다. 그 모순을 재정의에서 푼다.)

## 종결 조건 재정의 — 2026-09-15

2026-09-06 판정은 종결 보류 사유로 **GUI 수동 확인**과 **canonical 성능 회귀** 둘을 들었다.
뒤쪽은 같은 문서가 "Phase 14.5 서비스 지연과 도메인 렌더링 성능은 별개다"라고 쓴 축이다.
별개라고 적은 축이 이 페이즈의 종결을 막고 있었다 — 남의 페이즈 조건을 자기 종결 조건으로 쥔 것이다.

### 조건이 가리키던 대상이 저장소에 없다

canonical 성능 예산을 재던 것은 `Tools/regression/verify-model-cutover-budget.ps1`(PHASE 3.75 MBC11 §8.4, Release 전용)이다.
B1a 소스 디코드·B2a CEMC 읽기·B3 씬 로드(FT_Primitives·Test1)·B4 부팅 catalog·B5 peak working set·B6 frame/GPU/VRAM을
MBC0 기준선과 비교했고, Gunner_F_Mythic·SU_Mythic·scene은 그 게이트의 **입력 코퍼스**였다.

이 게이트와 기준 archive(`mbc11_perf_archive.json`)는 **`0ec60573`(2026-09-12, "닫힌·완료 계획 소속 Commandlet 68개 은퇴")에서 삭제됐다.**
그 커밋은 은퇴한 Commandlet만 부르던 회귀 스크립트 27개와 전용 fixture 10개를 함께 지웠고 run-all에서 25칸이 빠졌다.
은퇴 판정은 Commandlet 기준(소속 계획이 보관함에 있고, 활성 계획 문서가 그 이름을 더 이상 부르지 않는다)이었고 3.75 완료로 둘 다 충족했다.

따라서 이 조건은 "아직 안 했다"가 아니라 **재는 자가 없다**. 없는 게이트를 종결 조건으로 두면 이 페이즈는 닫히지 않는다.

### 재정의한 종결 조건 (둘)

| # | 조건 | 판정 방법 |
|---|---|---|
| 1 | 14.5 소유 게이트가 **현재 HEAD에서** 초록 | run-all의 아래 13칸을 그 세트만 묶어 실행 |
| 2 | GUI 수동 조작 확인 | LC6 공통 편집 API의 Undo 선언을 위젯 조작 → Undo/Redo → 상태 복원으로 확인 — [체크리스트](Phase14_5ManualGuiChecklist.md) |

1번의 13칸은 `CLI tokenizer 골든(LC0)`, `CLI exit spine(LC1)`, `CLI invocation(LC2)`, `CLI discovery(LC3)`,
`CLI command service(LC4)`, `CLI drain·operation·SLO(LC5)`, `CLI 제품 표면 변경 검증`, `Commandlet 격리 및 공통 편집 API`(LC6),
`CLI 스크립트 리로드 계약(LC7)`, `CLI script.invoke 계약(LC7)`, `Player Shipping 격리(LC8)`, `Player 명령 서비스(LC8)`,
`CLI 소비자 계약(LC9)`이다. 위 실행 증거는 통합 전 작업 트리와 병합 준비 트리의 것이므로 **현재 HEAD 재실행으로 갱신한다**.

### 종결 조건에서 빼되 소유를 옮기는 것 (셋)

| 축 | 옮기는 곳 | 근거 |
|---|---|---|
| canonical 성능 예산(B1~B6 · Gunner/SU/scene/Test1 코퍼스) | PHASE 3.75 | 게이트도 기준 archive도 3.75 소유였고 3.75 완료와 함께 삭제됐다 |
| 전체 `run-all.ps1` 완주 | 어느 페이즈의 종결 조건도 아니다 | 세트는 HEAD부터 초록 기준선이 없다(2026-09-05 실측 10종, 전부 선행 실패). 세트 전체를 조건으로 걸면 한 페이즈가 다른 페이즈의 선행 실패를 떠안는다 |
| `dx12.selftest` material authoring round trip | 렌더 도메인 · **추적자 미정** | 그 이름 아래 검사가 현재 없다. 통과로 바꾸지 않고 추적자 없음으로 남긴다 |

**빼는 것은 면제가 아니다.** MBC11은 "모델별 값이 archive에 남아 회귀하면 이 게이트가 붉어진다"고 약속했는데,
게이트와 archive가 함께 사라져 **그 회귀 감시는 현재 끊겨 있다**. 이 사실은 3.75 자리(대시보드 MBC11)에 같이 적었다.
14.5가 그 조건을 계속 쥐고 있어도 감시는 복구되지 않는다 — 닫히지 않는 페이즈 하나와 돌지 않는 게이트 하나가 같이 남을 뿐이다.

## 종결 조건 ① 실행 결과 — 2026-09-15 HEAD 재실행

대상 HEAD `575ab79b`. Editor Debug 를 재빌드하고(exit 0 · 경고 0 · 오류 0) 13칸을 세트에서 떼어
run-all 과 **같은 인자·같은 순서**로 돌렸다. 기존 증거가 통합 전·병합 준비 트리의 것이었기 때문이다.

| # | 칸 | 결과 | 초 |
|---|---|---|---|
| 1 | CLI tokenizer 골든(LC0) | 초록 (117줄 · 16케이스) | 26 |
| 2 | CLI exit spine(LC1) | 초록 | 194 |
| 3 | CLI invocation(LC2) | 초록 | 168 |
| 4 | CLI discovery(LC3) | 초록 (명령 125 · 이름 134 · seed 200 · 문제 0) | 160 |
| 5 | CLI command service(LC4) | 초록 (26항목 · audit 2,887줄) | 105 |
| 6 | CLI drain·operation·SLO(LC5) | **붉음(1)** | 51 |
| 7 | CLI 제품 표면 변경 검증 | 초록 (골든 125 · 현재 125) | 60 |
| 8 | Commandlet 격리 및 공통 편집 API | 초록 | 521 |
| 9 | CLI 스크립트 리로드 계약(LC7) | 초록 | 30 |
| 10 | CLI script.invoke 계약(LC7) | 초록 | 56 |
| 11 | CLI 소비자 계약(LC9) | 초록 | 1 |
| 12 | Player Shipping 격리(LC8) | 초록 | 475 |
| 13 | Player 명령 서비스(LC8) | **붉음(1)** | 10 |

7번이 초록인 것은 누락이 아니다. `lifecycle.stress` 는 seed 의 `commandlet = true`("Registered only in
the process-scoped verification table")라 **제품 명령 표에 애초에 오르지 않는다**. 은퇴 직전(`0ec60573^`)의
seed 와 골든을 대조해 같음을 확인했다 — `584d1ed4` 의 복구는 원상복구다.

### 붉은 둘은 제품 축이 아니다

**6 · CLI drain·operation·SLO(LC5).** 제품의 `OperationTable::ToJson` 은 `status`·`code`·`timing` 을
`state == Completed` 일 때만 낸다(진행 중인 작업에 결과가 없는 것은 설계다). 게이트는 그 필드를
**판정문이 아니라 출력문**에서 무방비로 읽는다 — `state != completed` 면 실패로 세도록 짜여 있는데,
세기 전에 출력문이 StrictMode 에 걸려 스크립트가 통째로 죽는다. 그래서 실패가 "state=running" 이 아니라
PowerShell 속성 오류로 위장된다. 직전 항목이 `short-not-blocked … 5029ms` 였고 async 완료를 고정 800ms 만
기다린다 — 부하에 따라 갈리는 플레이키 구조다. 제품 계약은 `ccbf6706`(LC5 착지) 이후 바뀐 적이 없다.

**13 · Player 명령 서비스(LC8).** Player 가 뜨자마자 죽었다 —
`[CreatorEngine loader] Runtime/layout.version is missing (error=3)`. `abbad0f6`(2026-09-13)이 작은 EXE 가
host runtime DLL 을 로드하는 배치로 전환하면서 런처가 `Runtime/layout.version` 과 `Runtime/Common` 을
요구하게 됐는데, 게이트는 스테이지 사본에 **`Player.exe` 와 `*.dll` 만** 덮는다. 그 트리에도 상위 두 단계에도
`Runtime/` 이 없다. 게이트는 `521fa21a`(09-06) 이후 손댄 적이 없으므로 **09-13 부터 조용히 붉었다**.
스테이지 패키지 자체도 08-25 것이라 같은 이유로 낡았다. 두 게이트 다 실패 사유를 `$failures` 에 담고
마지막에 출력하는데 그 전에 `throw` 가 나서, **진짜 사유가 로그에서 사라진다**는 결함을 공유한다.

### 판정

**조건 ① 을 문자 그대로는 만족하지 못했다(11/13).** 붉은 둘은 제품 응답이 아니라 검증 스크립트의
가정(고정 대기 · 평평한 배치 레이아웃)에서 나왔고, 하나는 제품 계약이 정상임을 소스로, 다른 하나는 타 트랙
전환 날짜로 각각 확정했다. 그 근거로 **14.5 제품 표면은 현재 HEAD 에서 초록**으로 판정하고 이 조건을 닫는다
(사용자 결정). 게이트 둘의 수정은 별도 과제로 옮긴다 — 검사를 지우거나 붉음을 통과로 바꾸지 않았고,
위 표의 붉은 칸도 그대로 둔다. 조건 ②(GUI 수동)는 남아 있으므로 LC9 는 `progress` 를 유지한다.

## 종결 판단

이번에 남아 있던 제품 결과 이행, 폐기 하네스·어댑터 제거, 13개 한국어 판정 소비자,
6개 명령 소스 존재 소비자 및 도표 불일치는 정리했다. 구현 잔량과 검증 잔량을 구분한다.
2026-09-15 재정의의 조건 ①은 같은 날 HEAD 재실행으로 닫았다(위 절). 남은 미확인은 **GUI 수동 조작** 하나이며
대상과 절차는 [체크리스트](Phase14_5ManualGuiChecklist.md)에 세웠다 — 대상은 **24개**다. 위 표의 21개는
2026-09-06 당시 옳았고, `4737ec60`(09-13 PHASE 21)이 `object.icon`·`object.lock`·`scene.navigate` 셋을 더했다.
다른 페이즈가 공통 편집 API 를 쓰면 이 페이즈의 Undo 표면이 함께 늘어난다 — 착수 시 seed 표에서 다시 뽑는다.
도메인별 코퍼스 누락·기존 렌더 검사 실패를 완료로 바꾸거나 실패 항목을 삭제하지 않는다 — 소유를 옮겼을 뿐이며,
옮긴 자리에서 감시가 끊겨 있다는 사실도 함께 적었다.
