# W4 · ND-b 저작 변경·로컬 관찰 구현 수용

2026-10-22 · Windows x64 · 현재 작업 트리

## 구현 범위

- typed Inspector scalar/custom drawer·enum/container/nested 편집을 중앙 transaction으로 연결. 전체 복원 문서를 유지하고 해당 객체의 변경 필드만 통지한다.
- Inspector/Undo/Redo 출처, no-op·잠금 거부·stale Undo와 컨테이너 내부 주소 수명을 처리한다. 빈 값 컨테이너의 null 표기는 명시적 저작 replay에서 비우며 일반 로더의 기존 유지 규칙은 회귀로 보존한다.
- 일반 Component.NotifyPropertyChanged와 렌더 dirty를 분리한다. 비렌더 reflection/Prefab, native render setter, Enabled, Transform 단일/배치와 물리 shape setter를 연결한다.
- 엔티티 실제 생성/편입 완료와 파괴/분리/재부착·root retirement를 copied 정체성으로 통지한다. DDOL 원래 EntityHandle을 무효화하고 ScriptObjectHandle은 유지한다. 목적 계층 remap 뒤 Attached, 분리된 transfer의 finalization에는 단일 Destroyed를 발행한다.
- shared-owner/token 구독, owner dispatch·worker queue, 콜백 예외/포화 metrics 및 종료 정리를 추가한다. ND-a wire 계약은 변경하지 않는다.

계약과 어댑터 사용법은 [SceneObservationContract](../design/SceneObservationContract.md)를 따른다.

## 검증

| 검사 | 결과 | 범위 |
|---|---|---|
| SceneObservation 실제 헤더 Debug/Release | PASS · 각 35검사 | 구독/해제, 콜백 수명, 재진입, worker 큐, 예외, 포화 |
| NetworkHandoff 전체 등록 suite + Product | PASS · 10개 그룹 | ND-a, W4/ND-b, N3 Debug/Release, PhysicsABI, PhysicsIdentity Debug/Release, lifecycle enums, 실제 N3·W4/ND-b Editor |
| 실제 runner 실패·등록 제거 canary | PASS | ND-a exit37 전파와 ND-a/W4-ND-b 필수 등록 제거 거부 |
| production render queue CPU 회귀 | PASS · 6 cases | 실패 재시도·mask 병합·제거·세대/교체·소유자 |
| Release Editor 새 빌드/실제 W4·ND-b 통합 | PASS · 2회 | typed commit/enum/container/nested·잠금·Undo/Redo·native/Transform/비렌더·entity/DDOL |
| 최신 Editor·Player Release 빌드 | PASS | 최종 변경 소스로 컴파일·링크. Player cooked 실행 수용과 구분 |
| 물리·CCT·Script·Motion·DDOL·계층/전환 실패 | PASS · 205 commands | 실제 Editor, Script probe 18 passed / 0 failed |
| 기존 C# DDOL 호출자 | PASS | OnUpdate 시작 1회, 이송 added/removing 각 1회, 이송 후 진행 1회 |

## 증거

- [소스 해시·실행 결과 JSON](SceneObservationAcceptance20261022.json)
- `Build/Verification/NDA/Suite-bfe132510e284f1b98c5de7dd2b5c645/result.json`: 최종 등록 회귀 10/10, failures=[]
- `Artifacts/scene-gc/Release/result.json`: 최종 런타임의 authoring/일반 관찰·렌더 등록/GC/DDOL 각 2회
- `Build/Verification/N3Exchange/Editor-b7315e1f12f1484283c7175d3ed055c8/result.json`: 실제 명령·snapshot·세대/씬 경계
- `Build/Obj/Phase19C0/http-624e76a6716143679f0d2c06b704d97f/result.json`: 물리/CCT·계층·전환 실패
- `Build/Verification/W4NDB/CallerRegression`: 기존 C# DDOL 실행 로그와 재현 씬 복사본. 고유 이름의 원본 fixture도 ignored Assets에 보존한다.
- `Build/w4-ndb-editor-final-state.log`, `Build/w4-ndb-player-final-state.log`: 최종 Editor/Player 빌드

## 판정 경계

실제 Editor의 typed 편집 commit 경로를 호출하는 회귀이며 마우스/키보드 입력 또는 렌더된 픽셀 비교를 뜻하지 않는다. 렌더 dirty publication까지 검사하고 W5 렌더 소비 프록시/GPU 수용은 별도다. raw public C++ 필드 대입 자동 감지나 전체 public 필드 private화는 포함하지 않는다.

Network ID/schema·권한·transport/replication, 렌더 없는 최종 서버 링크/구동·2-client·배포/장기 soak, PrSM 언어 및 원격 CI 실행을 이 결과로 완료 표시하지 않는다. 커밋/푸시는 수행하지 않는다.

부가 전체 문서 인덱스 사전 검사는 기존 `docs/design/RHISoftwareCommandListSpecification.src.html` 생성물 drift로 실패했다. 이번 요청의 계획 3개 시각 검사와 신규 문서 생성·대시보드/문서 스크립트 검사는 통과했으며, 전체 문서/RHI 생성물은 변경하지 않았다.
