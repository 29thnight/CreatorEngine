# RG8 구조 수정 1 — 큐 배치·기록 저장소 재사용

후속: [구조 수정 2](RenderRg8Barriers20261009.md)에서 단일 큐 compiled barrier 재사용과 다중 큐 배치 내부 상태 유지를 적용했다. 아래 수치는 수정 1 바이너리의 기록이다.

[구조 감사](RenderRg7Rg8StructuralAudit20261009.md)의 패스별 native allocator/list 생성과
제출 문제를 먼저 수정한다. RG8은 progress·기성 0·기본 OFF를 유지한다.
COMMON 왕복/상태 계획 단일화, overlap에 따른 후보 선택, RG7 조합은 이번 변경의 완료 범위가 아니다.

## 변경

- 같은 큐의 연속 패스를 하나의 command list로 기록한다. 교차 의존의 producer 뒤와
  consumer 앞은 끊어서 독립 작업이 불필요하게 대기하지 않게 한다.
- 같은 producer queue의 더 높은 timeline 값이 낮은 값을 포함한다는 계약으로 중복/지배된
  Wait를 제거한다. 각 패스의 완료점은 소속 batch의 완료점으로 게시한다.
- DX12 queue endpoint가 기록 저장소 풀을 소유한다. DIRECT와 COMPUTE 및 서로 다른 queue/device의
  저장소를 섞지 않는다. 기존 DIRECT 전용 frame/worker pool을 COMPUTE 큐와 억지로 공유하지 않는다.
- GPU 완료 후 pending 소유권과 호출자 소유권이 모두 해제된 저장소만 allocator/list Reset에 재사용한다.
  캐시는 native allocator/list만 보관하며 graph/descriptor/resource 소유권을 보관하지 않는다.
  큐당 반환 저장소 32개를 상한으로 두고 나머지는 해제한다.
- callback/Close 실패한 저장소는 캐시하지 않는다. 제출 후 Signal 실패로 격리된 batch는
  검증된 shutdown까지 대여 상태를 유지하며, shutdown은 캐시 재진입을 닫고 보유 저장소를 해제한다.
- 제품 종료 로그에 created/reused/leased/cached를 기록한다. 단순 GPU 시간뿐 아니라
  실제 native 저장소 생성과 batch 수 감소를 검증한다.

이 단계는 native 생성/제출 비용 수정이다. graphics 기록의 기존 worker 분할을 owned 실행기와
통합하거나 compiled barrier 계획을 통합한 단계는 아니다.

## 검사

기존 Editor의 queue execution 검사에 다음을 추가했다.

- 지연 GPU의 저장소가 반환되지 않는지 확인.
- 완료된 저장소 재사용에서 생성 수가 증가하지 않는지 확인.
- GPU가 완료돼도 호출자가 batch를 잡고 있으면 독점 대여가 유지되는지 확인.
- Signal 실패 격리와 shutdown 이후 leased/cache 해제를 확인.
- 교차 의존 전후의 독립 패스를 추가해 batch 병합이 대기 경계를 넘지 않는지 확인.

VS 2026 Debug·Release 빌드와 각 구성 native/frame/profiler **147검사**, queue plan **24검사** 및 전체 RenderGraph 회귀가
GPU validation 오류 0·exit 0으로 통과했다. 별도 테스트 프로젝트를 만들지 않고 기존 Editor를 사용했다.
증거: `Build/Verification/Phase43/RG8Batching20261009/NativeDebug`, `NativeRelease`.

## TestShadow 제품 결과

동일 Release 바이너리·1496×692·7 draw, 정방향 0→1→2 / 역방향 2→1→0으로
각각 독립 Editor를 실행했다. 모드 0은 기존 기본 경로, 1은 owned 단일 큐, 2는 SSAO compute 큐다.
RG7 aliasing은 OFF다. 캡처 6프로세스 × 8표본의 입력을 대조하고 336개 출력을 비교해 최대 오차 0을 확인했다.
별도 일반 프레임 6프로세스 × 32표본은 캡처 패스 없이 같은 뷰·해상도·56개 profiler slice를 확인했다.
제품 성능 실행은 validation OFF이며, validation 0은 위 D/R 회귀의 결과다. 12프로세스 모두 exit 0.

