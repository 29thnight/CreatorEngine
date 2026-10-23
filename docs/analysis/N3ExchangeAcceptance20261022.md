# N3 명령·스냅샷 배선 구현과 인계 수용

2026-10-22 · Windows x64 Release · 현재 작업 트리 · Script API 43

공통 고정 틱에 bounded 명령 적용과 성공 커밋 snapshot 복사 경계를 연결했다. C#은 기존 컴포넌트·일반 메서드에서 `Simulation.GetStatus/Submit/ReadSnapshot`을 사용한다. 서버 작업자는 [동결한 N3 계약](../design/SimulationTickExchangeContract.md)의 native adapter를 연결한다. 새로운 lifecycle hook이나 C# 전용 coroutine 저작 구조는 없다.

## 구현 범위

- `SimulationExchange`는 256개 명령/64바이트/미래128틱, 8개 snapshot/4096바이트의 고정 보존 경계를 소유한다. 입력과 결과는 복사하며, native 생산자/소비자는 concurrent 호출을 사용할 수 있다. GT만 명령 적용·capture·commit을 구동한다.
- SceneManager는 lifecycle 시작 처리 뒤 지정 틱 명령을 FIFO로 적용하고 OnUpdate와 물리 단일 Step을 실행한다. 접촉 게시 뒤 capture가 성공해야 clock과 snapshot을 커밋한다. 실행 중 틱은 외부에서 읽히지 않는다.
- 이미 실행한 틱·너무 먼 틱·큐 포화·잘못된 길이를 명시적으로 거부한다. 현재 틱으로의 재진입도 거부한다. 잘못된 명령 kind/죽은 target/nonfinite 좌표는 해당 명령만 거절하고 snapshot에 합계로 기록한다.
- slow reader의 덮어쓰기는 SnapshotGap으로 알리고, 작은 버퍼는 필요한 길이와 재시도 계약을 제공한다. 배치/물리/adapter/capture 실패는 미완료 결과를 공개하지 않고 세션을 중단한다. 세계 rollback과 실패 틱 재시도는 제공하지 않는다.
- Play/Stop·씬 epoch·script.reload에서 clock/큐/결과 cursor/adapter 수명을 무효화한다. pause는 접수한 값을 유지한다. 종료는 ingress를 닫고 adapter 소유권을 해제한 뒤 씬을 정리한다. 기존 접촉/DDOL/컴포넌트 정리 배선은 유지한다.
- native shared-owner adapter의 Apply/Capture가 서버 확장점이다. 기본 로컬 위치 명령은 생존·활성 씬·Transform을 검증하고, 계층 Transform을 resolve해 물리 뒤 마지막 대상의 값을 복사한다. 이 로컬 핸들은 wire ID가 아니며 기본 snapshot은 전체 씬 snapshot이 아니다.
- Script API 43에서 status/submit/read 함수 포인터를 append하고 필수 바인딩·native/managed 크기 검사·이전 버전 거부를 추가했다. 기존 물리 identity와 자산 typed-binding 시험의 가짜 API 표도 이주했다.

## 실행한 검증

검증별 디렉터리·최종 호스트/managed/소스 SHA-256은 [기계 판독 수용 기록](N3ExchangeAcceptance20261022.json)에 기록한다. 아래 독립 검사를 실제 transport 또는 서버 제품 수용으로 확대하지 않는다.

| 검증 | 결과 |
|---|---|
| Release ScriptCore/GameScripts, Editor/Player/Cooker/Packer | 빌드 성공, 최종 호스트 계약 43, 새 SDK 발행 |
| Native 교환 계약 | 2831단정: 합성30/60/144fps 동일 지정 tick/상태, FIFO, 포화, horizon, 복사, 네 스레드 생산자, gap/버퍼 재시도, 실패·예외·초과 capture·이전 성공 결과 보존, reset 재진입·adapter 해제 |
| 실제 ScriptCore/ABI | 23단정: 기존 고정 업데이트 수명/실패 격리, 이전 버전/누락 슬롯 거부, 새 status crossing, worker 접근 거부 |
| 접촉 CPU/물리 identity | 접촉76단정·기존 steady loop 할당0바이트, 물리 identity15단정/API43 |
| 실제 Editor N3 소비자 | 두 Play/Stop, future/FIFO 명령이 OnUpdate 전에 적용, invalid target/kind/nonfinite 거절 합계, 물리 뒤 복사, 미커밋 비공개, 작은 버퍼 재시도와 gap, pause 보존/재개, stop·실제 reload·scene.new의 세대 무효화 |
| 실제 Editor 회귀 | 고정 업데이트/일반 async·종료, 접촉 subscriber disable/enable 수명, CCT ScriptProbe/MotionProbe, DDOL 이동/동기 진행 |
| 신규 cooked Player | 동일 SDK의 API43/ScriptCore로 새 패키지, N3 소비자 완료, 2000프레임과 실제 display 제출, 정상 종료·패키지 불변 |
| 문서/도구 | 변경 PowerShell 구문, scoped diff, 계획 HTML 재생성·시각 검사, 문서 script 검사 |

Editor 표지는 Console stdout에 기록되므로 시험은 engine HTML과 stdout을 함께 검사한다. 최초 시험은 모든 상태 단정이 통과했지만 HTML만 검사해 표지를 찾지 못했다. 수정한 gate의 최종 실행과 위 소스 신원을 수용 대상으로 삼는다.

## 서버 작업자에게 넘기는 범위

N3의 명령·snapshot API 및 native adapter 수명/실패 계약은 구현하고 실제 로컬 소비자로 수용했다. 서버 담당자는 새 epoch에서 adapter를 바인딩하고, 값 명령 제출과 성공 snapshot 복사 API에 wire decode/권한/transport를 붙인다. API 변경은 버전·인계 문서·회귀를 함께 갱신한다.

N2의 wire identity/schema/권한, N4의 실제 loopback/transport, N5의 full world snapshot/replication, N9의 renderer 없는 서버 target·2-client·graceful drain은 별도 단계다. N3 계획의 실제 화면 FPS별 CCT 궤적/시각적 보간 품질 matrix도 잔여 수용으로 유지한다. 합성 FPS 검사나 렌더 있는 Player 검증으로 이 항목을 완료 처리하지 않는다. 대표 성능/p99·GPU 픽셀·Debug/Shipping·다른 OS도 이번 수용 범위가 아니다.

[네트워크 계획](../plans/NetworkFrameworkPlan.md) · [C# 계획](../plans/ScriptSurfacePlan.md) · [이전 C# 고정 업데이트 수용](CSharpFixedUpdateAcceptance20261022.md)
