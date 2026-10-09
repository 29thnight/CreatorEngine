# PHASE 4 계열 재배치 — C# 저작·그래프·기능 완료선

2026-10-09 LX 후속: posed 메시렛별 프러스텀 검사, CPU 기하 LOD 선택·실제 제출과 그림자 LOD0 분리, 별도 현재 깊이→draw HZB, 생성 전 cache 예산 검사를 연결했다. D/R 8조합·192프레임·GPU validation 0, 실제 Release TestShadow 32출력 비트 동일을 확인했다. GPU LOD·메시렛 압축/개별 HZB·성능/VRAM·Vulkan 수용은 별도이며 기존 기성/상태를 변경하지 않는다. [구현·검증 경계](../analysis/LatticeGeometryIntegration20261009.md).

2026-10-09 구조 수정 2: 단일 큐는 기존 compiled barrier 계획을 그대로 사용하고, 다중 큐는 공통 계획기로 배치 내부 상태를 유지한다. D/R 최종 실행 150검사·계획 24검사·전체 RG 회귀 통과. 제품 검증은 [배리어 수정 기록](../analysis/RenderRg8Barriers20261009.md)을 따른다. RG8 progress·기성 0·기본 OFF 및 기존 공수 집계 유지.

2026-10-09 구조 수정 1: 패스별 제출을 의존 경계별 batch로 합치고 queue endpoint 소유 allocator/list 풀을 추가했다. D/R 실행 147검사·계획 24검사·전체 RenderGraph 회귀 통과. COMMON 전이 단일화·overlap 후보 선택·RG7 조합은 잔여이며 RG8 progress·기성 0·기본 OFF를 유지한다. [수정과 제품 검증](../analysis/RenderRg8Batching20261009.md).

2026-10-09 구조 재감사: RG8 제품 완료 판정을 철회한다. 패스별 allocator/list 생성·제출/Signal, 중복 COMMON 전이, TestShadow의 overlap 없는 큐 배치 및 RG7 조합 거부를 확인했다. RG8 progress·기성 0, PHASE 4.3 기성 74/잔여 26(RG8 16 + RG9 10), 전체 기성 126/잔여 437로 정정한다. RG7의 제한된 aliasing 구현/실험 종료와 기본 OFF는 유지한다. [상세 감사](../analysis/RenderRg7Rg8StructuralAudit20261009.md). 아래 종료 문구는 재감사 전 이력이다.

2026-10-09 RG8 종료: D/R 큐 실행·수명·SSAO compute 수용 후 Release TestShadow 1496×692의 336출력 오차 0·일반 GPU 192프레임·메모리 1846표본과 최신 Release 전체 RG 회귀를 확인했다. 두 순서 모두 GPU span 증가·메모리 절감 일관성 미확보로 기본 OFF를 유지한다. [종료·채택 판정](../analysis/RenderRg8Closure20261009.md). RG8 done·기성 16, 총계 142/421, PHASE 4.3 기성 90/잔여 10(RG9). RG7·Q0 완료 유지.


## 2026-10-07 최근 병합 반영

**같은 날 후속 — 재질 fallback 제거:** live Shadow/GBuffer/Forward의 native 재질 대체와 SceneHost의 이전 graph instance 대체를 제거했다. 그래프 준비 중에는 프레임을 대기시키고 실패한 요청은 거부한다. 타깃 clear와 graph 재질 기록을 분리했으며, CSM depth-content cache는 별도 미구현이다. 아래 RG5/RG6 수용은 해당 보고서에 보관된 소스 해시의 증거다. 후속 변경의 빌드·실행·capture 증거는 [graph-only 재질 패스 기록](../analysis/GraphOnlyMaterialPasses20261007.md)이 소유한다.

**같은 날 후속 — 그래프 오류 복구·RG-V 수용 완료:** 실패한 원본을 보존하고 별도 Principled→Output 기본 graph로 메시를 유지한다. D/R 빌드·편집/배치/저장·재로드·5개 graph draw·7개 원본 hash 보존을 수용했다. RG-V는 24개 변이/retention·실제 3 view·늦은 preview reader·scene epoch/resize·UI off/on 비용·GPU validation 0·exit 0을 통과해 기본 native viewer 4인일을 회수한다. C# 연결·alias/queue/range 확장·Vulkan runtime은 각 후속 페이즈에 남는다. [수용 기록](../analysis/GraphRecoveryRgV20261007.md).