| 순서 | 경로 | 일반 GPU span 중앙값 / p95 (ms) | 그래프 / batch | native pair 생성 / 재사용 |
|---|---|---:|---:|---:|
| 정방향 | 기본 0 | 2.3946 / 2.7986 | 기존 경로 | 기존 경로 |
| 정방향 | 단일 큐 1 | 2.5027 / 2.7126 | 627 / 1881 | 6 / 1875 |
| 정방향 | 다중 큐 2 | 2.6947 / 3.1191 | 618 / 3088 | 10 / 3078 |
| 역방향 | 다중 큐 2 | 2.8954 / 3.1263 | 598 / 2988 | 10 / 2978 |
| 역방향 | 단일 큐 1 | 2.7039 / 3.1611 | 618 / 1854 | 6 / 1848 |
| 역방향 | 기본 0 | 3.1278 / 3.5205 | 기존 경로 | 기존 경로 |

기존 RG8 실행 알고리즘의 일반 56패스+prologue/epilogue **58 batch**가 단일 큐 **3**,
다중 큐 **5**로 감소했다. 다중 큐 첫 워밍업 그래프는 compute 후보가 아직 없어 3이며 이후 5다.
캡처의 기존 74 batch도 다중 큐 5로 감소했다. 각 프로세스에서 `created + reused == batches`,
`batches == graphs * 3 + computeBatches * 2`를 확인했고 종료 시 leased/cache 모두 0이다.
각 compute pass profiler slice는 유지하므로 2개 SSAO pass를 1개 compute batch로 제출해도 계측은 분리된다.

**생성·제출 작업량 감소는 확인했다. 기본 경로 대비 GPU 가속은 확인하지 못했다.**
정방향에서는 수정 경로가 느리고 역방향에서는 빠르므로 환경/시간 경과의 영향을 배제할 수 없다.
이전 5~10ms대 RG8 실행과 이번 2.5~2.9ms를 순수 코드 개선율로 계산하지 않는다.
또한 이 배치 수정이 TestShadow에 graphics/compute 독립 작업을 새로 만들지는 않는다.
진단 캡처 CPU record는 readback/진단 패스가 포함되므로 일반 프레임 CPU 성능으로 해석하지 않는다.

DXGI 연속 메모리 1836표본(일반 측정 구간 839표본)을 수집했다.
이는 장치 예산 사용량 표본이며 정확한 물리 VRAM peak 또는 이번 allocator 풀의 고유 메모리 비용이 아니다.
메모리 절감이나 제품 전면 채택을 주장하지 않는다.

검증 스크립트: `Tools/regression/audit-rg8-adoption.py`의 `--require-pooling` 검사 통과.
수치/분포: `Build/Verification/Phase43/RG8Batching20261009/Normal/adoption-measurements.json`.
캡처/일반 측정/Release 회귀의 exe 및 runtime SHA256 일치:

- runtime: `B9C64402537630AD91B74342571A5420F8A02EF425003C05962E2977B0A9C703`
- exe: `B88F346F3310919A2C52C0DB384885C59FA233AD65F3201FDB75F758F7241561`

## 다음 수정

1. 패스마다 COMMON으로 왕복하는 실행 상태와 compiled barrier 계획을 단일화한다.
2. 실제 독립 작업과 전환 비용을 기준으로 compute 후보를 선택한다. 이득이 없는 그래프는 기존 경로를 사용한다.
3. RG7 조합의 수명 계약과 캐시 보유 정책을 해결한다.

이번 수정은 첫 번째 구조 결함 묶음에 대한 완료다. RG8 전체는 progress·기성 0·기본 OFF,
PHASE 4.3 기성 74·잔여 26, 전체 기성 126·잔여 437을 유지한다.
