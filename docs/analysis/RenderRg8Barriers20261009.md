# RG8 구조 수정 2 — 배치 내부 상태 유지와 공통 배리어 계획

[구조 수정 1](RenderRg8Batching20261009.md)의 배치를 유지하면서 패스마다 수행하던 COMMON 왕복을 제거한다.
RG8 progress·기성 0·기본 OFF는 유지한다.

## 변경 범위

- `SubmitQueues`의 독립적인 usage/state 해석과 반복 패스 실행을 제거했다.
  일반 경로의 `PlanBarriers`, `RecordPassBarriers`, `RecordPassBody`, `RecordPassFinalBarriers`를 사용한다.
- 단일 큐는 기존 compiled 계획을 그대로 사용하며 prologue/epilogue의 불필요한 COMMON 변환도 생략한다.
  다중 큐는 큐 배치가 확정되면 같은 계획기를 배치 경계로 특수화한다. 같은 배치의 연속 패스는 상태를 유지하며,
  UAV 쓰기 의존과 반복 phase의 최초/후속 반복 계획도 일반 경로의 규칙을 사용한다.
- 배치 마지막에서 사용한 자원을 COMMON으로 반환한다. prologue와 epilogue 및 큐 간 Wait는 유지한다.
  버퍼 final transition도 pass 진단에 포함하며, 경계·반복을 포함한 전체 배리어 수를 집계한다.
- 기록 전 특수화한 그래프는 실패해도 Reset 후 다시 작성해야 한다. GPU 제출 여부를 나타내는
  `submissionAttempted`와 그래프 재사용 금지를 구분해, 실패한 계획이 일반 실행기로 넘어가지 않게 했다.
- native 시험의 producer를 세 번 반복하는 UAV 쓰기로 바꿨다. 데이터 결과·반복 UAV 순서·배치 내부
  전이 수·기록 실패 후 일반 실행 거부를 확인한다. 기존 GPU 지연/격리/회수 검사는 유지한다.

다중 큐에서는 기본 컴파일의 barrier 계획 뒤 큐 배치에 맞춘 특수화가 한 번 더 실행된다. 계획 알고리즘은 공유하지만,
이 CPU 작업까지 제거한 것은 아니다. 첫/끝 배치 및 동일 큐의 별도 제출 경계에도 보수적인 COMMON 전이가 남는다.
최소 전이 계획이나 생산적인 compute overlap 달성을 주장하지 않는다.

## 경계 계약

DX12 buffer는 ExecuteCommandLists 완료 시 COMMON으로 decay한다. compute encoder의 ShaderResource는
NON_PIXEL_SHADER_RESOURCE로 변환되므로 graphics encoder의 전체 shader 상태와 그대로 연결할 수 없다.
이번 단계는 동일 제출 안의 중복을 제거하고 다중 큐의 별도 제출 간에는 기존 COMMON 계약을 보존한다.
[Microsoft의 상태·decay 규칙](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).

캡처 `compiledGraph.passes`는 pass 내부/끝 배리어를 담는다. 별도 prologue/epilogue는 pass가 아니므로,
`graph.barriers`와 비교할 때 다중 큐이면 used resource의 initial→COMMON 및 COMMON→final 항목을 더한다.
`audit-rg8-adoption.py --require-barriers`가 반복 횟수까지 반영해 두 수치의 일치를 확인한다.

## 검증

실행 증거 디렉터리: `Build/Verification/Phase43/RG8Barriers20261009`.
VS 2026 Release·Debug 최종 빌드 통과. 각 구성 native 실행 **150검사**, queue plan **24검사**,
전체 RenderGraph 회귀가 validation 오류 0·exit 0으로 통과했다(`NativeDebugFinal`, `NativeReleaseFinal`).
최종 Release Editor의 실제 Dynamic_CPP/TestShadow에서 단일·다중 큐 각각 GPU validation ON,
캡처 4표본을 실행했다. 두 프로세스 모두 layer enabled·problems 0·droppedMessages 0·exit 0이다
(`ProductValidationFinal`). 단일 큐 캡처는 110~114개, 다중 큐는 523개의 계획·기록 배리어 항목을 확인했다.
별도 프로젝트를 생성하지 않고 workspace 저장 위치만 각 실행의 증거 디렉터리로 분리했다.

중간 바이너리의 `ProductValidation/Forward-2`는 첫 GPU 검증 캡처가 180초 제한을 초과해 실패했다.
로그만으로 GPU 오류 여부를 판정할 수 없으므로 통과 증거로 사용하지 않는다. GPU 검증 실행의 명령 제한만
600초로 조정했고, 성능 측정의 180초 제한은 유지한다. 최종 바이너리의 재검증은 위와 같이 통과했다.
검증 레이어는 시작 안내 메시지에서 다수 PSO의 GPU 검증 shader 준비 때문에 첫 프레임에 수 분이 걸릴 수 있다고 명시했다.

