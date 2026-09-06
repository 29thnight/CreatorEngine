# Editor 명령 / Commandlet 경계

> master 병합 후보: `6b9c2b79`의 `light.proxy` 제품 조회와 텍스처 Commandlet 4개를 보존했다. 통합 검증 및 원본 작업 보존은 [병합 준비 기록](../analysis/Phase14_5MergePreparation.md)을 따른다.

2026-09-06 · 현재 작업 기준 HEAD `55374f16`

## 목표

GUI와 외부 자동화는 동일한 편집 동작과 Undo 이력을 사용한다. CLI의 HTTP/JSON
서비스는 개발자용 제품 조작·조회 창구다. 검증은 별도 프로세스의 Commandlet이며,
이관이 끝난 일회성 하네스는 소스·등록·빌드 항목·전용 소비자를 함께 제거한다.
기존 명령의 개수나 동작을 무조건 보존하는 것은 목표가 아니다.

과거 LC0-LC9의 이동 계획과 실측 이력은 Git 이력에 보존된다. 이 문서는 현재
방향과 남아 있는 제품 계약만 설명한다. 실제 명령 처분은
[CommandSurfaceDisposition.tsv](../analysis/CommandSurfaceDisposition.tsv), 실행 증거는
[CommandSurfaceImplementation.md](../analysis/CommandSurfaceImplementation.md)를 본다.

## 소유권

```text
GUI ──────────────┐
                  ├─ EditorObjectOperations / EditorProjectOperations / editor commands ─ UndoManager ─ Scene
CLI HTTP/JSON ────┘
       ↑
향후 MCP adapter

--commandlet / --commandlet-script ─ 독립 검증 registry ─ 제품 API + 검사
```

- MCP는 향후 HTTP 요청과 도구 스키마를 연결하는 외부 어댑터다. 엔진 상태 변경,
  Undo 저장, 검증 실행을 MCP 안에 다시 구현하지 않는다. 이번 작업에 MCP 서버는 없다.
- 서비스 registry와 Commandlet registry는 서로 다른 조회 표다. HTTP discovery와
  일반 console/batch에는 검증 이름을 등록하지 않는다.
- `commands.selftest`는 개발자용 registry 무결성 조회다. 도메인 검사나 합성 fixture를
  실행하는 하위 창구가 아니다. `cli.echo.args`도 입력 진단으로 유지한다.
- `wait`는 프로세스 스크립트 문법이다. HTTP에서는 전역 프레임 정지를 거부한다.
- 살아 있는 제품의 회귀 검사·성능 측정은 Commandlet으로 유지한다. 계획 단계가
  끝났다는 사실만으로 현재 제품 계약을 지키는 회귀 검사를 폐기하지 않는다.

## 구현 순서와 결과물

1. 전체 등록을 처분표로 대조한다. 미분류 후보를 없애고 제품 API, 검증,
   제거, 분리가 필요한 혼합 진입점을 구분한다.
2. 닫힌 하네스를 제거한다. `selftest`와 `SelfTestTable`, 폐기된 packer의
   `experiment.matparity`, LC0의 `commands.dump`·`cli.probe.timing`·중복 등록/계측
   저장소 및 전용 스크립트를 제거한다. 완료된 LC6 라이브 분류·Undo 특성화
   하네스도 새 제품 계약 검사로 대체한다. 실패를 정상값으로 고정하는 표를 유지하지 않는다.
3. 유지할 검증은 Commandlet registry로 분리하고 실제 소비 스크립트를 연결한다.
   DX12 suite는 Commandlet의 런타임 목록을 읽는다. 혼합 Transform 명령의
   `probe`/`bench`는 `*.check` Commandlet로만 실행한다.
4. 생성/삭제 → 복제/부모 → Transform/UI → 프로퍼티/컴포넌트 → 프리팹 소환/선택을
   공통 편집 API로 연결한다. Undo 깊이뿐 아니라 실제 상태·논리적 식별자·선택·
   프리팹 override의 복원을 검증한다.
