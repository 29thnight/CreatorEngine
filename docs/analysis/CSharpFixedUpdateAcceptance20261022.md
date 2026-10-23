# C# 기본 저작 복귀와 고정 OnUpdate 수용 — 2026-10-22

후속 N3 명령·snapshot 배선은 API43으로 구현했다. 아래 API42 결과는 당시 이주 수용 기록이며, 최신 인계 범위는 [N3 후속 수용 기록](N3ExchangeAcceptance20261022.md)과 [인계 계약](../design/SimulationTickExchangeContract.md)을 따른다.

현재 작업 트리의 구현·Windows x64 Release 결과다. 고정 master의 과거 문서 감사와 구분한다. source SHA-256·삭제 파일·실행 증거 경로는 `CSharpFixedUpdateAcceptance20261022.json`에 보존한다. 커밋/푸시는 수행하지 않았다.

## 변경 결과

- `Component.OnSimulate`, 공개 `Component.Scope`, `SimulationScope`와 전용 Delay/Subscribe/cleanup/token 저작 구조를 제거했다. 시작 훅에서 별도 coroutine을 자동 실행하지 않는다. 추적 중인 활성 C# 소스에서 제거 API 참조는 0건이다.
- C#은 `public override void OnUpdate(float dt)`로 동기 갱신한다. C++ `SceneManager::Physics`가 공통 시계의 60Hz/최대 catch-up 4를 소유하고 관리 배치를 물리 앞에서 한 번 호출한다. 0틱 프레임은 호출 0회이며 렌더 frameDelta를 전달하지 않는다. 기존 물리 상수와 같은 기본값을 유지했다.
- ClrHost가 필수 `OnUpdate` 진입점을 `int(__stdcall*)(float)`로 바인딩하고 Bootstrap의 UnmanagedCallersOnly 배치가 인스턴스의 void/float 훅을 호출한다. 배치 실패는 -1, 컴포넌트 실패는 해당 인스턴스 비활성화로 격리한다. API v42는 구형 v41 호스트와의 조용한 갱신 누락을 차단한다. 기존 6단계 생명주기 enum은 늘리지 않았다.
- 생산 물리는 `ScenePhysicsSimulation::Step`으로 단일 스텝만 실행한다. 독립 물리 테스트의 `Advance` 누산 경로는 유지하며 생산 clock과 중복 실행하지 않는다. 기존 CCT 이동·접촉·previous/current pose를 재사용하고 보간 alpha를 공통 시계에서 전달한다.
- ScriptRegistry의 멤버십 변경을 순회 밖에서 처리한다. 시작 실패·비활성·파괴·종료 상태를 걸러내며 리로드 세대가 바뀌면 남은 배치를 중단한다. 정리 중 새 등록 거절이 ScriptFactory에 미결합 인스턴스를 남기지 않도록 되돌린다.
- 엔진 접촉 구독·역할 바인딩은 내부 ComponentLifetime으로 해제한다. 공개 저작 framework를 새 이름으로 대체하지 않았다. 정리 실패는 형제 정리와 OnEndSimulation을 건너뛰지 않으며 DDOL detach는 최종 해제와 구분한다.
- ContactStream은 프레임 버퍼와 tick 버퍼를 각각 제한 용량으로 소유한다. OnUpdate는 직전 tick 결과를 읽고 그 갱신 종료에 소비한다. 프레임 PostPhysics는 해당 프레임 결과를 읽고 EndFrame에 소비한다. 0틱 프레임이 다음 OnUpdate의 접촉을 잃지 않으며 다중틱이 같은 결과를 재생하지 않는다. 어느 경로도 빌린 span을 다음 소비 경계까지 보관하면 안 된다.
- 기존 호출자·프로젝트의 Phase22ProductAudioProbe·오디오 package 생성 template을 동기 상태/시간 누산 또는 일반 Task/CancellationToken 소유권으로 이주했다. 게임 스레드 synchronization context와 API 진입 거부는 유지한다. 일반 Task의 완료 시각·게임 pause·결정성을 전용 대기 계약으로 포장하지 않는다.

## 실행한 검증

