# N3 고정 틱 명령·커밋 스냅샷 계약

2026-10-22 · Script API **43** · C++ `SimulationExchange.h` / C# `Simulation.cs`

서버 작업자는 이 경계를 통해 검증한 값 명령을 제출하고 성공한 틱의 복사본을 읽는다. 기존 60Hz·최대 catch-up 4·C++ 소유 `OnUpdate(float fixedDelta)`를 사용한다. 네트워크 패킷, wire identity/schema, 연결별 권한 검증, full world snapshot과 replication은 N2/N4/N5의 소유다. 이 문서의 구조체 메모리를 패킷으로 전송하지 않는다.

## 실행 순서

`수명 시작 처리 → 지정 틱 명령 FIFO 적용 → C# OnUpdate → 물리 단일 Step → 접촉 게시 → snapshot capture → clock commit → snapshot 공개`

물리 없는 씬도 같은 경로를 사용한다. pause는 Begin/Commit을 실행하지 않고 이미 접수한 명령과 분수 시간 debt를 보존한다. 재개 시 다음 지정 틱에 적용한다. 틱 실행 중 제출은 현재 틱에 재진입하지 않으며 다음 틱부터 허용한다. 접수 순서는 mutex 안에서 발행하는 sequence로 정해진다. 서로 다른 스레드의 도착 순서 자체를 결정적으로 만들지는 않는다.

## 용량과 결과

| 경계 | 계약 |
|---|---|
| 명령 | 세대 전체 미실행 최대 256개, payload 최대 64바이트, committed 기준 미래 128틱 |
| 접수 | `Submit(epoch, targetTick, kind, copiedBytes, out sequence)`; 성공 시 nonzero sequence. 실패 시 0 |
| 실행 | 같은 targetTick에서 접수 sequence 순서. 잘못된 kind/값/죽은 target은 해당 명령만 거절 |
| 결과 | snapshot header에 해당 틱의 Applied/Rejected 합계. 개별 명령 결과나 네트워크 ack가 필요하면 adapter가 payload에 정의 |
| 스냅샷 | 성공 틱 8개 ring, payload 최대 4096바이트. 소비자는 각자 afterTick cursor 소유 |
| 읽기 | `Read(epoch, afterTick, header, output)`은 가장 오래된 다음 복사본을 반환. 빌린 엔진 메모리 없음 |
| 느린 소비자 | 덮어쓰인 cursor는 `SnapshotGap`과 가장 오래 남은 header/payload를 반환. 자동 무음 유실 없음 |
| 작은 출력 버퍼 | `BufferTooSmall`과 필요한 Bytes/header 반환, payload 미복사. 같은 cursor로 재시도 |
| 아직 결과 없음 | `NoSnapshot`; 실행 중 틱이나 capture만 끝난 틱은 읽히지 않음 |

`SimulationResult`/`TickResult`는 같은 정수 순서다: Ok=0, Inactive=1, StaleEpoch=2, InvalidArgument=3, Late=4, TooFar=5, Full=6, NoSnapshot=7, SnapshotGap=8, BufferTooSmall=9, WrongThread=10, Busy=11, Failed=12. Late/TooFar/Full은 접수 실패이므로 성공처럼 cursor/sequence를 진행하지 않는다. Full에서는 호출자가 다음 틱/프레임에서 재시도하거나 자체 정책으로 폐기한다. 엔진이 보이지 않게 큐를 확장하지 않는다.

## 소유권과 실패

