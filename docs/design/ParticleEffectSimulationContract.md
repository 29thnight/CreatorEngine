# 파티클 시뮬레이션 첫 수치 계약

> 2026-10-24 · PHASE 10-2b-1 · CPU 표본 입력과 최소 DX12 GPU 체인

[전체 계획](../plans/ParticleEffectRedesignPlan.md) · [모듈 IR](ParticleEffectModuleIRContract.md) · [프레임 전달](ParticleEffectTransportContract.md) · [수용 기록](../analysis/Phase10SimulationAcceptance20261024.json)

## 이번 실행 범위

CPU IR 평가기에 난수·커브·그라데이션을 추가하고 `StepEffectParticle`로 고정 tick의 수명·이동 기준을 만들었다. 별도로 Effect 패키지의 실제 RHI 그래프 기여 함수와 Slang compute shader를 추가해 최소 생성/초기화·갱신·수명 종료·위치 적분을 GPU에서 실행한다.

**일반 모듈 IR을 GPU 코드로 변환한 결과는 아직 아니다.** 이번 GPU 경로는 상수 초기값과 Gravity→Drag 순서를 가진 고정 커널이다. CPU 쪽 기준값은 공통 IR을 실행해 만든다. GPU의 난수/커브/그라데이션·형상 생성, 사용자 그래프, 임의 스택 순서, 제품 snapshot·burst 인수 확인, Billboard는 후속이다. 10-2b 전체를 완료 처리하지 않는다.

소스는 `Engine/Effect/EffectModuleIR.h/.cpp`, `EffectSimulation.h/.cpp`, `Shaders/EffectSimulation.slang`이며 검증기는 `Tools/regression/verify-effect-simulation-gpu.ps1`이다. 기존 CPU 검증기는 표본 입력·step 검사도 함께 실행한다.

## CPU 표본 입력

RandomInput은 기존 `EffectRandomKey`의 FNV-1a 표본 신원 계약을 유지한다. effect/emitter seed, particle ID, emitter/module instance/port 안정 ID와 표본 시점으로 key를 구한다. AtSpawn은 tick을 무시하고 EachTick은 simulation tick을 포함한다. 실행 시 유효한 emitter ID가 없으면 거절한다. 뷰·프레임 번호나 모듈의 스택 위치는 신원에 넣지 않는다.

분포 v1은 64비트 unsigned wraparound 연산이다. counter는 1부터 시작하며 다음 식으로 각 성분의 비트를 얻는다.

```text
x = key + 0x9e3779b97f4a7c15 * counter
x = (x xor (x >> 30)) * 0xbf58476d1ce4e5b9
x = (x xor (x >> 27)) * 0x94d049bb133111eb
bits = x xor (x >> 31)
```

- Float는 상위 24비트를 2^24로 나누어 양 끝점 사이를 double 중간값으로 선형 보간한 후 float로 변환한다. 최종 float 반올림으로 상한과 같아질 수 있다.
- Float3는 x/y/z에 차례로 독립 counter를 사용한다. 각 성분의 범위를 보존한다.
- UInt는 양 끝을 포함한다. 64비트 span과 rejection sampling으로 modulo bias를 제거하며 전체 `[0, UINT32_MAX]`도 처리한다.
- Boolean/Color 난수, 역전 범위와 잘못된 표본 시점은 기존 스키마 검증에서 거절한다. 분포의 통계 품질이나 GPU 동일성은 이번 수용에서 주장하지 않는다.

CurveInput·GradientInput은 `clamp(age/lifetime, 0, 1)`을 사용한다. 인접 키 사이를 선형 보간하며 Gradient는 선형 RGBA/HDR 값이다. Age와 Lifetime의 초기화 읽기를 컴파일러가 자동 추가한다. age는 유한한 비음수, lifetime은 유한한 양수여야 한다. 키/타입/단계 검증과 실행 후 포트 영역 검증은 기존 공용 검증기를 재사용한다.

## CPU 고정 tick 기준

`StepEffectParticle`은 모든 속성이 초기화된 살아 있는 입자와 Update 모듈만 받는다. dt는 유한한 비음수다. dt=0은 상태를 유지한다. 죽은 입자는 다시 실행하지 않는다.