5. HTTP 입력 스키마, 오류 결과, 문서와 회귀 실행 경로를 정리한다. 배포나 커밋은
   별도 요청 범위다.

## 2026-09-06 후속 정리

제품 명령은 99개(이름 106개), Commandlet은 107개다.
`experiment.matruntime`의 seed 텍스처도 명시적 입력으로 받으며, 코퍼스 전용
검사는 일반 명령과 구별한다. 종료된 비교 벤치
`dx12.bench11`, `dx12.encoderbench`, `dx12.ssaoscale`, `dx12.postscale`,
`dx12.forwardscale`, `perf.reflect`를 제거했다. SSAO의 벤치 전용 참조 PSO와
옛 알고리즘 셰이더도 제품에서 제거했다. Post/Forward의 참조 경로는 현재 픽셀
동등성 검사가 사용하므로 유지한다.

`scene.hierarchycheck`와 `animator.status`는 공통 읽기 전용 진단 서비스다.
`tag.list/has/add/remove`는 프로젝트 태그 API이며 GUI의 추가 동작도 같은
저장/Undo 서비스를 사용한다. `experiment.animlive`와 `tag.authoring.probe`는
별칭으로 남기지 않았다.

정리 대상 여섯 명령의 Gunner/SU/scene.glb/Cube 텍스처 선택은 입력 인자로 옮겼다.
`assets.generationcorpus`/`assets.scenemodel`의 코퍼스 전용 단정은 유지한다.
실제 corpus를 사용하는 소비 스크립트는 입력 경로와 독립 pose 골든을 명시한다.
이는 제품 자산 삭제나 corpus 검증 완료를 뜻하지 않는다.
상세 범위와 검증은 [후속 정리 기록](../analysis/CommandletRetirementAudit.md)을 본다.

## 마무리 반영

- 제품 99개 전부 결과 반환, named JSON 입력 59개, Undo 선언 21개.
- `assets.scenemodel`은 검증 표로 이동, 사용하지 않는 `animator.state/exit` 제거.
- `animator.param`·`render.matmode`는 공통 GUI/Undo 작업으로 연결.
- 미보고 상태·void 등록·legacy 직접 exit 어댑터 제거.
- 한국어 verdict consumer 13→0, 소스 기반 명령 존재 consumer 6→0.
- 최종 판정은 [종결 검토](../analysis/Phase14_5Closure.md)의 현재 실행 증거와 미검증 경계로 정한다.

## Commandlet 사용

```powershell
& .\Bin\x64-Debug\Editor\CreatorEditor.exe --commandlet list -- --result-file C:\Temp\commandlets.jsonl
& .\Bin\x64-Debug\Editor\CreatorEditor.exe --commandlet scene.transformpull.check probe -- --result-file C:\Temp\pull.jsonl
& .\Bin\x64-Debug\Editor\CreatorEditor.exe --commandlet-script C:\Temp\scenario.txt --result-file C:\Temp\scenario.jsonl
```

결과 파일의 부모 폴더는 호출자가 준비한다. 시나리오는 제품 준비 명령과 검증
명령을 함께 쓰며, 프레임당 한 줄을 실행한다. 큐가 소진되면 `quit` 없이 종료한다.
`--commandlet`은 하나의 검사를 실행하고 종료한다. `--` 뒤에는 호스트 옵션을 둔다.

Commandlet 모드와 일반 `--script`/`--exec`/stdin/HTTP를 혼합하면 실행 전에 오류로
종료한다. 성공 exit 0, 인자 오류 2, 선행조건 실패 3, 검사 실패 4, 내부 오류 5다.
계속 실행과 최종 실패 누적이 기본이고 `--fail-fast`도 적용한다. 그래픽·CLR이
필요한 검사는 Editor 호스트를 사용하므로 별도 headless 실행 파일은 아니다.