PR #122/#123의 versioned 제품 graph·compiled viewer·GPU indirect/meshlet/static LOD/HZB와 재질 보존 경로가 master에 적용됐다. **RG5·RG6·RG-V 기본 native viewer는 후속 최종 수용으로 완료**이다. GD/HY 전체 제품 수용은 미완료다. 기존 로컬 병합 후 기록의 codec 466/466·단일 DX12 장면 비교를 보존한다. decal/rendergraph 예외는 [2026-10-07 검사](../analysis/RenderRg5Closure20261007.md)에서 fixture 수정 후 D/R 정상 종료·validation 0으로 해소했다.

MAT-9는 live split-sum·타일 SSS/투과의 품질/성능 수용을 계속 소유한다. GPU LOD는 적격 mesh-shader 자산/장치 범위이며 indexed 경로는 LOD0, skinned mesh-shader LOD·DXR은 미완료다. #118/#120의 표시 수명/native Present는 FG 기반일 뿐 FG 완료가 아니다. 총 355인일은 유지하고 RG5 10·RG6 4·RG-V 4인일 회수로 기성 106/잔여 249(+미산정)이다.

IBL/Surface/Raster reference API는 D/R 각각 3정책·일반 63 frames·공유 depth 72 frames·GPU validation 0으로 수용했다. [reference 이관 기록](../analysis/RenderRg5ReferenceMigration20261007.md). 최종 Fog/PostChain/UI/Editor도 D/R 각각 129 frames·9단계 오차 0, 실제 Editor 독립 4프로세스/8캡처·구성 간 16개 이미지 오차 0·현재 해시 계약·14개 변이 거부로 RG5를 종결했다. [최종 수용 기록](../analysis/RenderRg5FinalAcceptance20261007.md). RG6도 동일 현재 소스의 진단 legacy/기본 제품 D/R 독립 8프로세스·408캡처·각 100회 graph 결정성, final 색상 오차 0·깊이 기존 오차 기준 통과·CPU/GPU 산출물·validation 0·exit 0으로 수용했다. [RG6 수용 기록](../analysis/RenderRg6Acceptance20261007.md). Geometry Occlusion/명시 icon read 차이를 기록했으며 순수 스케줄러 비용이나 MAT-9 성능 완료로 확대하지 않는다.

근거: [10월 4~7일 PR 적용 감사](../analysis/MergedPrReview20261007.md). 아래 과거 날짜의 검증 기록은 해당 시점의 증거이며 최신 HEAD의 통과를 뜻하지 않는다.

