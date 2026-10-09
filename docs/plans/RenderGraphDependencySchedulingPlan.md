# RenderGraph 리소스 의존성 스케줄링 계획 (PHASE 4.3 · 트랙 RG)

2026-10-09 구조 수정 2: 단일 큐는 compiled barrier 계획을 재사용하고, 다중 큐는 공통 계획기로 배치 내부 상태를 유지한다. D/R 실행 150검사·계획 24검사·전체 RG 회귀 통과. [제품 검증과 잔여 범위](../analysis/RenderRg8Barriers20261009.md). RG8 progress·기성 0·기본 OFF와 PHASE 4.3 기성 74·잔여 26을 유지한다.

2026-10-09 구조 수정 1: 패스별 제출을 의존 경계별 batch로 합치고 queue endpoint 소유 allocator/list 풀을 추가했다. D/R 실행 147검사·계획 24검사·전체 RenderGraph 회귀 통과. COMMON 전이 단일화·overlap 후보 선택·RG7 조합은 잔여이며 RG8 progress·기성 0·기본 OFF를 유지한다. [수정과 제품 검증](../analysis/RenderRg8Batching20261009.md).

2026-10-09 후속 구조 재감사: RG8 제품 완료/기성 16 회수를 철회한다. 실제 SSAO 큐 배치는 overlap이 없고, 기존 풀 대신 패스별 allocator/list 생성·제출/Signal 및 COMMON 왕복을 수행한다. RG7 조합도 거부하므로 원래 queue pool·수명/메모리 계획을 충족한 제품 실행기가 아니다. RG8 progress·기성 0·기본 OFF, PHASE 4.3 기성 74·잔여 26. [감사와 수정 순서](../analysis/RenderRg7Rg8StructuralAudit20261009.md). RG7의 제한된 aliasing 구현/실험 종료는 유지한다. 아래 종료 기록은 재감사 전 이력이다.

2026-10-09 RG8 종료: 기존 D/R 큐 실행·수명·제품 SSAO compute 수용에 더해 Release TestShadow 1496×692의 336출력 오차 0·일반 GPU 192프레임·연속 메모리 1846표본과 최신 Release 전체 RG 회귀를 확인했다. GPU span은 두 순서 모두 기본 경로보다 증가했고 메모리 절감은 일관되지 않아 기본 OFF로 판정했다. RG8 done·기성 16, PHASE 4.3 기성 90·잔여 RG9 10인일. [최종 근거](../analysis/RenderRg8Closure20261009.md). 아래 날짜별 기록은 당시 상태다.

## 2026-10-07 RG6 제품 수용 완료

**수용 뒤 후속 변경:** live 재질 fallback 제거로 Shadow/GBuffer는 타깃 초기화만 수행하고 graph가 재질을 기록한다. 준비 중/실패 요청에 이전 instance를 선택하지 않는다. 이 절의 RG6 전후 비교는 보관된 수용 소스 해시에 한정되며, 후속 변경의 검사 결과는 [graph-only 재질 패스 기록](../analysis/GraphOnlyMaterialPasses20261007.md)을 따른다. CSM depth-content cache 수용이나 RG6 성능 전후 비교 재측정으로 확대하지 않는다.

**RG6 done·기성 4인일 회수.** 동일 현재 소스의 별도 DeclarationOrder 진단 빌드와 제품 ExplicitVersioned/DependencyOrder를 D/R 각각 독립 2프로세스로 실행했다. 총 408 accepted capture, 구성/정책별 동일 입력 100회 compiled order/dependency hash 결정성, final 색상 오차 0·깊이 기존 오차 기준 통과·16개 첨부/단계, CPU record/pass GPU/compile/graph 통계, GPU validation 0·drop 0·8프로세스 정상 exit 0을 확인했다. 기본 제품 두 live 생성자는 버전 의존성 경로이고 런타임 legacy 스위치가 없다. reference는 명시 진단 빌드 옵션에서만 활성화된다.

reference 59 pass/51 resource, 제품 65 pass/59 resource의 차이는 모드에 따른 Geometry Occlusion 및 icon 명시 read다. 비용을 순수 스케줄러 overhead나 역사적 전체 PR 효과로 귀속하지 않는다. CPU/GPU 분포와 깊이 readback 버전 차이는 [RG6 수용 보고서](../analysis/RenderRg6Acceptance20261007.md)에 기록했다. 통신 소켓 실패를 제외하고 하네스 수정 후 v2 전체 재검증 및 현재 기본 D/R 공동 BASE-0 phaseComplete를 확인했다. RG-V UI·MAT-9 품질/성능·Vulkan 4.9는 별도다. 아래 PR 감사/이전 날짜 기록은 수용 전 이력이며 이 절이 최신 판정이다.


## 2026-10-07 RG5 종결 검사 후속

현재 Debug/Release `dx12.rendergraph`·`dx12.decal`은 GPU validation 활성/문제 0, 정상 종료 exit 0으로 통과했다. 두 검사의 visibility 준비 누락을 수정했고, 현재 셰이더를 갖춘 독립 fixture에서 재검증했다. Decal의 구식 3패스 단정은 지원 장치의 reset/cull을 포함한 5패스·후보 1개로 보완했다. 아래 10월 6일 예외 기록은 해결 전 역사이며 현재 차단 사유가 아니다.

**RG5 완료·기성 10인일 회수.** IblBakeResult·SurfaceBatch·RasterSurfaceBatch의 명시 Read/Write·생산 버전 반환은 D/R 3정책·일반 63 frames·공유 depth 72 frames로 수용했다. 최종 Fog/PostChain/UI/Editor는 D/R 각각 3정책·43 fixture/129 frames·9단계 오차 0, 개별 기능 검사·GPU validation 0·exit 0으로 수용했다. 실제 Editor도 D/R 각각 독립 2프로세스×2캡처, 같은/독립 프로세스·구성 간 16개 이미지 오차 0 및 현재 소스/실행 파일 해시 계약을 통과했다. 선언 목록 제품 69·fixture 253과 명시 경로의 임시 추론 adapter 잔여 0을 감사했다. 의도한 legacy 비교 fixture/호환 분기는 보존한다. 제품 live 두 생성자는 ExplicitVersioned/DependencyOrder다. [최종 수용 보고서](../analysis/RenderRg5FinalAcceptance20261007.md), [reference 이관 보고서](../analysis/RenderRg5ReferenceMigration20261007.md). RG6는 위 후속 절에서 별도 수용했으며 RG-V UI 수용은 남는다.

검사·수정·증거 범위: [RG5 종결 검사 보고서](../analysis/RenderRg5Closure20261007.md).


## 2026-10-07 최근 병합 반영

PR #122(`33f44d6c`)의 RG6 ExplicitVersioned/DependencyOrder 및 RG-V immutable snapshot/UI 소스를 현재 HEAD에서 확인했다. 따라서 대시보드의 RG6·RG-V를 todo에서 **progress(earnedDays=0)**로 정정한다. 이는 아래 10월 6일 기록의 완료/기성 불변 원칙을 유지하면서 구현 착수 표기만 바로잡는 것이다.

기존 dx12.rendergraph·dx12.decal 예외와 RG5 선언/adapter·최종 조합·capture/fixture 수용은 후속 검사로 닫았다. 다음은 RG6 별도 프로세스 전환 전후 픽셀·validation·CPU/GPU 증거, RG-V 실제 generation·resize·stale/missing-edge·UI/비용 검사다. RG7~RG9/Q0를 이 PR들의 완료 범위로 추가하지 않는다.

근거: [10월 4~7일 PR 적용 감사](../analysis/MergedPrReview20261007.md). 아래 과거 날짜의 검증 기록은 해당 시점의 증거이며 최신 HEAD의 통과를 뜻하지 않는다.