완료 조건이 있는 한시적 하네스는 그 조건이 충족되면 등록·소스·전용 소비자를
함께 삭제한다. 현재 유지한 제품 회귀는 해당 제품 계약이 폐기되거나 후계 검사에
흡수될 때 같은 절차로 제거한다. `experiment.*`라는 이름만으로 일괄 폐기하지 않는다.

## 편집 HTTP 계약

```json
{"command":"object.create","parameters":{"name":"Lamp","type":"Light"},"mode":"sync"}
{"command":"object.describe","parameters":{"target":"Lamp"},"mode":"sync"}
{"command":"object.transform","parameters":{"target":"Lamp","position":[1,2,3]},"mode":"sync"}
{"command":"object.property","parameters":{"target":"Lamp","component":"LightComponent","field":"m_intencity","value":4.5},"mode":"sync"}
{"command":"undo","mode":"sync"}
```

`GET /commands/<name>`의 입력 스키마와 실제 변환기는 같은 descriptor를 사용한다.
추가/누락 필드, 잘못된 타입, NUL, 유효하지 않은 숫자·벡터를 실행 전에 거부한다.
`parameters`와 `args`를 동시에 보낼 수 없다. 기존 positional `args`도 같은 공통
편집 API로 들어간다. 이름에 공백이 있으면 스크립트에서 따옴표로 묶는다.

- 대상은 이름 또는 `object.describe`가 반환하는 `@scene:index:generation`이다.
  중복 이름은 거부하고 ID를 요구한다. 지워지거나 씬이 교체된 슬롯 ID를 재활용하지 않는다.
- Undo 내부는 scene ID와 객체의 논리적 instance ID를 사용한다. 삭제·생성·복제
  재실행으로 슬롯이 바뀌어도 이전 편집 이력이 복원된 객체를 찾는다. 외부 슬롯 ID는
  복원 후 다시 조회해야 한다.
- `object.describe`는 부모·자식 ID, Transform, 컴포넌트 ID/타입을 소유 값으로 반환한다.
  `object.properties`는 반사 필드의 타입과 현재 값을 반환한다. 동일 타입 컴포넌트는
  `#instanceId`로 구분한다.
- 생성 타입 Light/Camera/Mesh/Canvas는 해당 기본 컴포넌트까지 만든다. 공간 타입만
  바꾸고 필요한 컴포넌트를 빠뜨리던 이전 CLI 동작은 보존하지 않는다.
- 삭제 취소는 서브트리, 컴포넌트 값/식별자, 부모/root 참조, 프리팹 등록과 선택을
  복원한다. 이미 있는 컴포넌트 추가는 no-op이다. 필수 공간 컴포넌트는 제거할 수 없다.
- UI 위치/앵커/크기와 기즈모 편집도 같은 프로퍼티 트랜잭션을 사용한다.
  여러 객체를 한 번에 바꾼 GUI 동작은 Undo 한 항목으로 묶는다.
- 새 편집은 Redo를 비운다. 무변경과 잘못된 입력은 Undo 항목을 추가하지 않는다.
  실패한 Undo/Redo의 항목은 원래 스택에 남긴다.
- 프레임 종료 시 생명주기 정리가 필요한 명령 뒤에는 HTTP drain을 다음 프레임으로
  넘긴다. 명령 핸들러가 프레임 중간에 파괴 큐를 강제로 비우지 않는다.
- 프리팹 정의 저장/갱신과 씬 저장은 영속 자산 서비스다. 씬 편집 Undo와 별개이며
  `undoable:false`로 설명한다. 프리팹 **소환**은 씬 편집으로서 Undo를 지원한다.

## 검증 기준

`Tools/regression/verify-editor-command-surface.ps1`는 Commandlet 격리·종료와 HTTP
실제 상태 복원을 검사한다. `run-all.ps1`에 연결한다. 이름 변경 한 건이나 빌드
성공을 전체 GUI 기능의 수동 검증으로 확대하지 않는다. Debug/Release 실행 결과,
회귀 범위와 미실행 항목은 구현 기록에 명시한다.