**정본 2026-10-03 · Material 구현 기반 보존 · MAT-7 공통 재질 통합 완료 · LX 마감/MAT-9 품질·성능 수용 잔여 · RND-ENV/GPU 상태 유지.**
이 문서는 PHASE 4 계열의 현재 소유권·표시 순서·공수 원장이다. 이전 Asset-first SRP
분할의 근거·이력은 [`Phase4UnifiedPlan.md`](Phase4UnifiedPlan.md)에 보존했다.
새 C# 저작/네이티브 실행 목표는
[`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md)다.

## 1. 페이즈와 회계

**2026-10-08 RG7 종료:** [측정·기본 OFF 채택 판정](../analysis/RenderRg7Closure20261008.md) 완료로 기성 14인일을 회수했다. 당시 총 추정 563·기성 120·잔여 443(+미산정), PHASE 4.3 기성 68·잔여 32였으며, 후속 하드닝/확장 수용을 RG7 완료로 간주하지 않는다.

**2026-10-08 Q0 종료:** 중립 계약·DX12 큐 서비스·큐별 제출/수명 고정 3단계를 D/R에서 수용했다. [검증 기록](../analysis/RhiQueueContractQ0_20261008.md). 총 추정 563·기성 126·잔여 437(+미산정), PHASE 4.3 기성 74·잔여 26. RG7 기본 OFF 유지, RG8/RG9·L4 소비는 미완료다.

**2026-10-08 범위 확장:** PHASE 4.85 [Path Tracing·Hybrid RT 파이프라인](PathTracingHybridPipelinePlan.md)을 신설한다. 공통 RT 기반, 두 제품 파이프라인, ReSTIR PT Enhanced·AMD tetrahedral cages 연구 구현/평가를 14행으로 분리했다. 구현/실험은 미착수이며 중앙 추정 208인일·기성 0이다. 4.8 설계 9인일·기성 106은 유지하며 산정 총계 563·잔여 457인일(+별도 미산정)로 갱신한다. 기본 환경 116·효과 확장/연구 92, 관리 범위 약 130~290인일이다. GPU-9는 prototype 뒤 변경 추정을 검토한다.

| 표시 순서 | 단일 완료선 | 활성 행 | 재산정 인일 | 완료 | 잔여 인일 |
|---|---|---:|---:|---:|---:|
| **4** | 현행 DX12 PBR 제품 배선 | 10 | 18 | 18 | 0 |
| **4.25** | Graph→ShaderMeta/Slang·공통 재질·Blender 수용 | 11 | 40 + 미산정 | 32 | 8 + 미산정 |
| **4.3** | BASE-0 → DAG·버전/Modify·RHI queue·viewer, DX12 수용 | 12 | 100 | 74 | 26 |
| **4.5** | 모션/히스토리·업스케일·프레임 생성, DX12 수용 | 16 | 71 | 0 | 71 |
| **4.6** | C# Pipeline IR·PassSchema/Roslyn·native 조립 | 7 | 32 | 0 | 32 |
| **4.7** | UV1·BVH·DX12 백그라운드 라이트맵 | 8 | 35 | 2 | 33 |
| **4.75** | probe/AO·shadow·display/post·Environment | 4 | 28 | 0 | 28 |
| **4.8** | GPU-driven·확률 조명·DXR 설계·구현 공수 확정 | 4 | 9 | 0 | 9 |
| **4.85** | 공통 RT·Path Tracing·Hybrid·ReSTIR PT Enhanced·tetrahedral cages | 14 | 208 | 0 | 208 |
| **4.9** | RenderDoc DX12/Vulkan 캡처 → 리소스 확인 → 픽셀별 비교 | 6 | 22 | 0 | 22 |
| **현재 합계** | | **92** | **563 + 미산정** | **126** | **437 + 미산정** |

**2026-10-01 사용자 지시 반영:** Vulkan 실행 비교·교차 동등성·픽셀 수용은 4.9가 단독 소유한다.
4~4.85는 RHI 중립 구현을 유지하면서 **DX12 Debug/Release의 변경 전후 회귀**로 닫는다.
4.9의 착수/완료나 기존 교차 오차를 앞선 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.

1인 전담 엔지니어의 계획 추정치이며 완료 기성 52일은 보존했다. 부분 구현 행은 임의로 완료
공수를 늘리지 않고 남은 완료 조건을 산정했다. 기존 317.5일은 미산정 범위가 빠진 부분 합계였다.
기존 355일은 Q0·RG-V·CSRP·RND-ENV·BP의 신규 산정 74일과 기존 범위 재산정 -36.5일을 반영한다.
2026-10-08 RTP 208인일을 추가해 현재 중앙 추정은 563인일이다.
숫자가 늘어난 것은 Vulkan 비교를 앞 페이즈에 유지해서가 아니다. 상세 ID별 변경·근거·중복 제외는
[RenderPhaseEffortEstimate.md](RenderPhaseEffortEstimate.md)가 공수 정본이다.

횡단 Lattice 8행(LX-0~6/LX-3H)은 Material 자체의 완료선이 아니므로 기존처럼 별도 미산정이며
현재 92행/563일에 합산하지 않는다. RT/PT/Hybrid와 효과별 EXP는 4.85의 208일에 포함한다.
GD 전체 잔여·Vulkan RT 신규 구현·새 하드웨어/미확인 결함 확대분은 별도 산정이다. **457일은 산정 범위의 잔여이며 별도 미산정까지 포함한 전체 잔여가 아니다.**
기존 Asset-first 35일의 역사 원장은 재도입하지 않는다.

MAT-3 완료는 공용 core 의미와 독립 GPU 8,280개 대조, 현행 Standard 제품 회귀 판정이다.
[PrincipledCoreSemantics.md](../design/PrincipledCoreSemantics.md)를 따른다.
비기본 IOR를 담는 제품 lookup/packing·graph binding은 MAT-6/MAT-7, Blender 교차 판정은 MAT-9에 남는다.
MAT-4는 Layered 공용 평가와 독립 GPU 216,776개 수치·독립성, energy bound/numeric golden 검증이다.
MAT-5는 Special 공용 평가·명시적 Forward 자원 계약과 독립 GPU 202,049개 수치·독립성,
energy bound/numeric golden 검증이다. [PrincipledSpecialSemantics.md](../design/PrincipledSpecialSemantics.md)의
diffusion·단일 경계 glass·homogeneous volume 근사 범위를 따른다.
MAT-6은 초기 지원 graph의 결정적 Slang/metadata·진단·실패 generation 복구와 독립 texture GPU 대조다.
[MaterialSlangCodegen.md](../design/MaterialSlangCodegen.md)를 따른다. 제품 reflection/pass adapter,
실제 자원/PSO/Scene 배선과 자동 cook route는 MAT-7에 남는다.
MAT-7의 reflection·candidate PSO·texture owner fence와 LXMC/CEMF·AssetCooker·encrypted
PAK 기반을 검증했다. [MaterialGraphProduct.md](../design/MaterialGraphProduct.md)를 따른다.
DataSystem/Material GUID generation·typed instance 저장 왕복/실패 복구를 연결하고
runtime 43개·제품 DataSystem Debug/Release 각 26개 검사로 검증했다.
제품 b2·texture·독립 sampler render binding adapter는 실제 엔진 DX12 오프스크린
draw에서 Debug/Release 각 168개 검사·64개 GPU 성분으로 검증했다. Scene 적용은 후속이다.
Core/Layered evaluated-point IBL bake와 별도 base/coat/sheen 응답 소비 draw도
Debug/Release 각 19,351개 검사·18,240개 GPU 성분을 통과했다.
[PrincipledIblBake.md](../design/PrincipledIblBake.md)의 Scene lookup 배치/보간/재사용,
환경 MIS/수렴·실시간 예산은 후속이다.
[MaterialGraphScenePacket.md](../design/MaterialGraphScenePacket.md)의 graph instance·coverage/queue·PSO·
IBL·recording binding을 함께 준비/게시하는 render owner와 실제 두 in-flight 제출의
교체·실패·abort·완료 해제를 검증했다. 이 render owner 검증 자체를 Scene pass 설치로 세지 않는다.
후속 [MaterialGraphSceneHost.md](../design/MaterialGraphSceneHost.md)는 실제 Scene GBuffer·
공유 depth·legacy/LX Masked 가림·Core/Layered HDR 합성에 bounded host를 연결했다.
현재 4,096픽셀·64 draw 이하의 정확도 기준용 IBL 적분 경로다. full-resolution lookup·
재사용/실시간 비용 수용, Special/투명 transport와 자동 Scene host 쿠킹은 남는다.
Debug/Release 각 830,436개 검사·GPU 659,914성분, 실제 합성 graph 24개·가시 3,582픽셀·
실패 거부 120개·GPU validation 0건을 확인했다. 실제 Editor Live Tick·Vulkan native 실행은 별도다.
위 MAT-7 기반 설명은 당시 진행 기록이다. 2026-10-02 기존 LX generation/binding/쿠킹 3인일을 MAT-7-BASE 이력으로 보존한 뒤 Graph→ShaderMeta/Slang→LX 공통 재질 소비와 alpha/SSS/transmission/Volume Forward+ 혼합·제품 회귀를 완료했다. [최종 검증](../analysis/MAT7ForwardTransportComposition.md)을 따른다. 미산정 MAT-7의 완료 시간을 역산하지 않아 완료 기반 32인일과 MAT-9 잔여 8인일은 유지한다. LX 마감은 미산정이다. 현재 판정은 BlenderMaterialGraphPlan §0, 이전 노트는 archive/Phase425ImplementationHistory.md를 따른다.
[MaterialGraphSurfaceBatch.md](../design/MaterialGraphSurfaceBatch.md)의 graph GPU 평가 →
point buffer bake → bounded draw에서 UV/LOD·world frame·eye·typed override를 소비한다.
완료 readback acceptance와 recording 성공을 구분하고 invalid 지점에 진단 표식을 남긴다.
이 point bake의 Scene lookup 보간/예산·async 게시와 일반 해상도 route 설치는 후속이다.
[PrincipledLayeredSemantics.md](../design/PrincipledLayeredSemantics.md)를 따른다.
EEVEE가 사용하지 않는 anisotropy/thin film의 Cycles 비교·RGB 근사 수용과 제품 route/성능은 후속이다.

## 2. 선행 그래프

```text
PHASE 4 PBR-W9 → PHASE 4.25 현재 구현·검증 기반 (MAT-7 완료, LX/MAT-9 수용 잔여)
                            ↓
PHASE 4.3 BASE-0 → RG1(단일 writer DAG) → RG2(version/Modify DAG) → RG3~RG6(제품 전환)
                    ├─ PHASE 4.5 TR/TU/FG  (BASE-0 뒤부터 RG 본체와 병렬)
                    ├─ PHASE 4.6 CSRP-0/1  (native IR는 RG1 접근 계약부터 소비)
                    │                → CSRP-2~4 → CSRP-5(RG6 뒤) → CSRP-6
                    ├─ PHASE 4.7 L1/L2→L3→L4(Q0 소비)→L5/L6→L7
                    ├─ PHASE 4.75 RND-1 / RND-2 / RND-3 (각각 독립 품질 gate)
                    └─ PHASE 4.8 GPU-1/2/3 → GPU-9 (구상·공수 확정)
                       PHASE 4.85 RTP-0→RTP-1/2 (공통 RT)
                         ├→ RTP-4/5 Hybrid · RTP-6/7 PathTracing→RTP-8 ReSTIR PT Enhanced
                         ├→ RTP-3 RT history/denoise · RTP-9/10 tetrahedral cages
                         └→ RTP-11/12 제품 구성/수용→RTP-13 판정 (필요 계약만 선행)
PHASE 4.9 BP-0→BP-1→BP-2→BP-3→BP-4→BP-5
  동일 입력 → RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교 → 수정/재캡처 → 회귀 패키지
  각 기능의 DX12 기준선을 입력으로 받는다. 4~4.85로 돌아가는 선행 edge는 없다.
```

**2026-10-01 선행 조정:** BASE-0과 후속 구조 정리는 구현·검증된 MAT-0~MAT-8의 Material/Scene/cook 계약을 소비한다. MAT-9 최종 완료를 일괄 선행으로 받지 않는다. 현재 1024/4096 IBL 품질 설정과 이미지 수용 상한을 고정하고, 미달 항목은 baseline에 명시한다. BASE-0의 통과는 같은 입력의 재현·계측·회귀 검출 판정이며 MAT-9 품질/성능 수용을 뜻하지 않는다. GPU-driven 실제 구현과 측정 뒤 동일 장면의 이미지 오차·Debug/Release CPU/GPU 시간·근접 이동 끊김을 MAT-9에서 다시 판정한다. SSS·투과 품질 오차는 성능과 별도 완료 조건으로 남긴다.

**2026-10-02 BASE-0 완료 조건 정리:** BASE-0은 RenderGraph 변경 전 DX12 기준선이다.
2026-10-03 Lattice 저장·재생은 별도 선택 조건으로 분리한다. 카메라·draw 재생만으로 Lattice 검사를
실행하지 않으며 기본 기준선과 선택 확장은 독립 프로세스·결과로 판정한다. 확장 전용 결과는 BASE-0
최종 완료 증거로 받지 않는다. 실제 Lattice 재생 수용 검증은 선택 확장에 남긴다.
현재 소스/바이너리의 Debug/Release 빌드 신원, 고정 자산·장면·카메라·해상도·tuning·환경,
구성별 독립 프로세스 2개 × 캡처 2개의 이미지 및 구성 간 비교, 현행 graph 진단·변이,
CPU compile/record·GPU pass timing·VRAM 계측과 validation/정상 종료를 완료 조건으로 둔다.
IBL 1024/4096·기존 이미지 상한은 유지한다. 범용 frame packet·모든 재질/애니메이션 파일 재생은
선택적인 검증 도구 확장이며 BASE-0 완료 또는 RG1 착수의 필수 선행에서 제외한다.
기본 하네스의 한 구성 `complete`와 두 구성 최종 `phaseComplete`는 구분한다.
[정확한 완료 계약과 판정 도구](../analysis/RenderBase0Baseline.md#현재-완료-계약--2026-10-02-범위-정리)를 따른다.

2026-10-03 현행 Debug/Release에서 구성별 독립 프로세스 2개 × 캡처 2개를 검증하고 구성 간 16개 이미지
maxError=0, GPU validation/encoder drop/소스 변경 0, 정상 종료와 현재 해시를 확인했다.
`Build/Obj/RenderBase0/current-static-v4-phase-complete.json`의 `phaseComplete=true`로 BASE-0을 done·기성 4인일로
회수한다. 이전 재생 및 실패 기록은 보존한다. RG1도 명시적 단일 writer DAG·두 구성 native/GPU 검사 및 변경 전후 16개 이미지 maxError=0으로 완료했다. 제품 기본 순서는 RG5/6 이관 전까지 유지하며 RG2 version/Modify·RAW/WAR/WAW와 두 구성 240 shuffle/GPU·제품 회귀도 완료했다. RG3도 버전 producer 기준 컬링·정렬 후 수명/배리어와 두 구성 native/GPU·제품 회귀로 완료했다. RG4도 dependency wave·target append 제약·critical path 진단과 두 구성 native/GPU·제품 회귀로 완료했다. RG5-1 GBuffer·Shadow 생산자 이관은 두 구성 선언 검사·제품 회귀 및 16개 이미지 오차 0으로 완료했다. RG5 전체는 진행 상태이며 선언/정렬 정책 분리·consumer/ReadWrite·legacy 추론 제거·versioned 제품 GPU 수용이 남았다. [RG5 현행 증거](../analysis/RenderRg5ProducerMigration.md)를 따른다. [RG4 증거](../analysis/RenderRg4RecordingWaves.md)를 따른다. [RG3 증거](../analysis/RenderRg3LifetimeBarriers.md)를 따른다. [RG2 증거](../analysis/RenderRg2Versions.md)를 따른다. [RG1 증거](../analysis/RenderRg1Scheduling.md)를 따른다.
Vulkan 비교·잔여 교차 오차는 [PHASE 4.9](BackendParityPlan.md), MAT-9 품질/실제 성능 수용은 해당 gate에 유지한다.

번호는 대시보드의 **표시 순서**다. 모든 페이즈가 앞 번호의 전체 완료를 기다리는
직선은 아니다. 특히 4.5는 `BASE-0`만 받고 RG 본체와 병렬이며, 4.7/4.75/4.8의
native 기능은 4.6 C# 저작 완료를 일괄 선행으로 받지 않는다. 제품 결과를 해당
Pass/graph에 얹을 때의 `RG6`, `Q0`, `MAT-9`, 시간축 입력은 **항목별**로 단다.
`RND-1 → RND-2`의 기존 계획 표기는 구현 의존 근거가 확인되지 않아 두 품질 축의
페이즈 전체 선행에서 제외하고 각 독립 gate로 관리한다.

## 3. 정본 경계

| 범위 | 계획/결정 |
|---|---|
| C#은 구성·설정, C++은 Pass 실행, RG는 의존성·동기화 | [`RenderPipelineTargetArchitecture.md`](../design/RenderPipelineTargetArchitecture.md) |
| 단일 writer DAG → version/Modify DAG → 제품 cutover → alias/queue | [`RenderGraphDependencySchedulingPlan.md`](RenderGraphDependencySchedulingPlan.md) |
| typed C# builder → immutable IR → native 조립 → Roslyn/제품 cutover | [`CSharpRenderPipelinePlan.md`](CSharpRenderPipelinePlan.md) |
| 현행 PBR 배선 | [`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) |
| Material Graph | [`BlenderMaterialGraphPlan.md`](BlenderMaterialGraphPlan.md) |
| 공통 node 저작·UI와 기존 graph 창 이관 | [`LatticeNodeSystem.md`](../design/LatticeNodeSystem.md), [`LatticeAdoptionPlan.md`](LatticeAdoptionPlan.md) — 독립 ImGui 예제 빌드·조작 게이트 통과. Editor 제품 통합은 별도 단계. 횡단 범위·공수 미산정이며 4.25의 40인일에 BT/Animator 이관을 포함하지 않음 |
| 시간축 | [`TemporalReconstructionPlan.md`](TemporalReconstructionPlan.md) |
| 라이트맵 | [`LightmapBakerPlan.md`](LightmapBakerPlan.md) |
| renderer probe/AO·shadow·display/post·Environment | [`RendererQualityPlan.md`](RendererQualityPlan.md) |
| GPU-driven·확률 조명·DXR 설계 | [`GpuFeaturePlanningPlan.md`](GpuFeaturePlanningPlan.md) |
| Meshlet/Mesh Shader raster + DXR 상세 배선 계약 | [`GpuDrivenMeshletDxrWiring.md`](../design/GpuDrivenMeshletDxrWiring.md) — GPU-1/GPU-3 설계 작성·진행. GD0~GD3 소스는 PR #123 으로 병합, 실행 증거는 DX12 한 장면뿐([구현 계획](GpuDrivenGeometryImplementationPlan.md)) |
| 셰이더 탐색 DXR 전환의 가설·채택 실험 | [`DxrShaderTraversalExperimentPlan.md`](DxrShaderTraversalExperimentPlan.md) — GPU-3/GPU-9 판정 입력, 실험 미착수·RTP-0/5/13 예산에 포함 |
| Path Tracing·Hybrid RT 제품 환경과 연구 적용 | [`PathTracingHybridPipelinePlan.md`](PathTracingHybridPipelinePlan.md) — PHASE 4.85, RTP-0~13 미착수·중앙 추정 208인일 |
| 백엔드 패리티 | [`BackendParityPlan.md`](BackendParityPlan.md) |
| 분리 전 4.75 항목 감사 | [`Phase475AxisClassification.md`](../analysis/Phase475AxisClassification.md) |

