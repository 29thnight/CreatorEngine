# 엔진 명령 표면 정리 — 2026-09-06

> master 병합 후보: `6b9c2b79`의 `light.proxy` 제품 조회와 텍스처 Commandlet 4개를 보존했다. 통합 검증 및 원본 작업 보존은 [병합 준비 기록](Phase14_5MergePreparation.md)을 따른다.

기준 HEAD `55374f16`. 과거 CLI 보존을 목표로 한 계획을 종료하고, 제품 편집 API와
검증 Commandlet의 등록·실행·소비 경로를 분리했다. 제품 명령 99개(이름 106개),
Commandlet 107개이며 두 집합과 처분표가 일치한다. 최초 정리에서 이름 4개와 파일 13개를 제거했고, 후속 정리에서는 종료된 벤치
6개·이전된 구 진입점 2개와 벤치 C++ 5개·셰이더 4개를 추가 제거했다. 개별 처분은
[CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)에 기록한다.

## 현재 마무리 상태

제품 99개 모두 소유형 terminal 결과를 반환한다. named JSON 입력은 59개, Undo 선언은 21개다.
`assets.scenemodel`은 Commandlet으로 이동했고 사용하지 않는 `animator.state/exit` 하네스를 제거했다.
`animator.param`과 `render.matmode`는 GUI와 같은 공통 Undo 작업을 사용한다.
`LegacyUnreported`, void 등록 및 직접 exit 변환 어댑터는 제거했다.

프로파일러·GPU·파이프라인·메모리·스크립트 진단은 실제 상태를 반환한다. Debug CRT가 없는
구성, 없는 인스턴스·모델, 검증을 수행하지 못한 경우를 성공 또는 0으로 표시하지 않는다.
한국어 verdict 소비자 13곳과 명령 존재를 소스에서 찾던 6곳을 정리했다. UUID 실제 계산값,
검사 수, draw/coverage, 모델 generation, UI 검사·초기화 횟수는 측정 지점의 값을 반환한다.

최신 빌드·게이트와 남은 검증 경계는 [Phase14_5Closure.md](Phase14_5Closure.md)를 본다.

## 등록과 수명

- `selftest`, `SelfTestTable`, `Registrar::SelfTest`를 제거했다.
- 닫힌 `experiment.matparity`의 등록·524줄 구현·헤더·빌드 항목을 제거했다.
  폐기된 material packer 대조 종료 근거는 `ModelImportPipelinePlan.md` I5-M1이다.
- `commands.dump`, `cli.probe.timing`과 중복 등록 저장소/프레임 계측을 소유하던
  `CommandBaseline`을 제거했다. 현재 discovery와 서비스 telemetry가 그 역할을 담당한다.
- 완료된 LC0 계측/정적 재고, LC6 live 분류/Undo 특성화, 중복 exit canary와
  기준선 파일을 제거했다. 실패·Undo 부재를 정상값으로 고정하던 표를 유지하지 않는다.
  실제 종료 코드는 exit-spine, 실제 편집 복원은 command-surface 게이트가 담당한다.
- 유지할 제품 회귀와 성능 측정은 별도 `CommandRegistry::Commandlets()`와 실행 표로
  등록한다. HTTP/일반 console/batch의 discovery 및 실행 표에는 들어가지 않는다.
- `scene.transformwritestats`, `scene.sparseresolver`, `scene.transformpull`의 개발자
  조회·설정은 유지하고 합성 `probe`/`bench`는 각각 `*.check` Commandlet로 분리했다.
  `scene.transformstats`는 원래부터 제품 telemetry여서 분리할 fixture가 없다.
- `experiment.matmigrate`, `experiment.matresolve`, `experiment.matscript`는 독립
  Commandlet 진입점으로 합성·실사 두 검사를 실행한다. matscript 35/35+4/4,
  matmigrate의 현재 v8 carrier 승계/v4 거부 계약을 유지한다.
- `commands.selftest`는 registry 무결성 조회, `gpu.baseline`은 GPU census 비교점,
  `cli.echo.args`는 입력 진단이다. 이름만으로 제품 진단까지 폐기하지 않았다.

