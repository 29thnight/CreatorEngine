# 네트워크 담당자 최종 인계 점검 — 2026-10-23

> 2026-10-24 최종 정본: [사내 잔여·전달 준비·작업 순서·검증 기준](NetworkWorkerHandoff20261024.md)을 우선한다. 아래 판정과 표는 10월 23일 당시의 기록이다. 기존 Scene/Entity 재사용 무렌더 실행의 후속 수용으로 당시 headless 잔여는 닫혔다. 전달 리비전·도구·증거 고정과 콘텐츠별 provider 연결 조건, 실제 N9 서버 제품은 최종 정본에서 구분한다.

현행 기본 인계 회귀는 HeadlessBoundary를 포함한 **8 등록 항목 / Debug·Release 10그룹**이다. `-Product`는 기존 6제품 gate와 headless 1제품 gate를 더해 **17그룹**을 실행한다. CI 기본 실행의 소스/계약 통과와 로컬 전체 제품 실행을 분리한다.

> 후속 변경: 아래 본문은 API43/OnUpdate 시점의 기록이다. 현재는 API45의 F# 공통 관리 실행기와 override 없는 컴포넌트 일반 멤버를 정적으로 등록한다. 새 Editor·cooked Player·30/60/144fps·moving GPU와 등록 NetworkHandoff 15그룹을 재수용했다. [현재 계약](../design/FSharpScriptingContract.md)과 [API45 근거](FSharpManagedRuntimeAcceptance20261023.md)를 인계 정본으로 따른다. 현행 headless 범위는 위 2026-10-24 후속 계약을 따르고, N9 전체 제품 수용과 이전 기록은 구분한다.


## 판정

**네트워크 담당자의 N0 → N2/N4 → N5 착수는 가능하다. 실제 Dedicated Server 제품의 사내 선행이 전부 닫힌 것은 아니다.** ND-a·W4/ND-b·N1·N3는 수용됐지만, NetworkFrameworkPlan의 사내 책임인 렌더 없는 SceneRuntime 링크·구동은 아직 제품 수용 근거가 없다. N9는 조건부·미수용을 유지한다. 담당자의 독립 개발 하네스로 이 수용을 대신하지 않는다.

이번 점검은 현재 작업 트리와 기존 실제 실행 기록을 대조했다. 제품 전체를 다시 실행한 점검은 아니다. 작업 트리에 미커밋·미추적 구현이 많으므로 실제 전달 시 계약·구현·회귀·문서가 포함된 단일 소스 리비전과 해당 리비전의 수용 기록을 함께 고정해야 한다. 기존 HEAD만 전달하면 현재 수용 구현을 전달한 것이 아니다. 이번 작업에서 커밋·푸시는 수행하지 않았다.

## 사내 선행과 남는 책임

| 항목 | 현재 판정 | 인계 시 유지할 조건 |
|---|---|---|
| ND-a 계약 6종·변경 절차 | 완료 | contract/protocol v1, 36바이트 LE 헤더, ID 원장·tombstone·lock 및 실패 canary 유지 |
| W4 / ND-b | 완료 | 저작 변경 통지, 엔티티 생성·파괴·DDOL 이동·일반 쓰기, 구독 수명·overflow/rebuild 유지 |
| N1 Archive/Parser | 완료 | Network에 저작/진단 archive 유입 금지. 소스 경계·실제 cooked parser 비호출과 전체 parser 라이브러리 unlink는 별도 |
| N3 고정 틱·명령·보간 | 완료 | 60Hz/최대 catch-up 4, C++ 계약 OnUpdate(dt), API43, 성공 tick만 snapshot 게시. 로컬 큐 상수를 wire 정책으로 전용하지 않음 |
| 렌더 없는 SceneRuntime 링크·구동 | **사내 잔여, N9 전 필수** | Editor/renderer/표시 장치 없이 cooked scene 로드→tick/물리/관리 호출→정상 종료를 실제 산출물로 수용. 링크·패키지 의존도 함께 검사 |
| quantizer·snapshot 배치·backpressure | 후속 공동 접점 | N0 비용·지연·포화 계측 후 담당자가 제안하고 사내 계약 소유자가 변경 절차로 확정. 착수 전에 임의 수치 동결하지 않음 |
| 모델 shader cook | 범위 제한 잔여 | Prim_Cube cook 실패가 남아 있음. 수용된 primitive CCT package를 일반 모델 package 성공으로 확대하지 않음. 실제 제품 콘텐츠가 필요하면 해당 cook 경로부터 재검증 |

C# Verse식 계약, OnSimulate/Scope.Delay 전용 구조, 별도 OnSimulationTick 추가, PrSM 언어 완성은 선행 조건에 넣지 않는다. 기존 Pre/PostPhysics·일반 Task 수명과 AI simulation frustum 의미는 유지한다.

## 착수 순서와 완료 기준