## 4. 완료선 분리와 검증

- **4.3:** 잘못된 producer/version/order 변이가 graph compile에서 실패하고, 기본
  제품 frame의 DX12 변경 전후 픽셀·validation이 `RG6`에서 통과한다.
- **4.6:** C# `Build()`가 같은 19 node를 IR로 선언하고 native Pass를 선택한다.
  C++ 고정 조립과 동일한 sealed frame 결과, 관리 호출 스레드 격리, reload 실패 시
  마지막 정상 세대와 fence 수명이 증명된다.
- **4.7:** UV1 차트·BVH 정답·직접/간접광 수렴·Editor 비차단·취소/재개를
  `LightmapBakerPlan`의 독립 게이트로 판정한다.
- **4.75:** `RND-1` probe/AO, `RND-2` shadow, `RND-3` display/post는 각각
  Material parity와 별도 golden·성능 결과로 닫는다.
- **4.8:** `GPU-1~3`은 세 기능의 설계, `GPU-9`는 공통 자원·지원/폴백·최소 수직
  슬라이스·기능별 구현 공수를 확정한다. 아직 기능 구현 완료선은 아니다.
- **4.85:** 공통 RT 기반과 Hybrid/PathTracing 두 제품 환경의 DX12 수용, ReSTIR PT Enhanced·tetrahedral cages의 구현/평가·채택 기록을 소유한다. 첫 hard shadow만으로 닫지 않고 연구 결과와 기본 환경 완료를 구분한다. 기존 RT0/RT1/HY0 및 EXP는 새 행과 대응해 중복 산정하지 않는다.