| 검증 | 결과 | 증거 |
|---|---|---|
| ScriptCore/GameScripts · Editor/Player/Cooker/Packer | Release 빌드 성공 · native host 4종 API 42 · 새 SDK 발행 검증 성공 | `Build/csharp-fixed-gamescripts.log`, `Build/csharp-fixed-native-final2.log`, `Build/csharp-fixed-player.log`, `Build/csharp-fixed-cooker.log`, `Build/csharp-fixed-packer.log` |
| 실제 ScriptCore + 제한된 가짜 native API 표 | 20단정: 제거 표면, 필수 unmanaged 진입, 시작 게이트, 실패/재진입/자기 제거/고아/리로드/등록 rollback | `Contracts-f1377826dfea49d8b09979351f0bf4f4` |
| 접촉 CPU 계약 | 76단정 · 기존 반복 구간 할당 0바이트 · tick/frame 버퍼 경계 | `CONTACT_STREAM_OK` 로그 |
| Native 공통 clock 단독 검사 | 30/60/144fps 입력 10초에 각각 600틱, catch-up 4, pause/debt, reset, 잘못된 dt 거부 | `FIXED_SIMULATION_CLOCK_OK` |
| 실제 Editor | 두 Play/Stop, fixedDelta·비활성/재활성·실패/정리·자기 제거·pause/resume, 일반 await 재개·워커 API 거부 | `Editor-2b790b0edc4245c4bead5b28340c19d8` |
| 신규 cooked Player | OnUpdate 제품 표지 1회 · 2000 GT 프레임/실제 display 제출 · 정상 종료 · 패키지 불변 | `Player-96808e93eeb74fcea62e2cbe6bd03c0b` |
| 접촉 수명/역할 | Editor subscriber disable/enable 및 명시 역할 바인딩 각각 두 Play/Stop, 신규 Player subscriber 수용 | `Product-c243d621121f43b6bc0adea959d4d29e`, `Product-d8d3264cb5ce402b8cde54f650400f5c`, `Player-ec7ff91256aa426f915123357ccb9edf` |
| CCT | 실제 Editor ScriptProbe/MotionProbe 통과 | `Build/Obj/Phase19C0/http-81645088ea2c4d06af783c6d96b93828/result.json` |
| 생명주기 | 최종 Editor/ScriptCore로 DDOL 진행·핸들, 실제 script.reload·이전 ALC 회수·편집 모드 무음, 세 Play/Stop 세대 통과 | JSON의 ddol/reload/generation 로그 |
| 오디오 이주 | 실제 새 cooked Player에서 서로 다른 tick의 pause/resume·stop/GC·완료100/재생100·attached/positioned, 패키지 불변 | `Build/Validation/Phase22Player/011043706a7942c3854e03a97733b40f` |
| 기존 생명주기 계약 | native/managed 값 6개·순서·전달 배선 검사 통과 | `ScriptCore/check-lifecycle-enums.ps1` |

계약 harness는 실제 엔진 제품 수용을 대체하지 않는다. 위 표는 독립 검사와 실제 Editor/Player를 구분한다. 최종 SDK의 ScriptCore SHA가 Editor 수용의 SHA와 같은지 확인한 뒤 신규 Player를 만들었다. 오디오 표는 이주한 package fixture의 단일 MP3 경로이며 모든 codec/장치 조합 재수용을 뜻하지 않는다.

## 회귀 도구 변경과 한계

- `verify-csharp-fixed-update.ps1`은 실제 ScriptCore 계약·접촉·Native clock을 묶고 `-Product`로 소유한 Editor gate를 실행한다. Player gate는 통과한 Editor fixture와 발행 SDK를 받아 새 패키지를 만든다.
- 전용 Delay 인자/retention 검사는 폐기 사유와 API 제거 대체 검사로 전환했다. 기존 coroutine 축을 재던 도구는 새 계약·Editor gate로 연결했고 DDOL/세대/리로드 검사는 고정 갱신 표지를 직접 검사한다. 과거 coroutine 취소의 성공을 새 API 성공으로 복사하지 않았다.
- Release 접촉 fixture에 디버그 model corpus용 미사용 GLB를 무조건 넣던 부분을 Debug 조건으로 제한했다. Release DX12의 물리 전용 scene/SDK 경계에 맞춘 시험 입력 정리이며 Vulkan shader 오류를 고친 결과가 아니다. Debug의 기존 model corpus 요구는 유지한다.
- 기존 2026-10-20 전체 문서 감사는 master 이동·별도 파티클 작업·새 CS-AUTH 행 때문에 현재 전체 PASS가 아니다. 이번 문서 HTML/JS/링크 검증은 따로 기록한다. 기존 사용자 변경은 보존했다.
- PrSM 저장소는 수정하지 않았다. 서버 N3의 bounded 명령·snapshot observer·외부 계약 동결, 실제 N9 서버 target/2-client/connection drain은 잔여다. C# 기본 저작 복귀 완료와 서버 전체 완료를 혼동하지 않는다.
- 실제 30/60/144Hz 화면 제어에서의 CCT 궤적/시각적 보간 품질 matrix, GPU 픽셀·대표 성능/p99·Debug/Shipping/다른 OS 수용은 이번 결과가 아니다. 공통 clock의 합성 FPS 검사는 실제 화면 FPS matrix와 구분한다.

[스크립트 계획](../plans/ScriptSurfacePlan.md) · [네트워크/서버 계획](../plans/NetworkFrameworkPlan.md) · [PrSM 계약 계획](../plans/SimulationEffectContractPlan.md)
