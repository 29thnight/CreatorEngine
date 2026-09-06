# Phase 14.5 master 병합 준비 — 2026-09-06

최신 `origin/master`의 `6b9c2b792860b5f8bb408956990789f3a2d2b1d2`를 별도 통합 브랜치에서 합쳐 검증했다. **충돌 해결 및 병합 준비 완료**다.
원본 `phase14.5/lc0-baseline` 작업 트리와 로컬·원격 `master`는 유지한다.

## 병합 대상과 보존 범위

- 준비 브랜치: `codex/phase14-5-master-prep`
- 준비 작업 트리: `C:/Users/idene/source/repos/CreatorEngine-merge-phase14-5`
- 원본 HEAD: `55374f16ae783f1fc1b36d43b7d17d231455d050`
- 공통 조상: `e55cbb400eddc4eb2a7e1af29ee31303d9121875`
- 시작 시 분기: CLI 34개 / master 54개 커밋.
- 미커밋 CLI 변경 194개 경로는 별도 인덱스로 선택하여 `521fa21ac416cbc80e8b59623d2a340fe18e73cb`에 보존했다.
- 원본 변경 201개 경로의 파일 해시·삭제 상태, 원본 인덱스 해시, HEAD가 준비 전후 동일함을 확인했다.

원본의 다른 작업 7개 경로는 CLI 스냅샷에서 제외했다: `Prim_Suzanne.glb.meta`, `HashingString.h`,
`SerializationPlan.md`, `TransformUpdatePlan.md`, `UtilityFrameworkModernizationPlan.md`,
`hashing_string_contract_probe.cpp`, `verify-hashing-string.ps1`.
그중 master에 이미 반영된 변경은 master 버전을 유지한다.
`ReflectionTypedDraw.h`는 HashingString 관련 기존 수정을 분리한 뒤, master의 입력 확정 정책에 공통 Undo 호출만 연결했다.

## 충돌 해결

실제 미커밋 변경을 포함한 병합에서 충돌한 파일은 8개다.

| 파일 | 해결 내용 |
|---|---|
| `Editor/EngineEntry/ConsoleCommandSystem.cpp` | 제품·Commandlet 분리 구조를 유지하고 master 신규 명령을 해당 도메인 파일로 이식 |
| `Editor/EngineGUIWindow/ReflectionTypedDraw.h` | master HashingString 재구성·Enter 확정·빈 이름 방지 정책과 공통 Undo 결합 |
| `Engine/SceneRuntime/ScriptComponent.h` | 인스턴스 생성 실패 기억과 초기화 진입 계측을 함께 유지 |
| `ScriptCore/ScriptAssemblyLoader.cs` | Component/AniBehavior 개명과 실제 등록 경로를 사용하는 사전 검증 sink 결합 |
| `Tools/regression/run-all.ps1` | 양쪽의 CLI·스크립트 계약 검사를 보존하고 제거된 UI 모델 의존 경로는 복구하지 않음 |
| `Tools/regression/verify-model-cutover-budget.ps1` | master의 B1/B2 단계별 예산을 유지하면서 JSON phase 배열을 소비 |
| `Tools/regression/verify-script-add-awake-once.ps1` | 재생 중 초기화 규약과 네이티브 진입 카운터 판정을 결합 |
| `docs/RefactoringPlanDashboard.html` | master의 페이즈 9.5·12·12.5 및 다른 계획을 유지하고 14.5 상태 결합 |

충돌 표시 없이 자동 합쳐졌지만 수정이 필요했던 부분도 반영했다.
`script.reload`의 성공·실패 복구 모두 `RestoreAfterReload()`를 사용하며, 초기화 횟수는
반복적인 `EnsureInstance()` 호출이 아니라 실제 `OnInitialized()` 진입에서 센다.
CLI 생명주기 지표와 Player smoke는 master의 `pendingInitialize`·`pendingSimulation`·`GetComponentTypeNames()`를 사용한다.

## 통합 후 명령 계약

제품은 **99개·106이름**, Commandlet은 **104+3=107개**다. named JSON 입력은 59개, Undo 선언은 21개다.

| master에서 들어온 명령 | 통합 위치 | 결과 계약 |
|---|---|---|
| `light.proxy` | HTTP/JSON 제품 조회 | 광원 값 및 publish·commit·queue 누계의 소유형 스냅샷 |
| `assets.decodeab` | Commandlet | PNG 비교 건수·불일치·오류를 반환; 빈 코퍼스는 실패 |
| `assets.decodeabhdr` | Commandlet | HDR 비교 건수·편차·오류를 반환; 빈 코퍼스·치수 불일치는 실패 |
| `assets.texturebench` | Commandlet | decode·mip·compression 실제 시간과 완료 건수 반환 |
| `vk.texturecodec` | Commandlet | 기존 DX12/Vulkan 업로드 대조와 실제 bool 판정을 유지 |