문서 재배치와 공수 추정은 빌드·GPU 런타임·픽셀 동등성의 새 증거가 아니다.
C#·4.9도 위의 계획 추정치를 사용하며 착수 시 실제 소비 표면·지원 장치 차이를 반영해 갱신한다.
Lattice 설계 초안과 UI 와이어프레임도 구현·검증 기성에 넣지 않는다.

## 2026-10-01 UI/환경 후속 추적

4.3 RG-V 4일과 4.75 RND-ENV 10일을 현재 원장에 산정했다. RG-V는 RG3부터 시작하고
RG6 제품 결과에서 닫으며 CSRP-5/6에서 같은 viewer로 C# IR을 표시한다. RND-ENV는
MAT-7 기반에서 배경/IBL/캐시 계약을 닫는다. W9(Phase21)와 14-LIVE/14-7(Phase14)은
이 4 계열 공수에 섞지 않는다. 정본은 [EditorRenderingSurfacesPlan.md](EditorRenderingSurfacesPlan.md),
[EnvironmentRenderingPlan.md](EnvironmentRenderingPlan.md)다.


2026-10-03 RG5-2 실행 순서 정책 분리 묶음은 Debug/Release 빌드·native 정책 검사·기존 RG1~4 GPU fixture로 완료했다. [검증 기록](../analysis/RenderRg5OrderPolicy.md). 제품 소비자/ReadWrite 이관·legacy 추론 제거·현행 전체 프레임 기준선·RG6 제품 정렬 전환은 아직 남아 있다.


