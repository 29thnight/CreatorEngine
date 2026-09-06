# Phase 14.5 종결 검토 — 2026-09-06

상태: **구현·자동 계약 검증 완료. 전체 페이즈 종결은 보류**. 남은 GUI 수동 확인과 canonical 성능 회귀를 자동 게이트 통과로 대체하지 않는다.

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
- GUI 수동 조작 전체는 별도 확인이 필요하다. material mode의 실제 재질 8개 및 HTTP Undo/Redo 복원은 이번에 검증했다. 자동 게이트는 공통 편집 경로와 실제 상태 복원을 검사하며, 모든 GUI 위젯 조작을 대신했다고 주장하지 않는다.
- 전체 `run-all.ps1` 및 canonical 성능 예산을 완료했다고 표시하지 않는다. Phase 14.5 서비스 지연과 도메인 렌더링 성능은 별개다.

## 종결 판단

이번에 남아 있던 제품 결과 이행, 폐기 하네스·어댑터 제거, 13개 한국어 판정 소비자,
6개 명령 소스 존재 소비자 및 도표 불일치는 정리했다. 구현 잔량과 검증 잔량을 구분한다.
현재 문서·대시보드는 GUI 수동 조작과 canonical 성능 회귀가 미확인인 상태를 남긴다.
도메인별 코퍼스 누락·기존 렌더 검사 실패를 완료로 바꾸거나 실패 항목을 삭제하지 않는다.
