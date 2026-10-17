# RG8 큐 배치 계획 — 2026-10-08

RG7과 Q0는 종료 상태를 유지한다. RG8은 진행 중이며 기성은 0이다. 기존 제품 제출과 배리어는 단일 graphics 큐를 유지한다.

## 고정 구현 순서

1. 컴파일된 그래프의 큐 배치·대기 계획과 fallback 검증 (이번 단계).
2. Q0를 소비하는 큐별 기록·제출, COMMON release/acquire, 마지막 imported state 복구, 큐별 완료점까지 자원/allocator 보관. 순차 기록용 기존 배리어를 그대로 compute 큐에 보내지 않는다.
3. 기존 수용 기준으로 DX12 단일/다중 큐 픽셀 동일성, validation, GPU critical path 및 peak memory 비교 후 채택 여부 판정. 이득이 없으면 제품 기본값은 단일 큐 유지.

Vulkan 실행 수용은 PHASE 4.9이며 RG9 세부 범위와 별개다. 새로운 RG7 종료 조건을 추가하지 않는다.

## 이번 구현

`EnhancedRenderGraph::BuildQueueSchedule`은 Compile 뒤 살아남은 실행 순서를 사용하며 콜백을 실행하거나 GPU 제출을 하지 않는다. 출력에는 compile generation, 패스별 graphics/compute 배치, 큐 간 producer/consumer/resource 대기가 담긴다. 실패 시 출력을 비운다.

compute 선택은 ExplicitVersioned, graphics/compute/timeline 지원, 명시적인 compute-compatible 콜백 계약, 0보다 큰 GPU 비용 하한과 해당 하한 이상의 GPU 측정값을 모두 요구한다. CPU 배치 수인 recordCost를 GPU 비용으로 재사용하지 않는다. 측정값은 호출자가 제공하는 입력이며 이 함수 자체가 계측하거나 이득을 보장하지 않는다.

RAW/WAR/WAW 간선의 큐 간 대기를 보존하고, 같은 물리 저장소를 쓰는 패스에는 실행 순서대로 추가 대기를 둔다. 읽기끼리도 상태 전이와 소유권이 겹치지 않게 직렬화한다. 각 큐 안에서는 entries 순서를 보존해야 한다. 서로 다른 자원을 사용하는 패스는 이 추가 자원 대기로 직렬화하지 않는다.

힙 공유가 켜진 그래프는 전체 단일 큐 fallback이다. 현재 RG7 수명/alias 배리어가 하나의 실행 순서를 전제로 하므로, 큐별 완료 기반 수명 설계 전에 해당 힙을 병렬로 사용하지 않는다. Legacy/ExplicitSingleWriter, 기능 미지원, 측정값 부족, 호환 계약 부재도 기존 compiled order를 보존한다.

이 출력은 아직 실행용 barrier/submission plan이 아니다. native compute의 상태 제한, release/acquire, 최종 상태 epilogue와 완료점 연결은 2단계에서 구현한다. 제품 후보 패스에 임의 비용을 넣어 자동 활성화하지 않는다.

## 검증

기존 Editor의 `dx12.rendergraph queue-schedule`에 계획 검사 24개를 추가했다. `verify-rg8-queue-plan.ps1`은 `--smoke-offscreen`으로 이 검사와 종료만 실행하며 source/runtime 해시 및 종료 상태를 저장한다. 사용자 Editor UI를 점유하는 전체 회귀 명령은 이번 검사에 포함하지 않는다.

빌드와 실행 결과는 실제 산출물 확인 후 아래에 기록한다.

2026-10-09 첫 D/R 실행의 힙 공유 사례는 활성 owner recording 없이 Compile을 호출한 테스트 구성 오류로 실패했다. BeginFrame/AbortFrame을 명시한 동일 24검사 fixture로 수정했으며 실패 산출물은 RG8QueuePlan/Debug·Release에 보존한다. 최종 수용에는 DebugFinal·ReleaseFinal만 사용한다.

### 최종 결과 (2026-10-09)

- VS 2026(v18) Debug·Release 최종 빌드 통과.
- 기존 비표시 Editor의 24개 계획 검사 각각 통과, 종료 코드 각각 0.
- 검사 실행 시간 Debug 84.7158ms, Release 165.633ms. 이는 fixture 전체 실행 시간이며 스케줄러 성능/제품 GPU 시간으로 해석하지 않는다.
- 최종 소스/테스트/runtime 해시 일치 확인. 산출물 `Build/Verification/Phase43/RG8QueuePlan/{DebugFinal,ReleaseFinal}` 및 `plan-acceptance.json`.
- 변경 diff 공백 및 PowerShell 문법 검사 통과.

첫 단계 완료. RG8 전체 완료는 아니며 기성 0을 유지한다. 다음은 Q0 기반 실제 큐별 기록·COMMON 인계·제출·완료점 회수 연결이다. 제품 single queue와 RG7 기본 OFF 유지. 이번에 전체 RenderGraph GPU 회귀나 제품 성능 수용을 재실행한 것으로 보고하지 않는다.

2026-10-09 후속: [owned graph의 실제 큐 기록·제출·회수 연결](RenderRg8QueueExecution20261009.md)을 구현하고 D/R native 65·plan 24·전체 RG 회귀를 수용했다. 제품 frame 자원 회수 연결과 성능 수용은 남아 있다.