2026-10-03 RG5-3 Deferred·SkyBox 소비 체인 이관 완료: 두 구성 빌드·native 선언 검사와 제품 기본 경로의 변경 전후/구성 간 16개 이미지 오차 0을 확인했다. [현행 기준선 및 남은 범위](../analysis/RenderRg5ConsumerMigration.md). 제품 기본 DeclarationOrder, RG5 전체 진행 상태는 유지한다.


2026-10-03 RG5-4 SSAO·SSGI·history 이관 완료: 두 구성 빌드·native 8개 조합·제품 회귀와 변경 전후/구성 간 16개 이미지 오차 0을 확인했다. [현행 기준선](../analysis/RenderRg5IndirectMigration.md). Forward+ 혼합 스트림과 남은 제품 선언/legacy 제거·RG6 기본 전환은 열려 있다.


2026-10-03 RG5-5 Forward+ 타일 버퍼·Code 경로 이관 완료: 두 구성 빌드·native 선언 검사·제품 회귀와 변경 전후/구성 간 16개 이미지 오차 0. [현행 기준선 및 혼합 경로 미완료 범위](../analysis/RenderRg5ForwardCodeMigration.md). Graph 자원 소유자 이관 및 mixed GPU 수용 후 guard 제거가 필요하며 RG5 전체/RG6 전환은 열려 있다.