| 순서 | 소유·작업 | 완료 기준 |
|---|---|---|
| 0 | 사내 + 담당자: 전달 리비전·도구·SDK·fixture·기록 고정 | 새 checkout에서 등록 회귀와 제품 gate 재현. 변경 없는 파일·패키지 hash 확인. 문서 PASS와 제품 PASS 분리 |
| 1 / N0 | 담당자: 기준선·telemetry·실패 게이트 | tick 비용, bytes/packet, queue depth/drop, RTT/loss, 복사·할당·포화 비용 기록. malformed·초과 용량·실패 경로가 gate를 실제 실패시킴 |
| 2 / N2 | 담당자: wire ID/schema·권한 | 로컬 EntityHandle/typeID와 wire ID 분리. 원장·폐기 ID·호환성·필드 schema 정본 확정. stale/reused ID·다른 버전·비권한 쓰기 거부 |
| 3 / N4 | 담당자: bounded loopback transport·handshake | 6종 계약을 소비. 손실/재정렬/중복/손상/unknown message/포화/작은 receive buffer 검증. Send 접수와 상대 적용 ack 구분. disconnect·drain에서 누수·잔여 callback 없음 |
| 4 / N5 | 담당자: registry + spawn/despawn + full snapshot | ND-b 수명·DDOL에 연결하고 N3 명시 tick에서 적용. 실패 tick snapshot 미게시. late join·씬 교체·stale 세대·destroy/rebind·복원 후 상태 일치. 실제 두 endpoint의 수렴 확인 |
| 5 / N6 | 담당자 + 사내 계약 소유자: delta·interest·quantization·backpressure | full 기준과 delta 복원 비교, baseline 유실 시 full 재동기화, quantization 오차·대역폭·CPU 예산 수치화, 느린 peer에서도 메모리 bounded |
| 6 / N7 | 담당자: 실제 transport A/B와 adapter | 동일 fixture·부하·실패 조건에서 비교 후 선택 이유 기록. loopback 수용을 실제 socket 수용으로 확대하지 않음 |
| 7 / N10 | 담당자: hardening·운영 관측·soak | 접속/메시지/용량/rate 제한, 장시간 reconnect·drain·오류 복구, 메모리·큐 상한·오류율·지연 예산을 사전 명시하고 통과 |
| 별도 / N9 | 사내: headless 런타임·패키지 경계 / 담당자: 서버 bootstrap·접속·운영 | 제품 요구 확정 후 실제 DedicatedServer 산출물의 cooked startup, 창/GPU 없는 tick, 2-client 동기화, 정상 종료·connection drain, Editor/renderer/저작 parser 패키지 의존 차단 |
| 선택 / N8 | 담당자: prediction/reconciliation | 제품이 요구할 때만 착수. 지연·손실하의 권한 상태 수렴·오차 보정·재실행 범위와 예산 수용 |

N2와 N4의 독립 설계·하네스는 N0 뒤 병행할 수 있다. N5 통합은 양쪽의 wire/연결 계약 수용 뒤 진행한다. 사내 headless 경계 작업은 네트워크 작업과 병행할 수 있으나 N9 제품 완료 전에 반드시 닫는다.

## 공통 인수 gate와 재확인 근거

```powershell
./Tools/regression/verify-net-wire-boundary.ps1 -Configuration All -BaseRef <검토기준커밋>
./Tools/regression/run-all.ps1 -Suite NetworkHandoff -Configuration All -Product -EditorExe <해당리비전Editor.exe> -CookedStage <신규cookedStage>
```

필수 회귀 삭제·누락·하위 gate 실패는 전체 실패다. 제품 옵션 없는 실행은 Editor/Player/GPU 수용을 대체하지 않는다. transport/서버가 추가되면 그 제품 gate를 별도로 등록한다.

- 이번 재실행: wire 경계 PASS(6 headers/16 records/8 wire records, v1), Archive/Parser PASS(runtime 860 / role closure 1014), 필수 회귀 등록 PASS(7 등록 항목이 Debug/Release에서 9그룹으로 확장).
- N3 수용 JSON의 sourceSha256 31개와 현재 파일 일치(불일치 0). 실제 실행 당시 전체 인계 gate 15그룹(기본 9 + 제품 6) PASS, failures 0. 전체 회귀 결과 · 로컬 기록, 사이트 미포함 (`Build/Verification/NDA/Suite-9883ae0a29ce4ae3897c4788716cdd3a/result.json`).
- 실제 FPS 30.0047 / 59.5147 / 143.1011. 동일 tick CCT/강체 궤적·0틱 GPU 보간을 수용한 작은 DX12 fixture 결과이며 대표 해상도 성능이나 다른 기기/서버/TAA·FSR·DLSS 수용이 아니다. [N3 기록](N3PacingAcceptance20261023.md).
- Engine/Network의 현재 파일은 계약 6종과 원장/lock이다. 실제 transport·replication·서버 제품 구현으로 판정하지 않는다. 원격 CI는 배선됐지만 실행 수용 기록은 없다.

## 인계 정본

[네트워크 계획](../plans/NetworkFrameworkPlan.md) · [계약 변경 절차](../design/NetworkContractChangePolicy.md) · [로컬 관찰자](../design/SceneObservationContract.md) · [tick 명령/snapshot](../design/SimulationTickExchangeContract.md) · [Archive/Parser](../design/ArchiveParserBoundaryContract.md) · [고정틱/보간 판정](../design/SimulationPacingAcceptanceContract.md)
