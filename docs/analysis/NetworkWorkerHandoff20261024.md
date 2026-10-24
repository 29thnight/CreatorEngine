# 데디케이티드 서버 최종 인계 점검 — 2026-10-24

**수용한 Windows x64 콘텐츠 범위에서는 추가 사내 선행 구현 잔여를 발견하지 않았다. N0 → N2/N4 개발 착수는 가능하다. 실제 전달 전에는 미커밋·미추적 소스를 포함한 전달 리비전과 도구·SDK·증거를 고정해야 한다.** 실제 서버 제품과 모든 게임 씬의 무수정 실행을 완료한 판정은 아니다.

[네트워크 계획](../plans/NetworkFrameworkPlan.md) · [무렌더 실행 계약](../design/HeadlessSceneRuntimeContract.md) · [실제 실행 수용](HeadlessSceneRuntimeAcceptance20261024.md) · [대시보드](../RefactoringPlanDashboard.html#phase-20).

## 이번 점검의 근거와 판정

정본은 `C:/Users/idene/source/repos/CreatorEngine`의 현재 작업 트리다. 기준 HEAD는 `71504c4fd610a205c8c857538364e1d392e9e150`이며, HEAD만 전달하면 아래 미커밋 구현을 전달하지 못한다. 별도 재현 checkout과 원본의 명시적 source overlay 72개 SHA256을 다시 대조했다.

이번에는 기존 수용 JSON의 참조 95개·직접 프로젝트 소스 지문 417개·실제 host 결과 57건·제품 회귀 17그룹의 기록을 현재 파일과 대조했고 불일치는 0이다. wire 경계는 6 headers/16 records/8 wire records, contract/protocol v1을 재검사했다. 필수 회귀 8등록 항목과 headless source/project 경계도 재검사했다. 원장은 `Build/Verification/HandoffAudit20261024/evidence-audit.json`, 실제 실행 기록은 [수용 JSON](HeadlessSceneRuntimeAcceptance20261024.json)에 있다.

이번 점검에서 Editor/Player/GPU 전체를 다시 실행하지 않았다. 실제 실행 PASS는 연결한 직전 수용 기록의 범위이며, 원격 CI 실행 PASS를 주장하지 않는다. 과거 고정 master의 전체 문서 감사 실패는 기존 리비전/설명 기준과 누락된 Artifacts 링크 문제로 남아 있으며 현재 변경 문서 검사와 구분한다.

| 사내 선행 | 현재 판정 | 넘길 경계 |
|---|---|---|
| ND-a 계약 6종·변경 절차 | 수용 | 고정폭 wire ID, 36바이트 LE 헤더, 원장/tombstone/lock, 금지 타입·실패 canary |
| W4 / ND-b | 수용 | 저작 변경 통지, 실제 생성/파괴/DDOL·쓰기 관찰, shared-owner/token·포화 후 rebuild |
| N1 Archive/Parser | 수용 | 저작/진단/wire 역할 분리, cooked parser 비호출, headless 최종 링크의 ryml/c4 차단 |
| N3 clock·명령·snapshot·보간 | 수용 | 공통 60Hz/최대 catch-up 4, F# 일반 멤버 OnTickSimulate(dt), API45, 성공 커밋 읽기 |
| 렌더 없는 SceneRuntime | 수용 | 기존 SceneManager/Scene/Entity/Component/ClrHost·수명/핸들/GC/DDOL 재사용 |
| 기존 소비자·재현 | 수용 기록 대조 일치 | Debug/Release/새 checkout host 57건, NetworkHandoff All -Product 17그룹, Editor/Player/F#/프리팹/애니메이션 |
| 전달 리비전·도구·증거 고정 | **전달 전 잔여** | 아래 전달 준비를 끝내고 동일 리비전의 검증 원장을 함께 전달 |

## 전달 전 준비와 조건부 사내 작업

1. **전달 소스 고정:** 계약·공통 런타임·HeadlessHost·fixture·회귀·문서를 누락 없이 검토해 리비전을 고정한다. 현재 미추적 구현과 무관한 Effect·모델·workspace 작업을 구분한다. 이번 점검은 커밋·푸시를 실행하지 않았다.
2. **재현 묶음 고정:** F# 도구 revision `3351cbc80d7216eb10110c4c8b30dce98aef1d1f`, MSVC/Windows SDK/.NET 10, dependency 설치 조건, SDK·cooked fixture·패키지 원장·source overlay·로그를 전달한다. Build 하위 증거는 Git 추적 여부와 별도로 함께 전달한다. 기존 새 checkout은 vcpkg/NuGet cache를 재사용했으며 clean VM/Linux/self-contained 수용이 아니다.
3. **서버 콘텐츠 목록 확정:** 사용할 component reader·project layer·asset bundle·외부 collision geometry와 관리 서비스 목록을 정한다. 현재 기본 reader는 실제 Transform/PhysicsBody/CharacterMovement/ScriptComponent다. 미설치 항목은 입장 오류다. 추가 항목이 필요하면 엔진 담당자가 기존 Entity/Component factory·서비스 경계에 연결하고 해당 콘텐츠 회귀를 추가한다. 별도 제한 엔티티 모델을 다시 만들지 않는다. 이 작업은 그 콘텐츠를 처음 사용하는 통합 단계의 선행이며, 현재 fixture로 N0/N2/N4를 시작하는 조건은 아니다.
4. **측정 후 공동 확정:** quantizer·snapshot 배치·backpressure 임계와 큐/대역폭/오차 예산은 N0/N5 실측 후 담당자가 제안하고 사내 계약 소유자가 변경 절차로 확정한다. 실제 transport 의존 매니페스트·패키징 변경도 사내 저장소 소유자가 반영한다. 수치를 지금 임의로 동결하지 않는다.

## 넘길 작업·착수 순서·완료 기준

| 순서 | 담당 작업 | 완료 판정 |
|---|---|---|
| 0 | 공동: 전달 리비전·fixture·제품 요구 고정 | 위 전달 준비 완료. 동일 소스로 필수 gate 재현, 서버 콘텐츠/provider 목록과 운영 OS·배포 방식 명시 |
| 1 / N0 | 서버 담당: 기준선·telemetry·실패 gate | tick 비용·encode/decode·bytes/tick·복사/할당·큐 depth/drop·RTT/loss를 부하와 함께 기록. malformed/포화/실패를 주입하면 gate가 실제 실패 |
| 2A / N2 | 서버 담당: wire ID/schema·권한·registry | 로컬 handle/typeID/token과 wire ID 분리. ID 원장·tombstone·버전·field schema 확정. DDOL Rebind revision과 expected binding 검증. stale/reused ID·다른 schema·비권한 쓰기 거부 |
| 2B / N4 | 서버 담당: bounded loopback·framing·handshake | N2와 독립 하네스 병행 가능. 손실/재정렬/중복/손상·unknown message·초과 길이·작은 수신 버퍼·포화 검증. Send 접수와 상대 적용 ack 구분. drain/강제 종료 뒤 잔여 callback·누수 없음 |
| 3 / N5 | 서버 담당: spawn/despawn·full snapshot·late join | N2/N4 수용 후 ND-b 관찰자와 N3 명시 tick에 연결. 두 endpoint의 동일 커밋 서수에서 권한 상태 수렴. 늦은 접속·씬 교체·DDOL·파괴/재연결·오래된 snapshot의 부활 차단. 실패 커밋은 전송하지 않음 |
| 4 / N6 | 서버 담당 + 사내 계약 소유자: delta·interest·quantization·backpressure | full 기준과 delta 복원 비교. baseline 누락/유실 시 full 재동기화. 오차·bytes/tick·CPU·할당 예산 명시 후 통과. 느린 peer에서도 메모리/큐 bounded |
| 5 / N7 | 서버 담당: 실제 transport A/B·adapter | 같은 N6 fixture·부하·손실/지연 조건으로 비교하고 선택 근거 기록. 구체 라이브러리는 INetworkTransport 뒤에 격리. 실제 소켓·접속/종료·패키지 의존을 별도로 수용 |
| 6 / N9 | 서버 담당: 실제 서버 bootstrap·접속·배포 / 사내: 필요한 서비스·패키지 경계 | 전용 서버 제품 범위 확정 후 필수 인수. 실제 DedicatedServer 산출물로 cooked 시작→창/GPU 없는 tick·CPU 물리·F#→실제 2-client 상태 수렴→정상 종료/connection drain. import/map/패키지/로드 모듈의 렌더·Editor·저작 parser 차단 |
| 7 / N10 | 서버 담당: hardening·운영 관측·soak | 허용 접속·메시지·payload/rate·큐·메모리·지연·오류율·시험 시간/부하를 먼저 명시. 반복 reconnect·손실/포화·실패 복구·shutdown을 포함해 장시간 예산 통과 |
| 선택 / N8 | 서버 담당: prediction/reconciliation | gameplay 요구가 확정될 때 별도 착수. 권한 상태 수렴·보정 오차·tick history/replay 범위와 비용 수용. 현재 필수 선행 아님 |

N2와 N4는 N0 이후 병행할 수 있고 N5는 양쪽 계약 수용 뒤 통합한다. N9의 console/bootstrap 개발은 앞 단계와 병행할 수 있으나, 제품 완료 판정은 N5/N7과 필요한 콘텐츠 서비스 통합 후 실제 산출물로 한다. N10의 실패 하네스는 초기에 만들되 최종 soak는 최종 서버 제품으로 실행한다.

## 실제 연결 API와 유지할 동작

| 소비 지점 | 현재 API/계약 | 인계 시 주의 |
|---|---|---|
| 엔티티/쓰기 관찰 | SceneManager::Observations(), SceneObservation::Subscribe, ISceneEntityObserver/IComponentWriteObserver | copied 로컬 값과 구독 수명을 유지하고 N2가 wire ID로 변환. renderer dirty 큐를 전용하지 않음 |
| 명령/커밋 결과 | SceneManager::SimulationCommands(), SimulationExchange::Bind/Submit/Read, SimulationAdapter::Apply/Capture | 공유 소유 adapter, GT Bind/Reset, epoch와 성공 커밋 후 Read를 소비. 현재 ISimulationFrameObserver라는 구현 API는 없음 |
| clock | SceneManager::SimulationClock(), ExecuteSimulationTick | 단일 누산기·실행 순서 유지. Capture는 커밋 전이므로 이 안에서 외부 전송하지 않음. 성공 커밋이 확인된 결과만 게시 |
| cooked/확장 컴포넌트 | CookedSceneLoader::RegisterComponentReader와 기존 Entity/Component factory | 실제 컴포넌트·identity·수명/핸들을 유지. 필요한 provider는 엔진 담당자와 연결 |
| 씬/세대/종료 | 기존 SceneManager 전환·DDOL·Decommissioning | global script handle 유지와 scene/SDK handle·명령 epoch 갱신을 구분. 종료 훅 뒤 해제·drain·GC/물리 balance |

현재 local exchange는 copied 명령 256개/64바이트·미래 128틱·snapshot 8개/4096바이트의 **로컬 계약**이다. schema 1이나 이 payload를 wire 포맷으로 전송하지 않는다. 네트워크 full snapshot 저장소/분할·압력 정책은 N5/N6가 별도로 설계하고, 공통 한계 변경은 기존 소비자를 포함해 검토한다. Bind는 단일 adapter를 교체하므로 기존 로컬 명령·snapshot 소비자를 조용히 잃지 않도록 조합/호환 정책과 회귀가 필요하다.

F# 콜백 예외는 기존대로 해당 스크립트를 격리하며 나머지 배치를 계속한다. 배치 전체 실패 때 미완료 snapshot을 공개하지 않는 계약과 구분한다. 이 경계는 세계 상태 전체의 rollback이나 임의 Task 완료 순서·플랫폼 간 PhysX bitwise 결정성을 보장하지 않는다. 서버의 오류/복구 정책과 상태 비교 오차는 제품 수용에서 명시한다. F# 일반 멤버 OnTickSimulate(dt)/API45를 사용하며 OnSimulate·Scope.Delay·C# 병행 저작·새 생명주기·PrSM 언어 완성을 선행에 넣지 않는다.

## 인수 gate

아래는 저장소 루트의 pwsh에서 실행한다. 각 placeholder를 전달 리비전의 실제 값으로 바꾼다.

```powershell
./Tools/regression/verify-net-wire-boundary.ps1 -Configuration All -BaseRef <검토기준커밋>
./Tools/regression/run-all.ps1 -Suite NetworkHandoff -Configuration All -Product -EditorExe <해당리비전Editor.exe> -CookedStage <해당SDK로만든신규cookedStage>
./Tools/regression/verify-headless-scene.ps1 -Configuration Debug
```

기본 등록은 8항목/Debug·Release 10그룹, Product는 7제품 gate를 더한 17그룹이다. Product의 headless Release와 별도 Debug 실행을 유지한다. 필수 gate 삭제·하위 실패·실행 누락은 전체 실패다. 새 transport/registry/replication/실제 서버 gate도 필수 등록하며, 기존 suite만 통과한 상태를 새 서버 제품 PASS로 보고하지 않는다. 외부 F# 도구가 필요한 빌드는 고정된 저장소 revision과 경로 조건도 전달한다.

각 작업의 PR/인수 결과에는 소스·도구·SDK/패키지 식별자, fixture·부하·예산, 실제 명령/종료 코드, 실패 주입 결과·로그와 미수용 범위를 적는다. 로컬 제품 PASS·CI 배선·원격 CI PASS를 각각 구분한다.

## 인계 정본

[네트워크 계약과 변경 절차](../design/NetworkContractChangePolicy.md) · [로컬 관찰자](../design/SceneObservationContract.md) · [tick 명령/snapshot](../design/SimulationTickExchangeContract.md) · [Archive/Parser](../design/ArchiveParserBoundaryContract.md) · [F# 계약](../design/FSharpScriptingContract.md) · [무렌더 실행](../design/HeadlessSceneRuntimeContract.md) · [이전 점검 기록](NetworkWorkerHandoff20261023.md).