카메라·광원 프로브의 판정과 편집 모드 리로드의 훅 카운터는 표식된 `script.invoke` 결과로 읽는다.
리로드 게이트의 생명주기 사건 트레이스는 기존 검사 범위를 유지한다.
한국어 verdict 소비자 상한 0을 높이지 않았다.
현재 전체 도표는 [CommandSurfaceTable.md](CommandSurfaceTable.md)다.

## 병합된 소스의 검증

로그는 준비 작업 트리의 `artifacts/merge-validation/`에 있다. 중간 실패 로그는 남겨 두고 아래에서 최종 실행을 구분한다.

| 검사 | 결과 | 최종 로그 |
|---|---|---|
| Editor Debug / Release | 빌드 통과 | `build-debug-final.log`, `build-release.log` |
| Development Player Debug | 빌드 통과 | `build-player-debug-final.log` |
| 제품 편집·Undo 및 Commandlet 분리 | Debug 281개 단정 통과 | `surface.log` |
| Release 최종 명령 계약 | 광원 HTTP 조회와 신규 Commandlet 격리를 포함한 288개 단정 통과 | `release-surface.log` |
| Debug / Release registry golden | 99개·106이름 일치 | `registry.log`, `release-registry-verified.log` |
| 초기화 중복 | native 진입 1회·trace 1회 | `script-awake.log` |
| 카메라 / 광원 | 각각 15개 단정; publish +8·commit +1·queue +1 | `camera-script-verified.log`, `light-script-verified.log` |
| 생명주기 기준선 | 네이티브 239·관리 33 사건 순서 동일 | `lifecycle-baseline.log` |
| 생명주기 리로드 | NN~SS 통과; 편집 모드 hook 0; 이전 문맥 회수 | `lifecycle-reload-verified.log` |
| 재생 세대 | 3세대·취소 3·누출 0 | `lifecycle-generation.log` |
| 리로드 실패 / 라이브 C# 수정 | 기존 어셈블리 유지·복원 2/2·새 코드 호출 통과 | `reload-failure.log`, `invoke.log` |
| UI | 재생 4회·48/48 단정 | `ui.log` |
| 텍스처 명령 | 7개 시나리오 통과; PNG 1/1 대조 및 DX12/Vulkan 95,604바이트 digest 일치 | `texture-verified.log` |
| 스크립트 경계 | API 207개·진입점 46개·게임 스레드 검사 206곳·생명주기 미러 통과 | `check-*.ps1.log` |
| HashingString | Debug / Release 계약 통과 | `verify-hashing-string.ps1.log` |
| 소비자 | 한국어 verdict 0/0·무인증 제품 경로 0 | `verify-cli-consumer-contract.ps1.log` |

첫 빌드에서 발견한 생명주기 필드명 및 Player 타입 조회명 불일치는 수정 후 재빌드했다.
새 프로브 소비자의 호출 구문·JSON 출력 옵션·상대 Work 경로 문제도 수정 후 위 최종 로그로 재확인했다.
구조화된 결과 파일이 없을 때 이전 로그로 성공 처리하지 않는다.
네이티브의 기존 경고와 C# trimming 관련 경고는 오류와 구분한다.
registry gate도 상대 Work 경로를 절대 경로로 바꾸고 경로를 인용하도록 고쳐 Release 재대조가 통과했다.
과거 CLI 비용 TSV의 빈 message 셀 6개는 `-`로 명시하여 master 대비 패치의 후행 공백을 제거했다. 측정값은 바꾸지 않았다.

## 병합 경계

이 작업은 통합 후보를 준비하는 단계이며 master 반영이나 원격 push를 수행하지 않는다.
master가 위 기준 SHA에서 더 진행하면 다시 충돌과 영향을 검사해야 한다.

전체 `run-all.ps1`, Shipping 패키지, 모든 Commandlet 모드·HDR 실물 코퍼스·canonical 성능은 이번에 전수 재검증하지 않았다.
광원 게이트는 게임 스레드의 프록시 큐잉까지 확인하며 렌더 스레드 최종 적용을 대신하지 않는다.
GUI 수동 조작과 canonical 성능의 기존 미확인 항목 때문에 Phase 14.5 전체 종결 보류는 유지한다.
통합 전의 실행 증거는 [Phase14_5Closure.md](Phase14_5Closure.md)에 별도 보존했다.