- Native `Status/Submit/Read`는 concurrent copied-value 경계다. 단일 GT만 `Reset/Bind/Begin/Prepare/Commit/Fail`을 호출한다. 콜백은 mutex 밖에서 호출한다.
- SceneManager는 Play/Stop, 활성 씬 epoch 변경, script.reload 시 clock·큐·snapshot cursor와 adapter 세대를 초기화한다. 리로드 시 이전 코드를 유지하는 실패 결과여도 기존 ingress 세대는 무효다. shutdown은 접수를 닫고 adapter를 해제한 뒤 씬을 정리한다.
- 세대는 scene/play뿐 아니라 script ingress 수명도 나타낸다. 이전 epoch의 명령·읽기는 StaleEpoch다. DDOL로 살아 있는 로컬 핸들도 이전 명령 epoch를 우회하지 않는다. 세대 소진은 wrap하지 않고 실패한다.
- 명령 적용·관리 갱신 배치·물리·capture 수준 실패는 세션을 중단하고 미완료 snapshot을 공개하지 않는다. 기존 컴포넌트별 OnUpdate 예외 비활성화는 배치 실패와 구분한다. adapter 예외와 4096바이트 초과 capture도 실패한다. 이미 성공한 snapshot은 다음 reset까지 읽을 수 있다. 적용한 세계 변경의 rollback은 제공하지 않으므로 실패한 틱을 재시도하지 않는다.
- native adapter는 `std::shared_ptr<Runtime::SimulationAdapter>`로 붙인다. Apply/Capture 중 로컬 소유자가 adapter를 유지하며 reset 뒤 저장된 callback 소유권을 해제한다. 실행 중 또는 미실행 명령이 있을 때 Bind는 Busy다. 새 epoch에서 생산자를 열기 전에 다시 바인딩한다. adapter 소멸자는 예외를 던지지 않아야 한다.
- native transport 작업자는 SceneManager보다 먼저 ingress/read 작업을 중단하고 join해야 한다. 이 계약이 연결 drain이나 transport 스레드를 생성하지는 않는다.

## 서버 adapter 연결

1. PlayModeEvent/active scene 변경 후 GT에서 `SceneManagers->SimulationCommands().Status()`의 새 Epoch를 취한다. PlayModeEvent 진입 통지는 공통 세션 활성화 뒤다.
2. GT에서 `Bind(epoch, sharedAdapter)`한다. 네트워크 작업자는 인가된 연결/명령을 해석하고, wire ID를 검증 가능한 값 payload로 변환해 `Submit`한다. kind 0은 금지한다. adapter가 kind/schema와 payload 해석을 소유한다.
3. `Apply(const TickCommand&)`는 GT에서 실행하며 길이·범위·권한·세대·대상 수명을 검사한다. 포인터/EntityHandle/typeID를 wire ID로 쓰지 않는다. false는 해당 명령 거절, 예외는 세션 실패다.
4. `Capture(span<byte>, bytes, schema)`는 물리/접촉 처리 뒤 실행한다. 수명 검사를 거친 값만 bounded output에 기록한다. false/예외/초과 길이는 커밋을 거절한다. 아직 준비되지 않은 N5 full snapshot을 반환하는 것으로 표시하지 않는다.
5. 소비자는 `Read(epoch, cursor, ...)` 결과가 Ok 또는 SnapshotGap일 때 반환한 Tick으로 cursor를 진행한다. Gap은 resync/full snapshot 정책으로 연결한다. 네트워크 전송이나 ack 대기를 GT 콜백에서 수행하지 않는다.

## 기본 C# 경로와 실제 소비자

C#은 일반 메서드 `Simulation.GetStatus`, `Submit`, `ReadSnapshot`을 사용한다. 모두 기존 엔진 API와 같은 GT 진입 검사를 사용한다. 일반 Task에서 완료한 값은 기존 synchronization context로 돌아온 뒤 제출한다. 전용 Scope/await 저작 구조나 새 lifecycle hook은 없다.

adapter 미등록 시 kind/schema **1**은 로컬 위치 브리지다. 명령은 ObjectHandle(index/generation 8바이트)+world xyz(float 12바이트)이며, `SetWorldPosition(epoch,tick,Entity,Float3,out sequence)`가 이를 복사한다. 실행 때 finite·활성 씬 소속·Transform·생존을 검사한다. snapshot은 마지막으로 적용한 위치 대상의 물리 뒤 값 20바이트+Alive(uint32 4바이트)다. Alive=0이면 좌표를 현재 상태로 사용하지 않는다. 전체 씬/여러 대상 snapshot이 아니다. native adapter를 바인딩하면 Apply/Capture를 모두 adapter가 소유하며 이 기본 브리지는 사용하지 않는다.

`SimulationExchangeContractProbe`는 실제 Editor/Player에서 이 브리지를 소비한다. native `SimulationExchangeProbe`는 별도 adapter의 상태 명령/capture, 합성 FPS·포화·동시 생산자·느린 소비자·실패/해제/재진입 경계를 검사한다. 독립 검사를 실제 네트워크 transport/2-client 또는 화면 FPS별 물리 결정성으로 확대하지 않는다.

[네트워크 계획](../plans/NetworkFrameworkPlan.md) · [C# 저작 계획](../plans/ScriptSurfacePlan.md)