1. age에 dt를 한 번 더한다.
2. age >= lifetime이면 즉시 종료하며 모듈·위치 적분을 실행하지 않는다.
3. 공통 IR의 Update 모듈을 스택 순서로 평가한다.
4. `position += velocity * dt`를 한 번 수행한다.

실패하면 age·속성·alive를 모두 유지한다. 고정 시계 backlog, emitter start time, 새 입자 슬롯 관리·재방출을 맡는 scheduler는 아직 연결하지 않았다.

## 최소 GPU 체인

`EffectGpuParticle`은 64바이트 구조화 버퍼 원소다. position/age, velocity/lifetime, color, size/alive/id/reserved를 고정 순서로 전달한다. `EffectGpuStep`도 64바이트다. 이는 작은 고정 커널 ABI이며 저장·cook ABI나 일반 IR 형식이 아니다. GPU의 산술 overflow 오류 보고와 CPU 같은 전체 tick rollback은 아직 구현하지 않았으며 이번 수용은 아래 유한 수치 fixture 범위다.

| 단계 | 동작 |
| --- | --- |
| Initialize | 모든 슬롯을 정의된 상수로 초기화. ID는 슬롯 번호. 앞의 min(requested,capacity)개를 활성화하고 초과 요청은 버림 |
| Update | alive·dt 검사 → age 증가 → lifetime 경계에서 종료 → 선택적 Gravity → 선택적 Drag → 위치 적분 |
| Readback 검사 | CopySource 전환·제출·완료 대기 뒤 전체 버퍼를 CPU 기준과 비교 |

최대 용량은 이 첫 커널에서 4,096개로 제한한다. 테스트는 1·65·4,096 용량에서 요청 0·1·최대·최대+17과 Gravity·Drag flags 0·1·2·3과 별도 dt=0 정지 모드를 조합한다. flags=0 비정지 등속 이동과 둘 다 켠 경우의 정확한 수명 경계 종료도 검사한다.

`AddEffectSimulationPass`는 backend 중립 서비스와 명령 인코더를 사용한다. 그래프의 기본 선언 순서/상태 기반 스케줄링으로 UAV 접근 사이 배리어와 readback 전환을 만든다. 명시적 single-writer/versioned 그래프 모드는 이번 수용 범위 밖이다. pipeline과 장치, 버퍼는 호출자가 기록과 GPU 완료까지 보존해야 한다. 매 pass는 값 매개변수만 복사하며 컴포넌트나 GT EffectState를 조회하지 않는다.

검증은 같은 영속 버퍼를 완료 후 다음 조합에서 다시 초기화해 재사용한다. 비동기 여러 프레임의 동시 슬롯 재사용·생성 journal 확인·씬 전환 중 GPU retire는 별도 제품 연결 단계에서 검증해야 한다. 현재 `Effect.HDRBoundary`는 계속 관찰만 하며 버스트 인수 확인을 발행하지 않는다.

## 수용과 재현

```powershell
pwsh -NoProfile -File Tools/regression/verify-effect-module-ir.ps1 `
  -OutputDirectory artifacts/phase10-sampling-new
pwsh -NoProfile -File Tools/regression/verify-effect-simulation-gpu.ps1 `
  -OutputDirectory artifacts/phase10-gpu-new
```

CPU 검사는 실제 Effect 라이브러리 소비자 Debug/Release 각각 274개 단정을 통과했다. 난수 고정 비트 표본, UInt 전체/좁은 범위, AtSpawn/EachTick·입자 신원, 곡선·그라데이션 키 경계와 보간, 암묵적 읽기·수명/step 순서·rollback을 포함한다.

실제 DX12 GPU 검사는 Debug 60조합에서 1,082,120개 float 성분과 정수 ID/alive를 비교했다. 허용 오차는 `abs(actual-reference) <= 2e-5 * max(1,abs(reference))`이고 유한해야 한다. 그래프 배리어 300개를 기록했으며 GPU validation을 켜고 종료 후 메시지 0개를 확인했다. 바꾼 CPU 기준은 별도 음성 검사에서 실제 비교 실패로 거절했다.

이 결과는 실제 GPU 수치 실행의 첫 증거다. 제품 Editor/Player에서 파티클을 재생했다는 뜻은 아니며 Release/Shipping GPU 실행이나 Vulkan, 픽셀·성능 수용은 포함하지 않는다. 기존 제품 포함/제외·reload·resize 회귀는 새 수용 기록에 구분해 보존한다.