## 같은 바이너리의 제품 비교

Release TestShadow 1496×692·7 draw, 정방향 0→1→2와 역방향 2→1→0의 독립 프로세스로 측정했다.
0은 기본 경로, 1은 owned 단일 큐, 2는 SSAO compute 큐다. RG7 aliasing OFF, GPU validation OFF이며
빌드·native 회귀·GPU validation 실행이 모두 끝난 뒤 측정했다.

- 6프로세스 × 8캡처, 입력 대조 및 **336개 출력 최대 오차 0**.
- 별도 6프로세스 × 32개의 일반 프레임, 같은 뷰·해상도·56 profiler slice, drop/overflow 0·exit 0.
- 큐 계획과 기록 통계 일치, batch 수와 native pair 생성/재사용 합계 일치, 종료 leased/cache 0.
- 단일 큐 3 batch, 다중 큐 warm 5 batch 유지. 단일 큐 6 pair 생성/1830~1857회 재사용,
  다중 큐 9~10 pair 생성/3004~3023회 재사용.
- 캡처의 과거 COMMON 실행 알고리즘을 동일 그래프에 대입하면 726개 요청.
  새 단일 큐는 **110~114개**, 다중 큐는 **523개**의 계획·기록 항목이다.
  이는 RHI 배리어 작업량 비교이며 드라이버가 낮춘 실제 native/GPU 배리어 개수의 계측은 아니다.

| 순서 | 경로 | 일반 GPU span 중앙값 (ms) | p95 (ms) |
|---|---|---:|---:|
| 정방향 | 기본 0 | 2.9896 | 3.5676 |
| 정방향 | 단일 큐 1 | 3.1186 | 3.3956 |
| 정방향 | 다중 큐 2 | 3.3270 | 3.9025 |
| 역방향 | 다중 큐 2 | 3.2384 | 3.7530 |
| 역방향 | 단일 큐 1 | 3.1037 | 3.4714 |
| 역방향 | 기본 0 | 0.9124 | 4.1165 |

**GPU 가속을 수용하지 않는다.** 정방향에서도 개선되지 않았고, 역방향 기본 경로는 앞 17표본이
약 0.89ms인 뒤 여러 패스의 시간이 함께 증가했다. 카메라 revision 2·1496×692·resize generation 2는
일정했지만 GPU 동작 클록/전력 상태는 수집하지 않았으므로 원인을 확정하거나 큐 구현의 영향으로 귀속하지 않는다.
이상 표본을 삭제하거나 과거 실행과 섞어 개선율을 만들지 않았다.

진단 캡처의 CPU record 중앙값은 정방향 기본 0.9035→단일 큐 0.6665ms,
역방향 0.8791→0.6736ms였다. readback/진단 패스 포함 값이므로 일반 프레임 CPU 개선으로 확대하지 않는다.
DXGI 연속 메모리 1773표본(일반 구간 834표본)은 부가 기록이며 정확한 physical VRAM peak 또는 메모리 절감 증거가 아니다.

증거: `NormalFinal/adoption-measurements.json`, `slice-result.json`.
`audit-rg8-adoption.py --require-pooling --require-barriers` 통과.
최종 D/R 소스 해시와 현재 소스 일치, Release 회귀·제품 GPU 검증·캡처·일반 측정의 바이너리 해시 일치:

- runtime SHA256: `2CAB929A909FA0F70F46E3E2E35288CC1B00D3C698FBCF5D866B2948D4D7A091`
- exe SHA256: `B88F346F3310919A2C52C0DB384885C59FA233AD65F3201FDB75F758F7241561`

## 판정과 다음 순서

독립 상태 계산 제거·단일 큐 compiled 계획 재사용·다중 큐 배치 내부 상태 유지의 구현/정확성 검증은 완료했다.
다중 큐는 여전히 기본 경로보다 훨씬 많은 보수적 경계 전이를 수행하며, TestShadow에 독립적인 작업을 만들지는 않았다.
다음은 **겹쳐 실행할 작업과 전환 비용을 기준으로 compute 사용을 선택하고, 실익 없는 그래프는 기본 경로로 제외**하는 것이다.
RG7 조합·보유 정책도 남아 있다. RG8 progress·기성 0·기본 OFF,
PHASE 4.3 기성 74·잔여 26, 전체 기성 126·잔여 437을 유지한다.