## 공통 편집과 Undo

`EditorObjectOperations`와 공유 editor command가 아래 경로를 소유한다.

| 동작 | GUI 연결 | HTTP/CLI |
|---|---|---|
| 생성/삭제/복제/부모 | Hierarchy 및 기존 공유 command | object.create/delete/duplicate/parent |
| 이름 | Inspector | object.rename |
| Transform | Inspector, Scene View 기즈모/카메라 위치 적용 | object.transform |
| 네이티브 프로퍼티 | ReflectionTypedDraw | object.property/properties |
| 네이티브 컴포넌트 | Inspector 추가/제거 | component.add/remove |
| UI 위치/앵커/크기 | Inspector, Scene View | ui.pos/screenpos/anchor/size |
| 선택 | Hierarchy, Scene View | scene.select |
| 프리팹 소환 | Scene View drop | prefab.instantiate |
| 모델 배치 | Scene View drop, LoadModelToSceneObjCommand | model.place |
| Animator 매개변수 추가 | Animator 편집창 | animator.param |
| 재질 렌더 모드 | MeshRenderer Inspector | render.matmode |
| 태그 정의 | Inspector의 태그 추가 | tag.list/has/add/remove |
| Undo/Redo | 전역 단축키, Hierarchy | undo/redo |

UI 동작 하나에서 여러 객체/필드가 바뀌면 `CommitPropertyEdits`가 한 Undo 항목으로
묶는다. 드래그 종료 프레임에 delta가 0이라는 이유로 이력이 빠지던 조건도 제거했다.
입력값과 실제 상태가 같으면 항목을 만들지 않는다. 네이티브 프로퍼티 편집은 현재
reflection 타입의 setter와 authoring codec을 사용하며, 객체 신원 필드는 읽기 전용이다.

외부 대상 ID는 `@scene:index:generation`이다. 이름 중복은 오류이고, 외부에서 받은
낡은 슬롯 ID는 거부한다. Undo 내부는 scene ID + 논리적 instance ID로 다시 찾는다.
삭제 복원으로 슬롯이 바뀌어도 이전 이름/프로퍼티 편집을 같은 객체·컴포넌트에 적용한다.

서브트리 archive는 Entity·컴포넌트 authoring 값, instance ID, 부모/root 참조와
프리팹 등록을 복원한다. 삭제 전에 선택 참조를 정리하고 Undo 때 선택도 복원한다.
생성/복제/모델 배치 Undo는 삭제 직전 상태를 저장하므로 GUI에서 배치한 최종 위치를
Redo가 유지한다. 프레임 종료 파괴를 기다리는 명령은 HTTP drain을 다음 프레임으로
넘긴다. Undo 핸들러가 프레임 중간에 파괴 큐를 비우지 않는다.

실패한 Undo/Redo는 항목을 원래 스택에 둔다. 자산 저장/프리팹 정의 갱신, managed
script 실행/리로드 같은 engine service는 씬 편집 Undo와 별개로 설명한다.

## HTTP와 소비자

편집 명령은 descriptor에서 입력 스키마와 named-parameter 변환기를 함께 만든다.
`args`와 `parameters` 혼용, 누락/추가 필드, NUL, 잘못된 숫자·벡터·열거 값을 거부한다.
벡터는 JSON 배열, 프로퍼티 값은 스칼라 또는 숫자 배열을 받는다. 기존 positional
입력도 같은 API를 사용한다. 모든 HTTP descriptor는 `undoable`을 명시한다.
`ui.pos/size/screenpos`는 입력 스키마가 서로 다른 독립 명령으로 등록한다.

회귀·DX12·profiling·featuretest 시나리오는 `--commandlet-script`로 연결했다.
Commandlet 시나리오는 준비용 제품 명령을 함께 쓸 수 있으며 큐가 소진되면 종료한다.
일반 배치·stdin·HTTP와 혼용은 오류다. DX12 suite 목록은 소스 파싱 대신 실제
`--commandlet list` 결과에서 가져온다.