2026-10-03 RG5-6 Lookup 소유자 완료: 캡처 출력 버전 반환·Bake Write·Ready Read 이관. 두 구성 최종 빌드·native·제품 반복 회귀 및 변경 전후/구성 간 각 16개 이미지 오차 0. [현행 검증과 종료 제한 재실행 기록](../analysis/RenderRg5LookupMigration.md). Refraction/Subsurface/Volume/GraphSurface 및 혼합 GPU 수용은 남아 있으며 RG5 전체 기성 0·RG6 미전환을 유지한다.

2026-10-03 RG5-7 Refraction·SSS·Volume 소유자 완료: 입력 캡처·배경 복사·Bake/필터·Volume 계수/합성 출력 버전과 최신 Lookup 입력 소비를 이관했다. 두 구성 빌드·native·제품 반복 회귀와 변경 전후/구성 간 각 16개 이미지 오차 0. [현행 검증 기록](../analysis/RenderRg5SpecialMigration.md). GraphSurface/Draw·DeclareBlended 갱신 출력 및 실제 혼합 GPU 수용이 남아 RG5 전체 기성 0·RG6 미전환을 유지한다.

2026-10-03 RG5-8 GraphSurface·Draw/반환 출력 완료: Shadow/GBuffer/Color/Blended의 결과를 블랙보드·Forward+로 전달하고 깊이 Copy→Modify, 색상 Modify, Mesh upload/world 버전·중복 읽기를 이관했다. 두 구성 빌드·native·제품 반복 회귀 및 변경 전후/구성 간 각 16개 이미지 오차 0. [현행 검증과 GPU 수용 경계](../analysis/RenderRg5SurfaceMigration.md). 실제 versioned SceneHost/mixed GPU 수용 및 guard 제거는 다음 조건이며 RG5 전체 기성 0·RG6 미전환을 유지한다.