**2026-10-01 사용자 결정:** 이 페이즈의 실행·픽셀·성능 완료 판정은 DX12 Debug/Release다. RHI 중립 계약과 필요한 backend 구현은 유지한다. Vulkan 실행 비교·동등성·교차 픽셀 수용은 [PHASE 4.9](BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교가 단독 소유하며 이 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.


2026-08-28 작성. `EnhancedRenderGraph`를 교체하지 않고, 명시적 리소스 접근과
버전 계보로 실행 순서를 컴파일하는 그래프로 단계적으로 확장하는 구현 계획이다.

상태: **2026-10-05 BASE-0·RG1~RG4 및 RG5-1~RG5-12의 기존 수용 기록 유지. 요청 범위의 RG5 후속·RG6·RG-V 소스 구현 완료·동결, 영역별 및 전체 diff 독립 최종 정적 검토 완료. 신규 수용 검증 전부 미실행. RG7~RG9·Q0 미착수.** 소스 구현 완료는 빌드·실행·픽셀·성능 수용 완료를 뜻하지 않는다. 2026-10-06 PR #122(`33f44d6c`)로 master 병합, 병합 뒤 첫 실행 결과는 아래 2026-10-06 절을 따른다.

## 2026-10-06 병합 뒤 첫 실행 — 성능 회귀 하나 수정, 붉은 검사 남음

PR #122 로 제품 일정이 `ExplicitVersioned` + `DependencyOrder` 로 바뀌면서 RG4 의 의존 차례
병렬 기록이 제품에서 처음 켜졌다. 의존이 줄줄이 이어진 제품 그래프는 패스 37개에 차례 33개가
생겼고, 차례마다 워커를 깨워 합류시키는 비용 때문에 명령 기록이 프레임당 0.33 → 2.45 ms,
무제한 렌더가 초당 994 → 315장으로 떨어졌다(GPU 시간 0.23 ms 는 같음).
`ef829373` 이 워커 하나만 일하는 연속 차례를 Job 하나로 묶어 Release 같은 장면 초당 688장으로
되돌렸다. 전환 전 994장에는 아직 못 미치며 남은 차이는 측정하지 않았다. 같은 시점 DX12 자가 검증
28종 중 26종 통과, `dx12.ibl`·`dx12.scene` 실패는 병합 전부터 같았다.

PR #123(`5ca4e82a`)은 그래프 상태 어휘에 버퍼 읽기 전용 `VertexAndShaderResource` 를 더하고
(`IndirectArgument` 와 같은 규칙: 버퍼·읽기 전용, 가져온 최종 상태로는 금지) GPU 가시성 자원과
간접 인자 전이를 그래프에 올렸다. 그 결과 **`dx12.rendergraph`·`dx12.decal` 은 패스를 직접 몰면서
GPU 가시성 준비가 없어 예외로 끝나며 아직 고치지 않았다**(병합 기록). 이 페이즈의 검사가 붉은
상태이므로 RG5·RG6 수용 전에 먼저 고친다.

RG6 완료선(같은 밀봉 입력의 별도 프로세스 전후 픽셀·검증 0·CPU/GPU 계측 산출물)은 여전히
실행하지 않았다. 위 초당 장수는 전환 수용 산출물이 아니다. RG5·RG6·RG-V 상태와 기성은 바꾸지 않는다.

## 2026-10-05 소스 구현 동결·독립 최종 정적 검토 완료 — 실행 수용과 분리

이번 작업은 GPU-driven geometry와 Unreal RDG의 명시적 의존성·수명 원칙을 참고한
runtime lookup/조명 분리를 같은 `EnhancedRenderGraph`에 연결하는 코드 이관이다.
요청 범위의 소스 구현과 영역별·전체 diff 독립 최종 정적 검토는 끝났다.
(2026-10-06 PR #122 로 병합됨.) 이 문서는
소스·정적 검토 범위를 기록하며 실행 수용 완료를 선언하지 않는다.
빌드·테스트·renderer 실행·capture·GPU validation·성능 측정은 사용자 요청에 따라 실행하지 않는다.
기존 `BASE-0`·`RG1~RG4`·`RG5-1~RG5-12` 증거는 해당 소스의 역사적 수용 기록이며,
이번 변경의 통과 증거로 재사용하지 않는다. 후속 RG5·RG6·RG-V의 완료 판정과 기성은 올리지 않는다.

| 영역 | 현행 코드 진행 | 남은 확인·경계 |
|---|---|---|
| RG5 후속 제품 선언 | VolumetricFog·PostChain·UI·Editor Grid/WireFrame/GizmoIcon/GizmoLine의 Read/Write/Modify 및 반환 출력 배선, Decal 중복 읽기 정리 | 전체 제품·test/fixture 접근 감사, 임시 추론·adapter 잔여와 실제 versioned GPU 수용은 미판정 |
| RG6 제품 전환 | SceneRenderer의 `ExplicitVersioned` + `DependencyOrder`, 최신 최종 출력·capture 소비 배선 | 현행 Debug/Release 빌드, 별도 프로세스 live 전후 픽셀·validation·정상 종료·CPU/GPU 계측 미실행 |
| RG-V·graph 진단 | compile generation·dependency hash, immutable snapshot reader/viewer, PBR capture의 compiledGraph schema 3(바깥 manifest schema 1 유지), imported final-state를 마지막 소비 뒤 복구하는 배선 | Scene/Game/Material Preview·resize·세대 교체 일치, stale/missing-edge 변이, viewer 비용·메모리·UI 검증 미실행 |
| GPU-driven geometry | backend-neutral 단일 indexed-indirect, reset→frustum cull·atomic compaction→간접 인자 생성 및 Enhanced/LX GBuffer 연결. static opaque/masked 대상, CPU material/PSO bin과 LX per-geometry bin 유지. encoder는 capability·usage·offset/range를 검사하고 graph가 `IndirectArgument` 전이를 소유 | 선택적 화면 밖 추가 후보를 기존 draw 예산으로 제한하고 확장된 Graph Seal 실패 시 원래 visible/caster 집합으로 재시도. skinned/custom·미지원 direct fallback 및 기존 Seal 예산 검사를 유지하며 native 현재 state 검증·실제 draw/픽셀·성능 수용은 주장하지 않음 |
| runtime lookup·조명 | 사용자 승인에 따라 DX12·Vulkan live 경로 모두 split-sum으로 통일, GPU scene/material 입력 재사용. 기본 live에서 320 B/pixel의 전체 화면 lookup bake 저장소 할당 제거 | 정밀 적분과 기존 approximate-cache API는 bake/reference/diagnostics에 보존. 과거 DX12 정밀 경로와 픽셀 동일성·실제 4K 전체 peak 메모리는 미판정. Scene host ABI 16으로 해당 재질 프로그램 재생성 필요 |
| 특수 재질 | 264×264의 ordered 14-RGBA32F + D32 scratch, float32 D32 tile-depth 초기화·4-pixel halo SSS·전역 HDR/depth ray 입력. 특수 stream마다 repeated graph node 1개와 compiler-planned phase 4개로 타일 반복 | 타일마다 graph node를 늘리지 않음. full-screen reference 경로 보존. 4K 메모리·타일 경계·SSS/투과 품질·성능 수용 미실행 |
| 제출·자원 수명 | reserved/admitted/native-confirmed/GPU-complete/rejected 구분, never-admitted만 정확한 예약 취소, producer 측 token 소멸 | native 실행 불명확 시 소유권 유지·일반 제출 차단. capture/resize/shutdown 공통 proof gate의 실행 검증 미실행 |

화면 밖 추가 후보의 상한은 `SceneInputBudget.draws`(현재 4096)이며 원래 visible/caster
집합의 예산을 확대하지 않는다. 확장 입력의 Seal 실패 시 해당 원래 집합과 대응 fallback
소유자로 재시도하고, 재시도 역시 기존 draw·geometry·payload 예산 검사를 통과해야 한다.
기본 live에서 제거한 320 B/pixel은 11×RGBA32F 입력 176 B와 `IblBakeSample` 출력 144 B의
합계다. 현재 live는 capture 입력을 끄고 sample 레코드 1개만 남기며, reference·명시적 capture와
특수 재질 scratch/snapshot까지 없애거나 실제 전체 VRAM을 측정했다는 뜻은 아니다.

메모리 수치는 **소스의 정적 산술이며 측정 peak VRAM이 아니다.** 타일 payload는
15,890,688 bytes(약 15.15 MiB), ledger charge는 16.0625 MiB에 타일별 상수를 더한다
(3840×2160에서 135×256 bytes). 굴절의 전역 HDR+depth snapshot ledger charge는 같은
4K에서 95 MiB다. native driver 계상은 backend가 소유한다. 일반 budget/rejection은
nonfatal이다. 드문 강제 소멸에서 GPU idle과 실제 device-loss 입증이 모두 실패하고 유지할
owner도 없는 경우에만 기존 fatal invariant가 적용될 수 있다. 세부 수명 경계는 아래 전환 기록을 따른다.

세부 소스 범위와 잔여 검증은 [GPU-driven 전환 기록](../analysis/GpuDrivenRenderGraphTransition.md)을 따른다.
이 절은 기존 슬라이스 번호·종료 게이트를 바꾸지 않으며 RG7 aliasing, async queue,
Mesh Shader, DXR, WorkGraph, compiled-plan/shadow cache를 이번 작업에 포함하지 않는다.

---

## 0. 결정 요약

**2026-10-02 BASE-0 범위 정리:** 현재 DX12 Debug/Release 바이너리·고정 대표 장면의 이미지,
graph 진단·변이, capture 계측·GPU validation·정상 종료와 구성 간 대조가 완료 조건이다.
범용 frame packet·재질/애니메이션 재생은 선택 확장으로 분리하며 RG1 선행이 아니다.
2026-10-03 현행 Debug/Release 기준선과 구성 간 16개 이미지 maxError=0, 현재 해시 및 `phaseComplete=true`를
회수했다. [현재 완료 계약과 증거](../analysis/RenderBase0Baseline.md)를 따른다. RG1 이후 현행 기준선과 변경 전후 증거는 [RG1 검증 기록](../analysis/RenderRg1Scheduling.md)에 남긴다. RG2도 완료했으며 [버전·Modify 검증 기록](../analysis/RenderRg2Versions.md)에 현행 기준선을 남긴다. RG3도 완료했으며 [컬링·수명·배리어 검증 기록](../analysis/RenderRg3LifetimeBarriers.md)에 현행 기준선을 남긴다. RG4도 완료했으며 [wave·병렬 기록 검증 기록](../analysis/RenderRg4RecordingWaves.md)에 현행 기준선을 남긴다. 다음 구현은 RG5다.

2026-10-03 RG1 구현 경계: 같은 `EnhancedRenderGraph` 생성자의 `ExplicitSingleWriter` 모드에서
`RGPassUsage.access`를 반드시 명시한다. 최소 authored index를 먼저 꺼내는 안정적 Kahn 정렬이며
순환 오류에는 실제 패스·자원 경로를 기록한다. imported Read는 writer가 없으면 외부 초기값을 소비하고,
writer가 있으면 그 결과를 소비한다. ReadWrite는 initialized imported 자원의 단독 사용만 허용하며
다른 reader나 writer가 있으면 RG2 version/Modify를 요구한다. 제품 기본 모드는 RG5 이관 전까지
`DeclarationOrder`이며 명시적 접근과 암묵 접근을 혼합하지 않는다. 이는 별도 실행 그래프가 아닌 같은
Compile/Execute 경로의 선언 계약 선택이다. Debug/Release 빌드·24 shuffle native fixture·역순 RAW GPU 픽셀 검사 및 제품 변경 전후/구성 간 16개 이미지 maxError=0을 회수했다. RG1 완료이며 제품 이관은 RG5/6에 남긴다.

2026-10-01 착수 판정: BASE-0은 구현·검증된 MAT-0~MAT-8 기반을 소비하며 MAT-9 최종 수용을 기다리지 않는다. 현재 IBL 1024/4096과 이미지 수용 상한을 유지하고, 기존 SSS·투과 품질 미달 및 이동 카메라 성능 미수용을 baseline의 알려진 공백으로 기록한다. 구조 변경의 회귀/재현 통과와 MAT-9 최종 품질/성능 수용은 별도 판정이다. RG6 뒤 필요한 GPU-driven/IBL 재사용 구현·실측을 거쳐 MAT-9 성능 게이트를 회수한다. 하네스·graph snapshot·capture 제출별 GPU 계측·변이 게이트를 구현했으며 DX12 Debug/Release의 재현·변이·계측 검증으로 판정한다. Vulkan 교차 비교는 PHASE 4.9로 이관하여 BASE-0의 실패/선행 조건에서 제거한다.

1. **실행 그래프는 `EnhancedRenderGraph` 하나만 유지한다.** 별도 FrameGraph나 제품용
   이중 실행 경로를 만들지 않고 현재 Compile/Execute 경계를 확장한다.
2. **C# Pipeline IR의 Pass 목록은 저작 순서다.** 실제 실행 순서는 선언한 리소스
   의존성에서 계산하고, 서로 독립인 Pass만 저작 index를 안정 tie-break로 사용한다.
3. **DAG 정렬을 먼저 연다.** `Read`, `Write`, `ReadWrite`와 `RHIResourceState`를 분리한
   뒤 첫 DAG는 단일 writer 리소스와 명시적 ordering token만 받는다. 다중 writer와
   `ReadWrite` 연쇄는 추측하지 않고 오류로 거부한다. 다음 슬라이스에서 버전 핸들을
   도입해 `Write`/`Modify`의 연쇄를 연다.
4. **단일 그래픽 큐를 먼저 완결한다.** 단일 writer DAG → 버전/Modify DAG → 기존 파이프라인 이관 →
   DX12 live 전후 픽셀 게이트를 닫기 전에는 aliasing과 async compute를 열지 않는다.
5. **그 뒤에 메모리와 큐 최적화를 연다.** transient buffer/aliasing을 먼저, 실제 GPU
   겹침 이득을 계측한 뒤 multi-queue/async compute를 도입한다.
6. **빅뱅 전환을 금지한다.** 각 슬라이스는 독립 검사와 A/B 스위치를 가지며, 다음
   슬라이스는 직전 acceptance gate가 통과한 뒤 시작한다.

이 순서는 현재 코드가 위상 정렬을 포기한 이유를 무시하지 않는다. 현재의 unversioned
handle과 state 기반 쓰기 추론만으로는 두 writer의 선후를 알 수 없다. RG1에서 명시적
접근과 **단일 writer DAG**를 먼저 닫고, 모호한 두 writer는 fail-closed한다. RG2에서
버전 계보를 도입한 뒤에만 다중 writer를 스케줄한다.

---

## 1. 현재 코드 기준선

2026-10-01 워킹트리 정적 감사 기준이다. BASE-0은 현행 계약을 관측하며 DAG 스케줄러 구현은 RG1~RG2에 남긴다.

| 영역 | 현재 보유 | 자동 의존성 실행까지 남은 공백 |
|---|---|---|
| 그래프 생명주기 | `Compile`/`Execute`, pass culling, transient texture pool, lifetime, Transition/UAV 배리어, 병렬 기록 | compile 결과가 리소스 DAG가 아니라 선언 순서 |
| 실행 순서 | `BuildOrder`가 read-before-write를 검사한 뒤 선언 index를 그대로 `m_executeOrder`에 적재 | edge table, stable topological sort, cycle/모호한 writer 진단 없음 |
| 리소스 핸들 | `RGHandle { index }`, texture/buffer import, transient texture 생성 | 논리 resource ID와 version 분리 없음, transient buffer 생성 없음 |
| Pass 사용 | `RGPassUsage { handle, RHIResourceState }` | `IsWriteState`로 쓰기를 추론하므로 read와 read-modify-write를 구분할 수 없음 |
| 서브리소스 | 텍스처 전체 단위 상태와 수명 | mip/array/range별 접근·배리어·alias 판단 없음 |
| 큐 | DX12 `DIRECT` 큐 하나, Vulkan graphics family/queue 하나 | compute queue, queue ownership transfer, cross-queue fence 없음 |
| 제품 표면 | 기본 live pipeline 15개 노드 + Editor 기여 4개, `.cpp`/`.h`의 `.AddPass`/`->AddPass`·`AddSplitPass` 정적 호출 183곳(Engine 52, Editor 제품 4, Editor test 98, Tools 29; BASE-0 capture·film upload fixture 포함) | 모든 생산 Pass의 RHI 중립 접근 선언 이관과 DX12 제품 cutover 필요 |

근거 위치:

- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.h:40`, `:50`, `:286`, `:296`
- `Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp:12`, `:131`, `:192`, `:497`
- `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp` (`BuildPipelineDesc`, DX12/Vulkan 제품 캡처 경로)
- `Editor/EngineEntry/EditorSceneOverlayContributor.cpp:82`
- `Engine/RenderEngine/RHI/DX12/DX12DeviceResources.cpp:295`
- `Engine/RenderEngine/RHI/Vulkan/VulkanDeviceResources.cpp:390`, `:451`, `:521`

호출 수는 구현 착수 직전 다시 센다. 이 표의 수치는 일정 산정용 기준선이며 완료 판정
자체가 아니다.

---

## 2. 목표 계약

### 2.1 리소스와 버전

이 절은 **RG2 이후의 최종 계약**이다. RG1의 첫 DAG는 명시적 접근을 요구하되
단일 writer만 허용하며 같은 리소스의 추가 `Write`/`ReadWrite`를 실패시킨다.

```text
RGResourceId = 프레임 안에서 동일한 논리 texture/buffer의 정체성
RGVersion    = 그 논리 리소스에 새 내용이 발행될 때 증가하는 버전
RGHandle     = (resourceId, version, kind)

Read(vN, state)      -> vN의 producer에 의존
Write(vN, state)     -> vN+1을 발행, vN을 읽지는 않음
Modify(vN, state)    -> vN을 읽고 vN+1을 발행
```

- import는 외부 내용이 있는 `v0`에서 시작한다.
- transient는 최초 writer가 `v0`을 발행하기 전에는 읽을 수 없다.
- 같은 입력 버전에서 두 writer가 갈라지는 forked write는 compile 오류다. 호출 순서를
  암묵적 답으로 쓰지 않고 어느 출력이 다음 버전인지 선언을 고치게 한다.
- `RGAccessMode { Read, Write, ReadWrite }`와 요구 `RHIResourceState`를 별도 필드로 둔다.
- texture와 buffer가 같은 버전·접근 API를 사용하되 descriptor와 실제 handle 형식은
  타입 안전하게 유지한다.

### 2.2 의존 edge

- **RAW:** `vN` producer → `vN` reader.
- **WAW:** 같은 논리 리소스의 `vN` producer → `vN+1` writer.
- **WAR:** `vN` readers → 물리 저장소를 덮어쓸 수 있는 `vN+1` writer.
- side effect와 외부 출력은 명시적 root/ordering token으로 모델링한다. 이름이나 우연한
  등록 위치로 순서를 만들지 않는다.
- 독립 Pass는 저작 index가 작은 순서로 꺼내는 stable Kahn sort를 사용한다. 이 규칙으로
  graph dump, 픽셀 fixture와 디버깅 재현성을 유지한다.

### 2.3 Compile 순서

```text
접근·버전 검증
  → producer/consumer edge 구성
  → side-effect/output root에서 역방향 culling
  → 살아남은 DAG의 stable topological sort
  → 정렬된 순서에서 lifetime 계산
  → transient 할당/alias 계획
  → barrier와 record wave 계획
  → immutable compiled graph 실행
```

cycle 오류는 최소한 `Pass → ResourceVersion → Pass` 사슬을 출력한다. missing producer,
forked write, read/write 중복 선언, 범위를 벗어난 handle도 GPU 실행 전에 Pass·리소스·버전을
함께 지목한다.

### 2.4 저작 순서와 실행 순서

C# Pipeline IR은 사용자가 이해하고 diff할 수 있는 authored Pass 목록을 보존한다.
Compiler가 각 슬롯의 버전 핸들을 연결한 뒤에는 다음처럼 해석한다.

- 의존성이 있는 Pass: 리소스 edge가 실행 선후를 결정한다.
- 독립 Pass: Stack 순서가 deterministic tie-break다.
- 화면 표시와 IR serialization: authored 순서를 유지한다.
- 실행 미리보기: compiled order와 원래 authored index를 함께 표시한다.

---

## 3. 구현 순서와 공수

1인 전담 엔지니어 기준 개발일이다. 병렬 인원 투입 시에도 RG1→RG6 임계 경로는 줄지
않으며, backend 안정화와 픽셀 판정 시간은 별도로 필요하다.

| ID | 슬라이스 | 선행 | 공수 | 종료 게이트 |
|---|---|---:|---:|---|
| ~~**RG0**~~ | **`BASE-0`에 흡수** — 4-0·SRP-G0와 같은 하네스·같은 artifact였다. graph dump와 변이 fixture는 `BASE-0`의 소비 항목으로 남는다 | 없음 | (BASE-0 4일에 포함) | [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md) §6.1 |
| **RG1** | 명시적 access mode + 단일 writer stable DAG compiler | BASE-0 | 8일 | RAW, 독립 Pass tie-break, cycle chain, 선언 배열 shuffle fixture; 다중 writer와 모호한 ReadWrite 연쇄는 명시적 오류 |
| **RG2** | versioned texture/buffer handle + 다중 writer/Modify DAG | RG1 | 10일 | `Write`/`Modify` 새 version, WAR/WAW, import/transient version dump, forked write·stale handle fail-closed |
| **RG3** | DAG 기준 culling·lifetime·barrier 재계산 | RG2 | 8일 | 죽은 producer 제거, 마지막 소비 수명, Transition/UAV 계획이 sorted order 기준으로 일치 |
| **RG4** | dependency wave 기반 병렬 recording·진단 | RG3 | 6일 | sequential/parallel compiled order와 픽셀 동일, wave·critical path·edge 원인 dump 제공 |
| **RG5** | 제품 Pass 접근 선언 이관 | RG4 | 10일 | 기본 19개 node, 제품 호출과 test/fixture 접근 선언 이관; 임시 adapter 잔여 0. C# IR 조립은 PHASE 4.6 소유 |
| **RG6** | RHI 중립 제품 cutover·DX12 수용 — **이 페이즈의 완료선** | RG5, **BASE-0** | 4일 | 같은 밀봉 입력의 별도 프로세스 live frame, PNG/차영상/선형 오차, CPU record·pass GPU·graph stats, validation 0 |
| **RG7** | transient buffer + in-frame aliasing | RG6 | 14일 | alias off/on 픽셀 동일, peak committed/resident byte 감소 실측, poison/overlap/lifetime 변이 통과 |
| **RG8** | queue-neutral multi-queue + async compute | RG7 | 16일 | single-queue fallback, cross-queue fence/ownership, DX12 validation, 겹침 GPU 이득 실측 |
| **RG9** | subresource·split barrier·Resource Inspector 성숙 | RG8 | 10일 | mip/array/range 추적, split barrier parity, producer/consumer/version/order/lifetime/alias/queue 시각화 |
| **Q0** | 중립 queue/capability·fence 기반 | RG8/L4 소비 전 | 6일 | DX12 큐·실패·수명 검증, 중복 구현 0 |
| **RG-V** | compiled graph 기본 reader/viewer | RG3→RG6 | 4일 | 실제 generation·order·resource 표시, 별도 실행 그래프 0 |

**2026-10-01 재산정:** BASE-0 4 + RG1~6 46 + RG7~9 40 + Q0 6 + RG-V 4 = **100인일**.
RG6 첫 제품 완료선은 BASE-0/RG1~6 50인일이며 기본 viewer 4일을 포함하면 54인일이다.
Q0 공통 기반은 RG8/L4에 중복 산정하지 않는다. 상세는 [공수 원장](RenderPhaseEffortEstimate.md)을 따른다.

RG7 이후는 최적화 트랙이다. RG6을 통과하면 리소스 의존성으로 실행 순서를 결정하는
제품 RenderGraph는 이미 성립하며, 뒤 단계가 늦어져도 declaration-order로 되돌리지 않는다.

---

## 4. 슬라이스별 구현 경계

### RG0 — 기준선을 먼저 잠근다

- 현행 `BuildOrder` 계약과 제품 graph dump를 artifact로 남긴다.
- 독립 Pass, 연쇄 RAW, 두 writer, read-modify-write, culled branch, imported history fixture를
  RHI 중립 입력을 사용하는 DX12 테스트로 만든다.
- UI/Grid처럼 같은 target을 읽고 다시 쓰는 Pass를 찾아 state만으로 `modify`가 표현되지
  않는 사례를 고정한다.
- 테스트가 잘못된 구현을 잡는지 producer edge 삭제, version 재사용, 순서 뒤집기 변이로
  확인한다.

### RG1 — 최소 접근 선언으로 단일 writer DAG부터 정렬한다

- 기존 `{handle, state}` aggregate 초기화는 임시 adapter에서만 받고 테스트와 이관 대상은
  `Read`/`Write`/`ReadWrite`를 상태와 별도로 선언한다.
- 한 리소스에 writer가 하나일 때 RAW edge로 stable topological
  sort를 수행한다. ready 집합은 authored index로 tie-break한다.
- writer가 둘 이상이거나 `ReadWrite` 연쇄의 producer를 결정할 수 없으면 컴파일을 실패시킨다.
  선언 순서에서 암묵적인 writer 순서를 추측하지 않는다.
- 독립 Pass 재배열, cycle, 누락 producer, 다중 writer 거부 fixture를 기준선과 대조한다.

### RG2 — 버전으로 다중 writer·Modify DAG를 연다

2026-10-03 완료: ExplicitVersioned·resource ID/version/kind/epoch·Write/Modify·RAW/WAR/WAW를 구현했다. 두 구성 240 shuffle·오류 거부 및 실제 GPU 이전 버전 픽셀 0, 변경 전후/구성 간 16개 이미지 오차 0을 검증했다. 제품 기본 DeclarationOrder와 보수적 culling은 유지하며 RG3/5/6에서 정리·이관한다. transient buffer 생성은 RG7이다.

- `Write`/`Modify`가 새 version handle을 반환하고 후속 consumer가 특정 버전을 읽게 한다.
- texture와 buffer의 import/create, read/write/modify 계약을 대칭으로 닫는다.
- RAW/WAR/WAW와 `Modify` 연쇄를 version 계보로 유도하며 다중 writer를 명시적으로 판정한다.
- 컴파일 결과에 resource ID·version·producer·consumer·access·required state를 기록한다.
  edge/indegree 저장을 compact하게 유지하고 hot-frame heap churn을 계측한다.
- debug에서는 전체 cycle 사슬, release에서는 짧은 오류와 stable ID를 남긴다.

### RG3~RG4 — 기존 기능을 새 순서에 다시 연결한다

2026-10-03 RG3 완료: version producer 역추적과 imported 최종 출력 root, dead writer/reader 제거 후 WAR/WAW 재연결·stable sort를 구현했다. compiled order의 수명/할당/Transition 계획을 검증하고 UAV read→read 배리어를 생략했다. 두 구성 120 shuffle 및 실제 GPU dead writer 미실행·이전 버전 픽셀 0, 제품 변경 전후/구성 간 16개 이미지 오차 0을 회수했다. 제품 기본 DeclarationOrder는 RG5/6 이관까지 유지한다. 2026-10-03 RG4 완료: 살아남은 RAW/WAR/WAW의 dependency wave·pass-count critical path, target append 제약을 포함한 recording wave를 구현했다. compiled GPU 제출 순서와 split/cost fallback을 유지한다. 두 구성 24 shuffle·1/2/4 워커 GPU 픽셀 0 및 현재 HEAD의 변경 전후/구성 간 제품 16개 이미지 오차 0을 회수했다. [RG4 증거](../analysis/RenderRg4RecordingWaves.md)를 따른다.

- culling은 "앞서 쓴 모든 Pass" 검색이 아니라 version producer edge를 역추적한다.
- lifetime과 transient 회수는 declaration index가 아니라 compiled index를 사용한다.
- barrier state machine도 compiled order에서 계산하고 access mode와 state 불일치를 거부한다.
- 병렬 기록은 DAG의 ready wave를 사용하되 GPU submit은 compiled order와 backend 계약을
  보존한다. wave 수보다 record cost가 우선인 기존 split-pass 휴리스틱은 유지한다.

### RG5~RG6 — 제품을 한 번만 전환한다

2026-10-03 RG5-1 완료: 실제 GBuffer 6출력·Shadow 1출력 생산자 선언을 버전 모드의 Write로 연결했다. 세 모드×두 선언 순서의 실제 Declare 계획 검사와 두 구성 제품 회귀를 통과했으며 변경 전후/구성 간 16개 이미지 오차는 0이다. 현재 정적 호출 위치는 제품 59·게이트 202(새 검사 1 포함)이며 Tools/regression의 포인터 호출 18도 게이트로 센다. [생산자 이관 기록](../analysis/RenderRg5ProducerMigration.md)을 따른다. 기본 DeclarationOrder 호환 분기는 아직 남아 있으므로 RG5 전체·legacy 추론 0·제품 versioned GPU 수용을 완료로 판정하지 않는다.

> **과거 `SRP-1`과의 병합 판정 (2026-09-01, 현재 계획에는 미적용).** `Phase4UnifiedPlan` 백로그 산정 중
> "RG5와 SRP-1이 같은 500줄을 만지니 병합하자"는 제안이 나왔다가 **전수 실측으로
> 기각**됐다. 두 계획서가 같은 명사("19개 노드")를 쓰지만 대상 심볼이 다르다 —
> RG5는 `AddPass`/`AddSplitPass`(총 120 · **제품 38** · 게이트 82)로 **Pass 구현 파일
> 전역에 흩어져** 있고, SRP-1은 `AddNode`(총 33 · 제품 33)로 **조립부에 모여** 있다.
> 교집합 파일은 `EnhancedSceneRenderer.cpp` 하나뿐이고 그 안에서도 다른 줄이다.
> 축도 다르다(접근 **선언** vs 노드 **조립**) — 합치면 픽셀이 붉을 때 어느 축인지
> 못 가린다. 당시에는 `RG5 → SRP-1` 순서와 12일·8일을 따로 산정했다.
> 판정 전문: [`ScriptableRenderPipelinePlan.md`](ScriptableRenderPipelinePlan.md) §12-E.
> 현재 C# 저작 전환은 [`CSharpRenderPipelinePlan.md`](CSharpRenderPipelinePlan.md)의
> `CSRP-5`가 별도로 소유하며, 옛 `SRP-1` 8일을 이월하지 않는다.
>
> **덤 — 호출 수가 늘었다.** §3 표와 아래 목록의 근거인 "제품 28곳 · test/fixture
> 80곳(총 108)"이 2026-09-01 실측으로 **제품 38 · 게이트 82(총 120)**다. 제품만 **+36%**.
> 이 절이 "호출 수는 착수 직전 다시 센다"고 적어 둔 그대로이며, 현재 RG5는 이관 7 + DX12 회귀 3의 10일 계획 추정으로 갱신했다.

- base 15개 + Editor 4개 node를 작은 묶음으로 이관하고 각 묶음마다 현행/A-B 픽셀을
  비교한다.
- migration adapter와 legacy order switch는 테스트 rollback용으로만 유지하고 RG6 종료 시
  제품 기본 경로에서 제거한다.
- pass fixture와 전체 live frame을 별도 판정한다. 하나의 통과로 다른 하나를 대신하지
  않는다.
- DX12에서 같은 밀봉 입력의 compiled graph stable ID와 dependency hash가 재현되어야 한다.

### RG7 — aliasing은 정확성 완료 뒤 연다

**2026-10-08 최종 종료: RG7 done·기성 14인일, 기본 OFF 유지.** 아래 경과 기록의 progress/잔여 문구는 역사 기록이다.
[최종 종료 근거](../analysis/RenderRg7Closure20261008.md): D/R 전체 회귀·Release 독립 4실행(ON→OFF/OFF→ON),
32캡처·반복/교차 126출력 오차 0·validation/drops 0·exit 0. 100ms 연속 조회 17,684건·최대 간격 130ms·조회 실패 0.
두 순서 모두 ON 준비 CPU/표본 최대 사용량의 개선이 입증되지 않아 기본 채택하지 않는다.
C4는 공유 힙 소유량과 lifecycle DXGI 사용량의 가용 계측 범위로 완료하며, 정확한 resident/per-resource committed 귀속은 주장하지 않는다.
C5 채택 판정·C6 설정/rollback/기록도 완료했다. RG7-H/S·Vulkan 4.9·Q0/RG8/RG9는 별도이며 PHASE 4.3 전체 완료는 아니다.


2026-10-08 구현 착수: graph-owned buffer·DX12 placed heap 공유·활성화/전체 RT·DS 초기화·
강제 수명 연장·기본 committed fallback과 Editor 검사를 추가했다. VS 2026 Debug/Release 빌드와
Editor 기존 전체 회귀·GPU 5모드·배치/초기 상태/재컴파일 거부 검사를 통과했다. 검사 그래프의
할당량은 384→128 KiB이며 실제 제품 성능·VRAM 수용과 구분한다. 기성은 0, 아직 완료가 아니다.
수정 소스의 Release 64×64 LX 실제 Editor OFF/ON은 모드별 2프로세스·16출력 오차 0을 통과했다.
그래프 할당은 384 KiB(0.76%) 줄었지만 CPU 기록 중앙값은 2.041→2.252ms로 증가해 기본 OFF를 유지한다.
실제품 해상도·다중 view·poison/실패·peak VRAM 수용과 힙/자원 준비 비용 개선은 다음 RG7 작업이다.
완료된 공유 힙/자원 묶음과 네이티브 할당 정보의 제한된 캐시를 추가했고, VS 2026 D/R 빌드·
Editor 전체 회귀와 9모드 GPU 검사를 통과했다. 재사용 구간의 새 힙 생성·할당 조회는 0이며
해제·재생성·최종 drain을 확인했다. 캐시 변경 후 Release 64×64 LX OFF/ON 4프로세스·16출력 오차 0,
warm ON 4힙/10자원 재사용·네이티브 생성/조회 0을 확인했다. 이번 CPU 기록 중앙값은
2.5795→2.163ms이며 실제품 성능 보장은 아니다. 기본 OFF와 기성 0을 유지한다.
후속 poison/실패 복구는 VS 2026 D/R 빌드·Editor 전체 회귀·GPU 11모드·6개 실패 시나리오를
통과했다. 공유 RT/DS·float UAV 버퍼 poison, 부분 생성 해제, stale 활성화 오류 반환,
제출 거부 후 캐시 상태·다음 제출 복구를 확인했다. 후속 GPU 지연 3프레임 fixture는
독립 그래프·공유 history·late reader 수명과 실제 native 제출 뒤 오류 반환 주입을 검사한다.
VS 2026 D/R 빌드·전체 회귀·11 GPU/6 실패/2 수명 모드가 validation 0·exit 0으로 통과했다.
실패 token은 fence 값만으로 회수하지 않고 명시적인 GPU idle 경계까지 유지하며,
CPU 완료 알림은 격리·수명 등록 뒤로 옮겼다. 별도 WARP native RemoveDevice 검사는 D/R 최종
전체 회귀·validation 0·exit 0을 통과했다. Signal 성공 반환 뒤에도 장치 손실 fence 표식을
확인해 병렬 제출을 거부하고 명시 abandon까지 자원을 유지하도록 수정했다.
후속 VS 2026 D/R 제품 Editor OFF/ON 4실행은 Scene/Game/Material Preview 1498×730,
각 ON 15공유 자원/15활성화·OFF 0·imported/history 제외·공유 수명 비중첩을 통과했다.
씬 epoch 4→5·2246×1094→1124×548 resize·validation/drops 0·exit 0과 동일 입력
controlled scene 7출력씩 총 14비교 오차 0을 확인했다. 도킹 루트 복원 assertion과
고정 node-editor preview의 Inspector 우선순위 충돌을 수정했다. 전체 제품 뷰 픽셀이나
GPU 지연 history 교체 내용 검증까지 확대하지 않는다. native OOM·자발적 하드웨어
손실/재생성·Signal HRESULT 실패·확장 poison·장기 제품 수명 stress·준비 CPU/peak VRAM과
대표 workload 성능 수용은 남아 기본 OFF·기성 0을 유지한다.
[구현 및 잔여 수용](../analysis/RenderRg7Implementation20261008.md).

준비 CPU 계측 기반은 후속 D/R 빌드·Editor 전체 회귀 및 RG7 11 GPU/6 실패/2 수명/
native WARP 제거(validation 0·exit 0)로 확인했다. `transientPrepareCpuMs`는 수명 계산·
할당/캐시·자원 생성의 CPU wall time이며 barrier 계획/명령 기록은 제외한다. 모드당 단일
관측으로 성능 수용이나 개선율을 주장하지 않는다. 다음은 같은 입력/해상도의 제품 OFF/ON
반복 표본·cold/warm 분리와 live heap/cache 바이트·장치 사용량 연속 표본 검증이다.
후속 Release 제품 capture 재실행은 ON/OFF 2프로세스·각 8표본·1498×730을 통과했다.
16표본의 변환/pose 포함 입력 일치·8쌍 56출력 오차 0, ON 각 6힙/15자원 재사용·
생성/조회 0·validation/drops 0·exit 0이다. 준비 CPU 중앙값 0.0053→0.0244ms,
장치 사용량 표본 최대 1116→1109MiB는 관측값이며 성능 채택·정확 peak 절감 근거가 아니다.
Debug 반복 제품 표본·cold/warm 계측·live heap/cache 바이트·연속 사용량 표본·
실행 순서 반전/독립 프로세스 반복과 성능 수용은 남아 있다.
후속 공유 native 힙 소유 바이트 계측은 D/R 빌드·Editor 전체 회귀 및 11 GPU/6 실패/
2 수명/native WARP 제거(validation 0·exit 0)로 확인했다. 동일 힙은 한 번만 집계하며,
retained=leased+cached, warm 128KiB·GPU 지연 3뷰 192KiB 보유→완료 후 캐시→drain 0,
native 제출 뒤 실패/장치 제거의 64KiB 격리와 partial 실패 peak/해제를 검사했다.
캐시 clear 뒤 별도 group 소유자가 남으면 64KiB를 유지한다. 계측 범위는 pool의 공유
group-owned native 힙이며 committed/imported·독립 native 소유자·실제 residency는 제외한다.
후속 pool 식별자·바이트 단위 장치 사용량은 D/R 전체 회귀와 새 Release 제품 OFF/ON
각 8표본으로 검증했다. 56출력 오차 0·validation/drops 0·exit 0이며, 세 뷰는 같은
풀 1개를 공유하므로 중복 합산하지 않는다. ON retained 16MiB·cache 0~8MiB, OFF 0.
장치 사용량 표본 최대 OFF/ON 1,171,038,208/1,163,436,032바이트는 resident peak 아님.
준비 CPU 중앙값 0.00565→0.02585ms는 관측값이며 성능 수용 아님. 근거:
RG7ProductMemoryDomains-20261008 및 RG7MemoryDomains. 연속 cold/resize/지연 표본·
committed 소유량·순서 반전 독립 반복·성능 수용은 후속, 기본 OFF·기성 0 유지.

- 먼저 transient buffer를 texture와 같은 lifetime 모델에 넣는다.
- 서로 겹치지 않는 compiled lifetime만 같은 heap 영역을 공유한다.
- alias barrier, alignment, format/usage compatibility, imported/history 제외 규칙을 명시한다.
- `r.RenderGraph.Aliasing=0/1`, poison clear, lifetime 강제 연장 모드로 메모리 절감과 픽셀
  동일성을 동시에 판정한다.

#### RG7 종료 기준 고정 — 2026-10-08

기존 측정/검증의 범위 확대를 자동 종료 조건으로 누적하지 않는다. 현재 정본은
[고정 체크리스트 C1~C6](../analysis/RenderRg7Implementation20261008.md#fixed-rg7-closure-checklist-2026-10-08)다.
C1 할당/수명·C2 지원 범위 poison/실패 소유권·C3 제한된 제품 다중 뷰/픽셀은 기존 근거 범위에서 수용한다.
필수 잔여 순서는 C4 연속 lifecycle 메모리 측정 → C5 동일 부하/순서 반전 독립 반복 및 채택 판정 →
C6 설정/rollback·종료 기록이다. 정확한 물리 resident peak 증명을 추가 종료 조건으로 삼지 않으며,
측정 가능한 committed/소유 바이트와 장치 사용량 표본 최대·간격/누락을 별도 기록한다.
기본 ON 전환이나 새 설정 UI는 자동 필수가 아니다. 성능 채택 판정에 따라 OFF 유지도 가능하다.
실제 OOM/자발적 손실/진짜 Signal 실패는 RG7-H, 확대 poison/장기 내용 stress는 RG7-S 후속으로 분리한다.
Vulkan 제품 수용과 cross-queue 인계는 각각 백엔드 게이트/RG8이다. 기존 결과를 전 범위 완료로 확대하지 않는다.
코드 변경에 따른 회귀 재실행은 새 진척으로 계산하지 않는다. 현재 progress·기성 0·잔여 추정 14는 유지한다.
### Q0 — 큐·timeline RHI 공통 기반 (2026-10-08 완료)

Q0의 고정 3단계(중립 계약 → DX12 service → queue별 제출/수명)를 완료했다.
[구현·최종 수용 기록](../analysis/RhiQueueContractQ0_20261008.md).

- queue/recording identity·producer timeline·capability/factory/signal/wait 계약 구현.
- 기존 RHI 전용 스레드를 통한 one-shot 제출, 해당 queue의 완료점 기반 회수,
  실행 후 Signal 실패의 미발행·격리·검증된 종료 및 장치 손실 회수 구현.
- COMMON buffer 인계와 기존 backend closed-list/pinned-storage 어댑터 수용.
  기존 pool/encoder 호출자는 실제 저장소를 pin하고 GPU 완료 전 reset하지 않아야 한다.
- 기존 비표시 Editor D/R에서 CPU 23·native 28·제출/수명 43검사·전체 RG 회귀 통과.
  지연 producer→consumer 16KiB 오차 0·healthy validation 0·exit 0.
- Q0 done·기성 6·추정 6인일. Single-queue 제품 경로·RG7 기본 OFF 유지.
  RG8 graph scheduling/adoption·L4 소비·Vulkan 4.9 runtime은 완료로 세지 않는다.
### RG8 — async compute는 계측으로 채택한다

2026-10-09 구조 재감사 우선: **progress·기성 0**으로 복귀한다. [감사](../analysis/RenderRg7Rg8StructuralAudit20261009.md)의 큐별 pool/batch 재사용 → compiled 상태/ownership 계획 단일화 → overlap/비용에 따른 배치 → RG7 조합 수명 검토 순으로 원래 구현을 보완한다. 정확성/수명 회귀는 보존하며 다음 문단의 종료 판정은 철회한다.

2026-10-09 최종 종료: [종료·기본 OFF 판정](../analysis/RenderRg8Closure20261009.md). 구현/정확성/수명 수용과 GPU critical path·표본 최대 DXGI usage 비교를 마쳤다. 개선 미입증으로 기본 OFF·opt-in/fallback 유지, done·기성 16인일 회수. 기본 ON 채택이나 Vulkan/L4 수용을 뜻하지 않는다. 아래 진행 기록을 대체하는 최신 판정이다.

2026-10-09: 실제 live SSAO.Compute/SSAO.Filter의 compute 분산과 큐별 query/clock calibration 연결 수용. VS 2026 D/R native/frame/profiler 135·plan 24·전체 RG 회귀, 각 OFF/ON 제품 7개 출력 오차 0·validation 0·exit 0. 세 뷰·재질 복구/편집·씬 교체·리사이즈와 실제 compute 제출/2구간 계측 확인. [실행·제품 소비 기록](../analysis/RenderRg8QueueExecution20261009.md). 남은 작업은 기존 GPU critical path/peak memory/픽셀 기준의 성능 채택 판정이다. RG8 진행·기성 0·기본 OFF, RG7/Q0 종료 유지.

2026-10-08 착수: 고정 순서는 compiled 큐 배치/fallback → Q0 기반 기록·인계·제출·회수 → 기존 픽셀/validation/GPU critical path/peak memory 수용이다. 이번 단계는 계획만 구현하며 기존 제품 single queue를 유지한다. 진행 근거: [RG8 큐 배치 계획](../analysis/RenderRg8QueueSchedule20261008.md). 기성 0, RG7/Q0 종료 상태 유지.


- RHI에 graphics/compute queue capability, encoder pool, signal/wait fence와 queue ownership을
  중립 인터페이스로 추가한다.
- scheduler는 capability와 비용 힌트가 허용한 Pass만 compute queue 후보로 만들고, 미지원
  환경에서는 같은 DAG를 graphics queue 하나로 실행한다.
- queue 이동으로 resource lifetime이 늘어 aliasing 이득을 상쇄할 수 있으므로 peak memory와
  GPU critical path를 함께 비교한다.
- Lightmap 트랙 L4는 이 공통 기반을 소비한다. 별도 compute queue 계층을 중복 구현하지 않는다.

### RG9 — RDG 운용 성숙도를 닫는다

- texture mip/array slice와 buffer byte range를 view/range로 선언한다.
- backend가 지원하는 split barrier를 중립 계획으로 표현하되 단일 barrier fallback을 둔다.
- Resource Inspector는 compiled graph의 읽기 전용 소비자다. 실행 그래프나 GPU resource를
  따로 소유하지 않는다.

---

## 5. 회귀와 실패 판정

| 범주 | 필수 판정 |
|---|---|
| 정적/API | implicit state-write 추론 제품 사용처 0, unversioned migration adapter 0, task/graph stable ID 중복 0 |
| 그래프 | missing producer, forked write, cycle, culled branch, side effect, history import의 양·음성 fixture |
| 결정성 | 동일 입력 100회 compiled order/dependency hash 동일, 독립 Pass 등록 순서 tie-break 명시 |
| 배리어 | DX12 debug layer error 0, 요구/실제 state mismatch 0 |
| 픽셀 | pass fixture와 전체 live frame을 분리해 DX12 변경 전후 PNG·linear RMSE·max error·changed pixel 판정 |
| 병렬 | sequential/parallel pixels 동일, CPU record time과 critical path 기록 |
| 메모리 | RG7 전후 peak committed/resident/transient byte와 alias reuse count, poison mode 오류 0 |
| 큐 | RG8 single/multi queue 픽셀 동일, fence wait와 ownership transfer 누락 0, GPU frame time 이득 실측 시 기본 채택. 이득 미입증이면 기본 OFF로 채택 판정을 기록한다 |

어느 단계든 새 경로가 실패하면 해당 슬라이스의 A/B 스위치로만 되돌린다. 이미 통과한
버전 핸들/API까지 통째로 철회하거나 별도 제품 그래프를 만드는 롤백은 허용하지 않는다.

---

## 6. 비목표와 금지선

- Asset, Shader Graph, C#에 raw D3D12/Vulkan resource·barrier·fence를 공개하지 않는다.
- RG0~RG6에서 성능 추측만으로 독립 Pass를 재배치하는 cost optimizer를 만들지 않는다.
- RG6 전 aliasing, multi-queue, async compute를 제품 기본 경로에 배선하지 않는다.
- graph compile 도중 Pass callback을 실행해 숨은 의존성을 발견하지 않는다. 의존성은 setup
  선언에서 완결한다.
- DX12 전용 해법을 먼저 만들고 Vulkan을 나중에 어댑트하지 않는다.
- 문서 항목의 `done`을 빌드·런타임·픽셀 검증 완료로 해석하지 않는다.

---

## 7. 다른 페이즈·트랙과의 의존성

이 트랙은 **PHASE 4.3**이 소유한다. 2026-09-15 이전에는 PHASE 4.75의 한 트랙이었고, 그때
`BASE-0`은 PHASE 4.5에 있었다. 지금은 `BASE-0`도 이 페이즈가 소유하며 PHASE 4.5·4.6·
4.7·4.75·4.8의 필요한 게이트가 읽기 전용 입력으로 받는다. 현재 관계는
[`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md)가 정본이다.

| 계획/트랙 | 소속 | 관계 |
|---|---|---|
| `BASE-0` | **같은 페이즈** | `RG0`의 graph dump·변이 fixture를 흡수한 고정 장면 기준선 하네스다. `RG1`이 선행으로 받고 `RG6`가 live 판정에 쓴다. 하네스는 한 벌만 만든다 |
| `CSharpRenderPipelinePlan.md` | PHASE 4.6 | C# `Build()`의 immutable IR이 RG1 단일 writer DAG, RG2 version/Modify 계약을 소비한다. 제품 조립 전환 `CSRP-5`는 RG6 이후 별도 판정한다 |
| SRP-G0 | (`BASE-0`에 흡수) | RG0 기준선과 RG6 전체 live backend artifact를 공유한다. 별도 캡처 체계를 만들지 않는다 |
| `LivePipelineDescPlan.md` | archive | 현재 nodes/reads/writes/modifies를 RG5의 첫 native compiler 입력으로 사용한다 |
| `RhiBoundaryPlan.md` | archive | RG7 heap/alias 계약과 RG8 queue/fence 계약을 backend-neutral RHI에만 추가한다 |
| `ModelAssetBigBangCutoverPlan.md` | PHASE 3.75 | MBC6의 vertex attribute mask→input layout/PSO/VSIn 계약과 model generation handle이 RG5 제품 이관 전에 필요하다 |
| 트랙 L4 | PHASE 4.7 | `RG8`이 아니라 **`Q0`**(queue/fence RHI 계약)의 소비자다. `Q0`은 RHI 계층 공용 기반이며 어느 트랙도 별도 queue 계층을 만들지 않는다 — [`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md) |
| GPU-driven/DXR/Stochastic Lighting | PHASE 4.8 | RG6 단일 큐 제품 cutover 뒤 새 resource/pass를 추가하고, RG7~RG9 기능을 필요에 따라 소비한다 |
| TU/FG | PHASE 4.5 | **이 트랙을 선행으로 받지 않는다.** 모션 벡터·업스케일은 RG 재작성과 독립이고, 프레임 생성은 SDK가 자기 큐를 소유하므로 `Q0`도 받지 않는다. `BASE-0` 하나만 공유한다 — [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) §3.3 |

권장 임계 경로는 다음으로 고정한다.

```text
BASE-0 → RG1(단일 writer DAG) → RG2(version/Modify DAG) → RG3 → RG4 → RG5 → RG6
                                                                              ↓
                                                                            RG7 → RG8 → RG9
```

**2026-09-02 정정.** 모델/정점 입력 계약은 PHASE 3.75 완료를 하드 선행으로 받고, `SRP-G0`는
`BASE-0`으로 흡수돼 이 트랙보다 앞에 선다. **L4의 async compute는 `RG8`이 아니라 `Q0`을
기다린다** — `Q0`은 `RG8`·`L4` 중 먼저 필요해지는 쪽의 착수 시점에 세운다. 그 전의
UV/BVH/직접광 준비는 독립적으로 진행할 수 있다. 현재 통합 순서는
[`RenderPhaseRoadmap.md`](RenderPhaseRoadmap.md)가 정본이다.

**2026-09-15 페이즈 분리.** 이 트랙과 `BASE-0`이 **PHASE 4.3**으로 나왔다. 공수·선행·종료
게이트는 하나도 바뀌지 않았고 **소속만 바뀌었다** — 이 재배치를 진척으로 읽지 않는다.
분리 근거는 셋이다: ① `SRP-1`(RG5 선행)과 `L4`(Q0 선행)를 같은 페이즈에 묶어 두면 어느
완료선도 독립적으로 닫히지 않았다, ② 구 PHASE 4.75 208.5일 중 이 트랙이 113일로 절반을
넘어 한 페이즈에 완료선이 둘 있었다, ③ `BASE-0`은 `4-0`·`SRP-G0`·`RG0`의 통합물이라 가장
앞선 소비자인 RG를 따라와야 했다.

**이 페이즈의 완료선은 `RG6`이다.** `RG7`~`RG9`는 같은 페이즈 안의 최적화 트랙이며, 늦어져도
`RG6`가 세운 제품 RenderGraph를 declaration-order로 되돌리지 않는다.

## RG-V — compiled graph 읽기 전용 viewer (2026-10-01 추가)

2026-10-07 기본 native viewer 수용 완료: scene epoch·history·view/extent/inactive 검사, 완료 후 preview reader admission, 24개 순서/edge/stale/retention 변이, D/R 실제 3 view·동일 뷰 씬 교체·resize·UI off/on·capacity/copy/p95 비용·validation 0·exit 0을 수용해 기성 4를 회수한다. graph 오류 복구·메시 배치/편집/재로드도 수용했다. C# IR 연결은 PHASE 4.6 CSRP-5/6이며 RG7~9 확장·Vulkan runtime은 별도다. [검증 기록](../analysis/GraphRecoveryRgV20261007.md).

PHASE 4.3의 별도 todo 4인일 행이다. RG9의 subresource/split barrier 구현 완료까지
기본 graph viewer를 미루지 않는다. RG3 compiled DAG/culling/lifetime/barrier 결과를 입력으로
초기 viewer를 만들고 RG6 제품 cutover에서 Scene/Game view별 실제 compiled generation으로 검증한다.
RG4 wave/critical-path와 RG7~9 alias/queue/range 정보는 지원되는 세대에서 같은 reader에 추가한다.

- Window > RenderPass는 설정/시간 계측이 아닌 이 viewer를 연다. 노드·리소스·edge 원인,
  authored/compiled order, version, culling, lifetime/state/barrier를 조사할 수 있다.
- 실행/저작의 정본을 새로 만들지 않는다. compiled immutable snapshot을 읽고, runtime GPU
  리소스를 UI가 소유하지 않는다. 미지원 range/queue는 빈 정답을 만들어 표시하지 않는다.
- 두 view·Scene/resize/세대 교체에서 원 graph와 node/edge/hash/수명/배리어가 일치하고,
  Pass 조건 변경/culled branch·cycle 진단이 실제 compile 결과와 일치해야 한다.
- 선언 배열 shuffle, missing edge, stale snapshot 변이가 검증을 실패시키고, viewer off/on
  비용·메모리 상한과 Debug/Release UI를 확인한다. 단순 현재 pipeline 문자열 dump는 RG-V 완료가 아니다.
- 4.6 CSRP-5/6의 C# Build → immutable IR → native compile 결과를 동일 viewer에 연결한다.
  Source가 C++/C#인 경우를 표시하되 두 개의 편집 가능한 graph SoT를 만들지 않는다.


2026-10-03 RG5-2 정책 분리 검증 완료: 실행 순서 정책을 별도 RGOrderPolicy로 분리한다. 같은 버전 DAG로 의존성 정렬 또는 선언 순서 유효성을 검사한다. [정책 분리 기록](../analysis/RenderRg5OrderPolicy.md). 소비자 이관·제품 GPU 수용과 RG6 기본 전환은 열려 있다.


2026-10-03 RG5-3: Deferred의 GBuffer·Shadow 읽기와 SkyBox의 Modify/ReadWrite를 이관했다. 두 구성 빌드·native 선언 검사·제품 전체 장면 회귀 통과, 변경 전후/구성 간 16개 이미지 오차 0으로 첫 소비 체인 묶음 완료. [소비 체인 기록](../analysis/RenderRg5ConsumerMigration.md). RG5 전체는 진행 상태다.


2026-10-03 RG5-4 완료: SSAO·SSGI 및 현재 프레임 history 저장의 Read/Write·버전 계보를 이관했다. Debug/Release native 8개 조합·제품 회귀와 변경 전후/구성 간 16개 이미지 오차 0을 확인했다. Forward+는 Code/Graph 혼합 스트림과 재질 Graph 선언을 함께 이관하는 다음 묶음이다. [검증 기록](../analysis/RenderRg5IndirectMigration.md).


2026-10-03 RG5-5 Code 경로 완료: Forward+ 타일 버퍼와 Code 색상 스트림을 이관했으며 두 구성 빌드·native·제품 회귀와 변경 전후/구성 간 16개 이미지 오차 0을 확인했다. Graph 혼합 경로는 Lookup/Refraction/Subsurface/Volume 자원 소유자와 함께 이관해야 하므로 아직 열림이며 명시 모드에서 구체적 오류로 거부한다. [범위 및 검증](../analysis/RenderRg5ForwardCodeMigration.md). RG5 전체 기성 0을 유지한다.


2026-10-03 RG5-6 Lookup 소유자 이관 완료: 입력 캡처 출력 버전 반환·Bake sample/statistic Write·Ready Read를 연결했다. Debug/Release 최종 빌드·native 반복 2회·두 정책 및 제품 회귀 통과. 변경 전후/구성 간 각 16개 이미지 오차 0. Release 최초 120초 종료 제한 실패는 보존하고 240초 대기 재실행의 정상 종료로 수용했다. 나머지 Graph 소유자와 혼합 스트림 guard는 아직 열려 있다. [Lookup 기록](../analysis/RenderRg5LookupMigration.md).

2026-10-03 RG5-7 Refraction·Subsurface·Volume 소유자 이관 완료: 최신 Lookup 입력 소비·캡처/배경/Bake/필터/합성 출력 버전을 연결했다. 두 구성 빌드·native·제품 반복 회귀 통과, 변경 전후/구성 간 각 16개 이미지 오차 0과 현재 해시를 확인했다. [소유자 검증 기록](../analysis/RenderRg5SpecialMigration.md). 제품 59·게이트 213 호출. GraphSurface/Draw·혼합 GPU 수용과 RG5 전체 기성 0은 유지한다.

2026-10-03 RG5-8 GraphSurface·Draw 및 반환 출력 완료: Shadow/GBuffer/Color/Blended 출력 반환·블랙보드/Forward+ 전달, 깊이 Copy→Modify 및 색상 Modify, Mesh 정적 업로드·world 버전과 중복 읽기를 연결했다. 두 구성 빌드·native·제품 반복 회귀 통과, 변경 전후/구성 간 각 16개 이미지 오차 0 및 현재 해시 일치를 확인했다. [배선 및 검증](../analysis/RenderRg5SurfaceMigration.md). 제품 59·게이트 217 호출. 실제 versioned SceneHost/mixed GPU 수용·guard 제거와 RG5 전체 기성 0은 유지한다.


2026-10-03 원격 물리·CSM/RG5 통합 후 현행 기준선 갱신 완료: Debug/Release 빌드 및 구성별 독립 2회 × 캡처 2회 회귀 통과, 통합 전후/구성 간 각 16개 이미지 오차 0, 현행 소스·바이너리 SHA-256 및 phaseComplete=true. Release 최초 포트 충돌 실패는 보존하고 v2 정상 반복 실행으로 수용했다. [통합 후 검증 기록](../analysis/RenderRg5IntegrationBaseline.md). 다음은 실제 versioned SceneHost·Code/Graph 혼합 GPU 수용 및 guard 제거이며 RG5 progress/기성 0과 RG6 미전환은 유지한다.


2026-10-03 RG5-9 실제 versioned SceneHost·Forward+ Code/Graph 혼합 GPU 수용 완료: 세 정책 × 24개 장면을 두 구성에서 실행하여 각 72 frames·정책 간 maxError=0·validation=0, cold/완료 geometry 재사용 및 최신 반환 writer를 확인했다. ExplicitVersioned Graph guard는 제거하고 SingleWriter 거부는 유지한다. 두 구성 빌드·기본 제품 반복 회귀·전후/구성 간 각 16개 이미지 오차 0·현행 해시 기준선 통과. [검증 범위 및 기록](../analysis/RenderRg5MixedGpuAcceptance.md). 제품 기본 DeclarationOrder, RG5 전체 progress/기성 0과 전체 SceneRenderer versioned GPU 수용/RG6 미전환은 유지한다. 다음은 나머지 소비자/ReadWrite 및 legacy 추론/adapter 제거다.

2026-10-03 RG5-10 Decal 접근·출력 버전 이관 완료: snapshot Write·입력 Read, GBuffer Modify/ReadWrite와 원본 텍스처 Read를 명시하고 반환 버전을 블랙보드·SceneHost 후속 입력에 연결했다. 두 구성 빌드·각 3정책/342 frames·7개 첨부 정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5DecalMigration.md). 제품 기본 DeclarationOrder, RG5 전체 progress/기성 0 및 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다.

2026-10-03 RG5-11 Sprite 접근·출력 버전 이관 완료: 독립 출력 Write·기존 HDR Modify/ReadWrite, 깊이 Read 및 원본 텍스처 중복 import/Read 제거를 연결했다. 두 구성 최종 빌드·각 3정책/18 frames·전체 RGBA 정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5SpriteMigration.md). 정적 cpp/h 호출은 제품 59·게이트 221(신규 검사 헤더 포함)이며 inl 전체 목록과 구분한다. 제품 기본 DeclarationOrder, RG5 progress/기성 0 및 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다. 다음은 화면 SSS/SSR 소비 체인 이관이다.

2026-10-04 RG5-12 화면 SSS/SSR 접근·출력 이관 완료: 두 블러 축과 반사 pass의 입력 Read·새 출력 Write/v0 및 선언 당시 핸들 캡처를 연결했다. 두 구성 빌드·각 3정책/48 frames·네 단계 전체 RGBA 정책 간 오차 0·validation 0, 실제 블러/반사 기여와 비활성/마스크/입력 누락 경로, 기본 제품 반복 회귀·전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5ScreenMigration.md). 정적 cpp/h 호출은 제품 59·게이트 224(신규 검사 헤더 포함)이며 inl 전체 목록과 구분한다. 제품 기본 DeclarationOrder, RG5 progress/기성 0과 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다. 다음은 VolumetricFog 소비 체인 이관이다.

## RG5 남은 이관 순서 — 2026-10-04 기준선 이력

RG5-12 직후 남았던 제품·Editor pass는 7개이며, 당시 최종 출력·캡처 배선과 test/fixture 정리가 별도로 남아 있었다. 아래는 당시의 이관 순서다. 2026-10-05 코드 진행과 미검증 경계는 위의 별도 절을 따른다.

| 순서 | 대상 | 남은 접근·버전 배선 |
|---|---|---|
| 1 | VolumetricFog | Scatter→Accumulate→Composite, history 읽기·쓰기 및 출력 버전 전달 |
| 2 | PostChain | 후처리 단계별 입력 Read·출력 Write, 중간·최종 출력 전달 |
| 3 | UI | 색상 합성 Modify/ReadWrite, 원본 텍스처 Read |
| 4 | Editor Grid·WireFrame | 색상·깊이 접근과 수정된 출력 전달 |
| 5 | Editor GizmoIcon·GizmoLine | 아이콘 텍스처 Read, 색상·깊이 접근과 출력 전달 |
| 6 | 최종 출력·캡처 | live_present와 capture/readback의 최신 출력 버전 소비 |
| 7 | test/fixture·임시 adapter | 남은 명시 접근 이관, 의도적인 legacy 검증과 구분하여 임시 추론·adapter 정리 |

위 배선과 전체 SceneRenderer의 versioned GPU 수용을 확인해야 RG5를 닫는다. 2026-10-04 수용 기준선은 제품 기본 DeclarationOrder와 IBL 1024/4096이었다. 2026-10-05 의존성 정렬 전환 코드는 그 이후의 미검증 변경이며 RG6 수용을 뜻하지 않는다. MAT-9 SSS·투과 품질/성능 게이트는 별도 미완료 조건이다. Vulkan 비교는 PHASE 4.9 RenderDoc 캡처→리소스 확인→픽셀별 비교에만 둔다.