MCP 서버는 이번 변경에 포함하지 않는다. 향후 MCP는 HTTP discovery/입력 스키마와
결과를 연결하는 어댑터다. 엔진 상태와 Undo, 검증 수명은 엔진과 Commandlet에 남는다.

## 후속 정리

종료된 벤치·SSAO 참조 경로 제거, 공통 진단/태그 API, 명시적 fixture 입력의
최신 검증은 [CommandletRetirementAudit.md](CommandletRetirementAudit.md)를 본다.
아래 수치와 한계는 **앞선 95/111 표면을 검증한 시점의 기록**이다.

## 앞선 단계 검증 이력

- Debug/Release Editor, Development Player Debug 빌드 통과. 기존 LNK4229 및
  SceneRuntime C4244 경고가 남아 있으며 새 컴파일 오류는 없다.
- `verify-editor-command-surface.ps1`: 최종 Debug/Release 각각 **175개 단정 통과**.
  Commandlet 합성·실사, 목록, 독립/시나리오 자동 종료, 혼합 입력·없는 파일,
  일반 배치/HTTP의 검증 명령 부재, 실제 편집/Undo/Redo 상태와 실패 입력을 검사했다.
- 제품 registry는 **95 commands / 102 names / problems 0**. help 95/95,
  Debug/Release registry 골든 일치. 처분표와 실제 제품/Commandlet 이름 집합도 일치한다.
- 기존 parser golden, invocation, discovery, exit-spine, HTTP service, hierarchy mutation,
  prefab duplicate, prefab override write, light slot restore 회귀 통과.
- Play selection/undo, Play round-trip 통과. UI layout golden은 1920×1080에서
  rect 14개·히트박스 1개의 diff 0이다. `--console`과 Commandlet을 섞던 소비자
  4곳을 고치고 공백이 있는 선택 대상은 명시적으로 quote했다.
- DX12 소비자: `--commandlet list`에서 선택한 `dx12.descriptorheap` 통과.
  `dx12.selftest`는 두 실행에서 `internal_error / command.exception`, 엔진 exit 5였다.
  상위 suite가 CSV만 기록하고 exit 0을 내던 결함을 고쳐 지금은 exit 1을 반환한다.
  없는 `-Only` 항목도 무시하지 않는다. 실패한 검사를 통과로 바꾸거나 삭제하지 않았다.
  `EnhancedSceneRendererSelfTest.cpp`의 검사 본체 예외 원인은 이번 범위에서 미확정이다.
- 서비스 게이트의 옛 최소 200개 조건은 필요한 제품 명령과 검증 명령 부재 검사로
  대체했다. 명령 감소 자체를 실패로 보던 조건이 Commandlet 분리와 충돌했다.
- 소비자 계약 정적 검사 통과. 사람용 판정 문자열을 읽는 소비자 상한은 14→13으로
  낮췄다. 남은 13개 consumer 및 6개 소스 기반 존재 검사는 전체 JSON 이관 완료로
  계산하지 않는다. 오래된 서비스의 `resultBearing:false`도 성공으로 바꿔 표시하지 않는다.
- PowerShell 전체 구문 검사, 프로젝트 항목 경로 검사, `git diff --check` 통과.

증거는 `artifacts/cli-direction/verified-debug-surface.log`,
`verified-release-surface.log`, `final-debug-service.log`,
`cleanup-debug-gates/`, `verify-*-final.log`, `consumer-final-dx12.*/` 및
`build-cleanup-*.log`에 있다. 실패한 중간 실행은 남겨 두었고,
최종 통과 로그와 구분한다. UI 벡터 응답은 `{x,y}` authoring 값이므로 검사도 그 형식을
비교한다. 일반 배치 결과 파일에는 `--result-format jsonl`을 명시한다.

전체 run-all, Commandlet 111개의 모든 모드/코퍼스, GUI 수동 조작, Player Shipping
빌드 및 성능 수치는 이번 검증 범위에 포함하지 않는다. MCP 서버 구현도 포함하지 않는다.