2026-10-03 원격 물리·CSM/RG5 통합 후 현행 기준선 갱신 완료: Debug/Release 빌드 및 구성별 독립 2회 × 캡처 2회 회귀 통과, 통합 전후/구성 간 각 16개 이미지 오차 0, 현행 소스·바이너리 SHA-256 및 phaseComplete=true. Release 최초 포트 충돌 실패는 보존하고 v2 정상 반복 실행으로 수용했다. [통합 후 검증 기록](../analysis/RenderRg5IntegrationBaseline.md). 다음은 실제 versioned SceneHost·Code/Graph 혼합 GPU 수용 및 guard 제거이며 RG5 progress/기성 0과 RG6 미전환은 유지한다.


2026-10-03 RG5-9 실제 versioned SceneHost·Forward+ Code/Graph 혼합 GPU 수용 완료: 세 정책 × 24개 장면을 두 구성에서 실행하여 각 72 frames·정책 간 maxError=0·validation=0, cold/완료 geometry 재사용 및 최신 반환 writer를 확인했다. ExplicitVersioned Graph guard는 제거하고 SingleWriter 거부는 유지한다. 두 구성 빌드·기본 제품 반복 회귀·전후/구성 간 각 16개 이미지 오차 0·현행 해시 기준선 통과. [검증 범위 및 기록](../analysis/RenderRg5MixedGpuAcceptance.md). 제품 기본 DeclarationOrder, RG5 전체 progress/기성 0과 전체 SceneRenderer versioned GPU 수용/RG6 미전환은 유지한다. 다음은 나머지 소비자/ReadWrite 및 legacy 추론/adapter 제거다.

2026-10-03 RG5-10 Decal 접근·출력 버전 이관 완료: snapshot Write·입력 Read, GBuffer Modify/ReadWrite와 원본 텍스처 Read를 명시하고 반환 버전을 블랙보드·SceneHost 후속 입력에 연결했다. 두 구성 빌드·각 3정책/342 frames·7개 첨부 정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5DecalMigration.md). 제품 기본 DeclarationOrder, RG5 전체 progress/기성 0 및 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다.

2026-10-03 RG5-11 Sprite 접근·출력 버전 이관 완료: 독립 출력 Write·기존 HDR Modify/ReadWrite, 깊이 Read 및 원본 텍스처 중복 import/Read 제거를 연결했다. 두 구성 최종 빌드·각 3정책/18 frames·전체 RGBA 정책 간 오차 0·validation 0, 기본 제품 반복 회귀·변경 전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5SpriteMigration.md). 정적 cpp/h 호출은 제품 59·게이트 221(신규 검사 헤더 포함)이며 inl 전체 목록과 구분한다. 제품 기본 DeclarationOrder, RG5 progress/기성 0 및 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다. 다음은 화면 SSS/SSR 소비 체인 이관이다.

2026-10-04 RG5-12 화면 SSS/SSR 접근·출력 이관 완료: 두 블러 축과 반사 pass의 입력 Read·새 출력 Write/v0 및 선언 당시 핸들 캡처를 연결했다. 두 구성 빌드·각 3정책/48 frames·네 단계 전체 RGBA 정책 간 오차 0·validation 0, 실제 블러/반사 기여와 비활성/마스크/입력 누락 경로, 기본 제품 반복 회귀·전후/구성 간 각 16개 이미지 오차 0 및 현행 해시 기준선 통과. [검증 기록](../analysis/RenderRg5ScreenMigration.md). 정적 cpp/h 호출은 제품 59·게이트 224(신규 검사 헤더 포함)이며 inl 전체 목록과 구분한다. 제품 기본 DeclarationOrder, RG5 progress/기성 0과 전체 제품 versioned GPU 수용/RG6 미전환을 유지한다. 다음은 VolumetricFog 소비 체인 이관이다.
