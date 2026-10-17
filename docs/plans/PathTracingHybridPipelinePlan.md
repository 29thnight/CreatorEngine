# PHASE 4.85 — Path Tracing·Hybrid RT 렌더 파이프라인

**2026-10-08 사용자 범위·공수 산정 · 14행 208인일 · 구현/실험 미착수 · 기성 0 · 잔여 208인일.**

목표는 공통 RT scene/material 기반 위에 **Path Tracing 파이프라인**과 **기존 래스터에 RT 효과를 결합하는 Hybrid 파이프라인**을 구성하는 것이다. ReSTIR PT Enhanced와 AMD tetrahedral cages의 구현·평가·제품 적용 판정을 후속 트랙으로 포함한다. directional hard shadow는 Hybrid의 첫 수직 슬라이스이며 페이즈 전체 완료선이 아니다.

PHASE 4.8은 GPU 기능의 교차 설계·예산·공수 검토를 계속 소유한다. 실제 RT/PT/Hybrid 구현과 두 연구의 평가·통합은 이 페이즈가 소유한다. 신규 208인일을 기존 설계 9인일에 넣지 않는다. PHASE 4 계열 산정 총계는 355→563, 기성 106 유지, 산정 잔여는 249→457인일이다. 기존 GD/Lattice 등 별도 미산정 범위는 남는다.

## 1. 현재 기반과 연결 지점

| 현재 근거 | 소비할 기반 | 이 페이즈에 남은 일 |
|---|---|---|
| `Render/Scene/EnhancedSceneRenderer.cpp`의 `BuildPipelineDesc` | native Pass/LivePipelineDesc 조립, view/frame/제출/표시 수명 | Raster/Hybrid/PathTracing 모드별 graph 구성과 전환 |
| `RHI/RHIEncoder.h`, `RHIResourceTypes.h` | backend 중립 명령·자원 계약 | typed AS, RT pipeline/table, dispatch 또는 ray-query 지원·오류 계약 |
| `GpuDrivenGeometryImplementationPlan.md` | GD0~GD3·HY1 일부 소스 병합 | RT0·RT1·HY0는 미착수. Mesh 지원을 RT 지원으로 간주하지 않음 |
| `GpuDrivenMeshletDxrWiring.md` | 공통 geometry 세대, 독립 가시성, AS/fence 수명 설계 | 실제 RT scene·hit material·제품 소비와 검증 |
| `BlenderMaterialGraphPlan.md`와 MaterialGraph 소비 경로 | Graph→ShaderMeta/Slang·공통 material generation | 임의 ray hit에서 BSDF evaluate/sample/PDF·texture LOD·light/environment 평가 |
| `RenderPhaseRoadmap.md`의 RG6 완료 기록 | 단일 queue 제품 graph·version/수명 기반 | AS build/read·reservoir/history·모드별 graph를 실제로 연결 |

2026-10-08 확인은 문서와 제한된 소스 대조다. 이 계획 작성으로 RT 기능·빌드·픽셀·성능 완료를 추가하지 않는다. 기존 수용 보고서의 소스 해시는 후속 변경의 실행 증거를 대신하지 않는다.

## 2. 세 모드와 공통 소유권

| 모드 | 영상 생성 | 역할·실패 처리 |
|---|---|---|
| Raster | 기존 GBuffer/Deferred/Forward·화면 공간 효과 | 기존 기능 회귀 기준과 RT 미지원 fallback |
| Hybrid | 래스터 primary visibility + 선택 RT 그림자/반사/GI/굴절/volume 경계/fog visibility | 효과별 출력·대체 범위·fallback 명시, 기존 조명과 중복 합산 금지 |
| PathTracing | camera ray부터 다중 bounce BSDF/light sampling으로 HDR radiance 생성 | 고샘플 progressive reference와 저샘플 interactive 프로파일, 같은 post/display/UI 경로 사용 |

동일 EnhancedSceneRenderer의 view/frame/submission/presentation owner와 RenderGraph를 사용한다. PathTracing은 별도 graph 구성으로 독립적인 영상 생성 경로를 갖되 별도의 제품 renderer·제출/표시 루프를 만들지 않는다. C# Pipeline IR는 native 계약이 준비된 뒤 연결하며 C# 전체 완료를 일괄 선행으로 받지 않는다.

공통 RT scene은 position/index·instance transform·material/light/environment generation을 밀봉한다. BLAS/TLAS, hit record/table, geometry remap, descriptor의 세대를 함께 검증한다. 카메라 가시 집합과 RT instance/shadow caster 집합은 독립이다. Mesh Shader 출력은 RT geometry 입력의 대체가 아니다. 변형·LOD·alpha 변경은 래스터/RT 의미 일치와 AS 갱신/재생성 정책을 갖는다.

RT material은 hit primitive/instance→submesh/material→공통 graph 산출물로 연결한다. raster point bake를 임의 hit 평가로 간주하지 않는다. normal/UV/texture LOD, sidedness/alpha, emission, BSDF sample/evaluate/PDF와 light/environment sampling PDF를 일치시킨다. 첫 범위는 opaque이며 masked·transmission·SSS·volume의 지원 범위는 명시적으로 확장한다. 지원되지 않는 재질은 준비 시 진단·모드 전환 또는 명시적 실행 불가로 처리한다. 조용한 재질 대체는 하지 않는다.

AS scratch/build/read와 reservoir/history의 의존·수명은 graph가 표현한다. 첫 제품 실행은 단일 queue이며 multi-queue는 Q0/RG7~9의 필요한 계약을 받는 후속 확장이다. 완료 ticket 전 해제, 서로 다른 view의 history 공유, abort된 candidate 게시를 금지한다.

## 3. 구현·연구 슬라이스

모든 행은 `todo`, 기성 0이다. 아래는 1인 전담 렌더링 엔지니어의 중앙 계획 추정치다. RTP-0에서 하드웨어·지원 범위·성능 예산을 고정하고 GPU-9에 추정 변경과 근거를 반환한다. 성능/품질 예산 확정과 구현 공수 초안 산정을 구분한다.

| ID | 작업·산출물 | 선행 | 완료 게이트 |
|---|---|---|---|
| RTP-0 | 현재 call path·장치 지원·세 모드 계약·baseline/예산·연구/코드 출처 봉인 | GPU-3 상세 계약, 현재 코드 | source/dirty diff/shader/asset/binary/driver manifest, fixture/tolerance·CPU/GPU/VRAM 예산, 지원·fallback 행렬 |
| RTP-1 | RT RHI·정적 BLAS/TLAS·table·graph AS 의존/수명 | RTP-0, GD0의 필요한 중립 계약, 제품 설치는 RG6 | CPU ray-triangle oracle, instance remap, cold/warm/transform 갱신, abort/retirement·validation 0 |
| RTP-2 | RT hit material·BSDF/light/environment 공통 evaluator | RTP-1, MAT 공통 산출물 | Graph→paired output→RT hit 소비, sampling/PDF·재질 의미·invalid generation 거부 |
| RTP-3 | RT/PT accumulation·재투영·denoising 입출력·history | RTP-2, 필요한 TR motion/view 계약 | raw/accumulated/denoised 분리, 움직임·disocclusion·cut/resize/reload reset, 두 view 독립성 |
| RTP-4 | Hybrid 첫 directional hard shadow | RTP-1/2, RG6 | direct-only 적용, off-camera caster, light 단위 raster fallback, 기존 RT1 기준 |
| RTP-5 | Hybrid 효과별 반사/GI·굴절·volume 경계·fog visibility 평가/통합 | RTP-2/3/4, 관련 EXP 결과 | 효과별 oracle/예산/합성·fallback, screen-space/RT/hybrid 비교 및 채택 기록 |
| RTP-6 | 기본 offscreen progressive Path Tracer·reference | RTP-1/2 | camera ray·다중 bounce·next-event estimation/MIS·Russian roulette, 고정 seed, 고샘플 수렴·에너지/PDF 검증 |
| RTP-7 | interactive PathTracing 제품 graph·Editor Scene/Game·Player | RTP-3/6, RG6 | 저샘플 trace→denoise→HDR/post/display, 지원 재질 범위, 예산·전환·실제 view 실행 |
| RTP-8 | ReSTIR PT 기본 reservoir 계약 및 Enhanced 개선 구현/평가 | RTP-3/6/7, 출처·라이선스 기록 | reuse off/base PT/ReSTIR PT/Enhanced 비교, temporal/spatial validity·편향/상관·오차/총비용 판정 |
| RTP-9 | tetrahedral cage 자산 전처리·cook/LOD·reference | RTP-0/1, 기존 deformation/cook 조사 | cage·piece/remap·정적 mini-BLAS 입력, cook 재현성, 변형/경계/LOD 오차와 기존 자산 보존 |
| RTP-10 | cage RT 변형·ray 변환·수명·두 모드 통합 평가 | RTP-2/9, 기존 BLAS deformation reference | shared static geometry와 instance별 cage, hit/normal/t/alpha 의미, 기존 변형 대비 정확도·전체 비용/메모리 |
| RTP-11 | Raster/Hybrid/PathTracing 설정·진단·프로파일 | RTP-4/7, 선택 가능한 RTP-5/8/10 결과 | frame seal에서 모드 확정, unsupported 이유·fallback 표시, sample/bounce/history/예산·저장/재로드 |
| RTP-12 | 세 모드 제품 회귀·스트레스·계측 수용 | RTP-4/7/11, 통합 대상 연구 결과 | Mesh×RT, 두 view/in-flight, reload/resize/abort/device loss, D/R 실제 Editor/Player·validation 0·정상 종료 |
| RTP-13 | 효과·연구별 채택 판정과 최종 페이즈 감사 | RTP-5/8/10/12의 결과 또는 사유 있는 보류/기각 기록 | 공통 RT·Hybrid·기본 PT 제품 수용, 두 연구의 재현/평가/통합 판정, 지원 행렬·raw evidence·잔여 범위/공수 |

RTP-8과 RTP-9/10은 각각 샘플링 알고리즘과 변형 geometry 표현을 소유한다. 서로를 선행으로 묶지 않는다. 연구를 계획에 포함한다는 것은 실제 prototype·비교·채택 결정을 수행한다는 뜻이며 논문의 수치를 우리 제품 수용 증거로 쓰지 않는다. 기본 두 파이프라인의 구축은 필수이고, 연구의 기본값 채택은 실측 결과에 따른다. 보류 시 미완료 범위·재실험 조건·담당 후속 행을 남기며 구현 완료로 표시하지 않는다.

## 4. 실행 순서와 기존 ID 대응

```text
RTP-0 → RTP-1 → RTP-2 ┬→ RTP-4 → RTP-5 (Hybrid 효과별 EXP 결과 소비)
                     ├→ RTP-6 → RTP-7 → RTP-8 (ReSTIR PT Enhanced)
                     └→ RTP-3 ─────┘ (RTP-5/7/8의 history·denoising)
RTP-0/1 → RTP-9 → RTP-10 (tetrahedral cages, 두 파이프라인에서 공유)
RTP-4/7 + 선택 효과/연구 결과 → RTP-11 → RTP-12 → RTP-13
```

기존 상세 배선의 **RT0는 RTP-1/2**, **RT1은 RTP-4**, **HY0는 RTP-12의 Mesh×RT 결합 검사**로 대응한다. 해당 ID를 별도 구현 공수로 중복 합산하지 않는다. HY1의 기존 GD 변형/LOD 범위는 유지하고 RT 변형·cage 소비는 RTP-10에 연결한다.

`DxrShaderTraversalExperimentPlan.md`의 EXP-0/1은 효과별 baseline, EXP-2~4는 RTP-5 채택 입력이다. RT 공통 기반은 RTP-1/2에서 한 번 만들고 EXP에서 재구현하지 않는다. EXP-2V의 Volume 경계 실험 우선 순서는 그 실험 가지에 적용하며 Hybrid hard shadow나 기본 PT 착수를 막지 않는다.

TR의 motion/history 의미는 필요한 부분만 소비한다. RT denoising과 reservoir는 이 페이즈가 소유하고 TU/FG 구현은 선행이 아니다. MAT-9 최종 완료도 일괄 선행이 아니지만 미달 재질 의미·품질을 baseline에서 숨기지 않는다. Vulkan 실행/픽셀/성능 비교는 PHASE 4.9가 단독 소유하며 DX12 완료를 역으로 막지 않는다.

## 5. ReSTIR PT Enhanced 적용 계약

[NVIDIA 연구](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/)는 광경로의 시간·공간 재사용을 개선한다. RTP-8은 기본 path tracer와 RT material/light 평가, path/reservoir 기록, 재투영·visibility 재검증·denoising 뒤에 배치한다. 기존 SSGI에 단순 필터로 추가하지 않는다.

논문의 reciprocal neighbor selection, footprint 기반 reconnection, duplication map, direct/GI reservoir 통합을 개별 비교 가능한 변경으로 구현·평가한다. shift mapping과 sampling PDF/weight 의미를 기록하고, 움직이는 geometry/light·disocclusion·얇은 표면·specular/transmission·반복 무늬의 실패 fixture를 둔다. 품질과 성능을 따로 판정한다. 논문의 2~3배 결과를 엔진 목표 성능 보장으로 삼지 않는다.

고정 장면/seed에서 기본 PT의 고샘플 reference와 저샘플 base/ReSTIR/Enhanced를 비교한다. 동일 spp의 오차와 동일 GPU 예산의 오차를 모두 보고한다. reservoir 메모리·재사용·추가 visibility ray·denoise를 포함한 전체 비용, 시간축 안정성·잔상·상관·편향을 남긴다.

[사용자 북마크](https://x.com/panoskarabelas/status/2103548360384782392)는 참고 연결이다. X 원문은 이번 확인에서 확보하지 못했으며 Spartan의 특정 Enhanced 구현 버전은 검증하지 않았다. 구현의 기본 경로는 논문 기반 자체 작성으로 계획한다. 외부 코드를 가져오는 경우 정확한 commit·파일·license/notice·허가 증거를 RTP-0/8에 남긴다. [현재 Spartan license](https://github.com/PanosK92/SpartanEngine/blob/master/license.md)는 상업 사용에 서면 계약과 협의한 지급 조건을 요구하고 이전 MIT 배포본의 이미 부여된 권리는 철회하지 않는다고 명시한다. 공개 저장소 접근 자체를 코드 사용 허가로 간주하지 않는다.

## 6. AMD tetrahedral cages 적용 계약

[AMD 설명](https://gpuopen.com/learn/how-tetrahedral-cages-significantly-reduce-bvh-memory-usage/)과 [기술 설명](https://gpuopen.com/learn/ray-tracing-massive-amounts-animated-geometry/)을 RTP-9/10의 연구 입력으로 둔다. [사용자 북마크](https://x.com/wccftech/status/2101711735363138039)의 수치는 원문의 장면·장치·LOD 조건과 함께 기록한다.

조밀한 mesh를 tetrahedron별 조각으로 전처리하고 정적 mini-BLAS를 공유한다. instance마다 cage를 변형하고 ray를 rest-pose 공간으로 옮겨 교차를 구하는 방식을 평가한다. 연결성을 유지하는 식생 변형을 첫 fixture로 삼는다. 이는 조명 샘플 재사용과 별개이며 Hybrid/PT가 같은 RT geometry route를 소비할 수 있도록 계획한다.

cage 변형과 raster 변형의 시각적 일치, 경계 clipping·ray distance·normal·winding·alpha coverage·LOD 전환, degenerate/inverted cage 거부와 fallback을 검증한다. exact deformation+BLAS update/rebuild, static reuse, cage 경로를 동일 의미/오차 예산에서 비교한다. 전처리/cook 크기, 전체 geometry/cage/AS/scratch VRAM, CPU와 GPU update/traversal 비용을 각각 남긴다.

AMD의 약 80GB→1.7GB는 특정 대규모 식생 장면의 BVH 메모리 비교다. 게임 전체 VRAM 절감량이나 우리 엔진 예상 결과로 사용하지 않는다. cage 제어에 따른 변형 근사와 품질 제한을 지원 범위에 명시한다. 실제 구현 기법의 DXR 기능·SDK/장치 요구는 prototype 전에 확인하며 최신 특수 기능 지원을 전체 RT 기반의 필수 조건으로 만들지 않는다.

## 7. 공통 수용과 공수 확정

RTP-0에서 tolerance와 목표 장치·해상도·sample/bounce budget·CPU/GPU/VRAM 상한을 먼저 정한다. 정적/이동/변형·1/2 view, AS cold/warm/transform/geometry 변경, raster/Hybrid/PT, 연구 on/off를 구분한다. Debug는 correctness/validation, Release는 성능 판정의 주 근거이며 validation off 성능과 분리한다.

고샘플 선형 HDR reference, 기하/재질 oracle, RMSE·최대 오차·수렴·시간축 오차와 raw trace/denoise/final 이미지를 보존한다. PT와 Raster의 픽셀 일치를 강요하지 않고 각 모드의 의도한 transport를 정답과 비교한다. unsupported·allocation/table 실패·stale generation·overflow·history reset·record abort·device loss를 검사한다. fallback에서도 같은 원본 재질을 사용하며 실패 원인을 표시한다.

CPU prepare/record, upload/deform, AS build/update, trace, reuse, shading, integration, denoise, composite와 전체 frame critical path의 p50/p95·변동을 측정한다. AS 공유의 incremental 비용과 standalone 비용을 분리하고 성능 우위가 측정 변동 안이면 보류한다. 수렴 개선·품질 개선·성능 개선을 별도로 보고한다.

최종 완료는 두 제품 파이프라인의 실제 DX12 D/R 실행과 안전/품질/예산 수용, 연구별 결과·채택/조건부/보류/기각 기록, 지원·fallback·재질 범위·미완료 조건의 명시다. RTP-13은 기본 환경 미구현을 연구 기각으로 대신 닫을 수 없다. 연구 보류/기각은 결정 작업의 종료와 구현 미완료를 구분한다.

공수 초안은 아래 §8로 산정했다. RTP-0 및 첫 prototype 결과 뒤 GPU-9에서 지원·범위·불확실성과 변경 추정치를 검토한다. 기존 GD/RT/HY 및 EXP와 중복 합산하지 않는다. 문서 작성·소스 병합·셰이더 컴파일은 실행/품질/성능 수용을 대신하지 않는다.

## 8. 공수 산정 — 2026-10-08

1인 전담 렌더링 엔지니어, 1인일=8시간의 계획 추정다. 구현·행별 검증·통합 결함 대응을 포함하며 실제 투입 시간이나 확정 납기가 아니다. 기존 RG6·native Pass/표시 루프·공통 graph 산출물·cooker를 재사용한다. 현재 중립 RHIEncoder/ResourceTypes에는 Mesh 계약은 있으나 이번 확인에서 RT dispatch/AS 계약은 확인되지 않았고, 기존 raster bake를 임의 hit의 BSDF sampling으로 전용할 수 있다는 증거도 없어 신규 비용으로 잡았다.

| ID | 인일 | 구현·검증 분해와 근거 |
|---|---:|---|
| RTP-0 | 5 | 소비 경로/지원/출처 조사 2 + 모드·fixture·측정 예산/manifest 3. GPU-3/9의 교차 설계 비용은 재계상하지 않음 |
| RTP-1 | 18 | 중립 RT 타입·DX12 AS/pipeline/table 7 + scene/remap·graph 의존/수명 6 + oracle·cold/warm/실패 검증 5 |
| RTP-2 | 20 | RT hit→graph material·texture/normal/LOD 7 + BSDF evaluate/sample/PDF·light/environment/MIS 입력 8 + 의미·세대·재질 검증 5 |
| RTP-3 | 16 | accumulation/reprojection/history 5 + 기본 temporal/spatial denoising·입출력 7 + 움직임/reset/두 view 검증 4. TR 공통 motion 생산은 별도 |
| RTP-4 | 6 | directional hard shadow·Deferred 합성 3 + off-camera caster·bias·fallback 검증 3 |
| RTP-5 | 28 | 공통 효과 baseline/AS 공유 스트레스·EXP 판정 4 + 반사 5 + GI 6 + 굴절 5 + volume 경계 5 + fog visibility 3. 각 효과 prototype·oracle·채택 효과 통합 포함 |
| RTP-6 | 12 | camera/multibounce/NEE/MIS/Russian roulette 7 + 고샘플 reference·수렴/에너지 fixture 5. 재질 evaluator는 RTP-2 재사용 |
| RTP-7 | 10 | interactive 제품 graph·HDR/post/display 6 + Scene/Game/Player 기능 smoke·저샘플 예산 검증 4. denoise는 RTP-3 재사용 |
| RTP-8 | 28 | 기본 ReSTIR PT reservoir·shift/weight 10 + Enhanced 개선 구현 10 + 동일 spp/예산·편향/상관/시간축 비교 8 |
| RTP-9 | 18 | cage 생성/mesh 분할·remap·LOD 10 + cook/세대/기존 자산 보존 4 + 변형·경계 reference fixture 4 |
| RTP-10 | 18 | 기존 deformation+BLAS update/rebuild 비교 경로 4 + cage 변형/ray 변환/RT geometry route 8 + clipping/normal/t/alpha·두 모드·메모리/전체 비용 검증 6 |
| RTP-11 | 6 | 모드/프로파일·설정 저장/재로드·진단 4 + frame seal 전환·fallback 검증 2. Editor 전체 UI 재설계는 별도 |
| RTP-12 | 18 | 통합 하네스/계측 4 + D/R 세 모드·Editor/Player·view/in-flight 스트레스 6 + 실패/수명·제품 회귀 결함 대응 8. 행별 알고리즘 oracle와 중복하지 않음 |
| RTP-13 | 5 | 효과/연구 결과·채택/잔여/지원 감사 3 + 재현 패키지·공수/정본 최종 정합성 2. GPU-9 설계 완료와 별도 제품 결과 감사 |
| **합계** | **208** | **기성 0 · 산정 잔여 208** |

| 묶음 | ID | 인일 |
|---|---|---:|
| 공통 RT·재질·history/denoise | RTP-0~3 | 59 |
| Hybrid 첫 그림자·효과 확장 | RTP-4/5 | 34 |
| 기본 PT·interactive 제품 연결 | RTP-6/7 | 22 |
| ReSTIR PT Enhanced | RTP-8 | 28 |
| tetrahedral cages 전처리·runtime | RTP-9/10 | 36 |
| 설정·통합 수용·최종 판정 | RTP-11~13 | 29 |
| **합계** | | **208** |

기본 환경 범위(RTP-0~4/6/7/11~13)는 116인일이다. Hybrid 효과 확장 28 + ReSTIR 28 + cage 36 = 후속 구현/연구 92인일이며 모두 현재 208에 포함한다. 연구가 보류/기각되더라도 prototype·측정·판정에 투입되는 예산을 자동으로 0으로 줄이지 않는다.

기본 환경 116에 약 ±30%, 후속 구현/연구 92에 약 ±50%의 계획 불확실성을 적용하면 약 127~289인일이다. 관리 범위는 반올림해 **130~290인일**로 둔다. 통계적 신뢰구간이나 별도 합산할 예비 공수가 아니며 중앙 원장은 208로 기록한다. 1인 주 5일 전담이면 중앙값 약 42작업주다. 휴일·다른 업무·선행 TR 계약 개발·허가/장치 대기·연구 재설계는 달력 기간에 별도로 영향을 준다.

산정 범위는 DX12 D/R, 단일 queue, 기본 opaque/masked·emissive·일반 dielectric 재질, progressive/reference 및 interactive 모드, 식생 중심 connectivity-preserving cage다. RT 재질 확장은 이 범위에서 검증하며 모든 SSS·복잡한 다중 매질·임의 topology 변경·스펙트럴/양방향 PT까지 완료한다는 예산은 아니다. MAT-9·TR 전체·TU/FG·C# 저작·multi-queue·GD 전체 잔여·Vulkan 실제 비교/RT 신규 backend 소비 구현·외부 라이선스 비용은 기존 담당 페이즈 또는 범위 확정 후 별도 산정한다. 중립 RHI 계약과 미지원 오류/fallback은 208에 포함한다. Vulkan 실제 RT 구현 예산의 담당은 GPU-9에서 확정하고 4.9에는 실행 비교만 인계한다.

EXP-0/1의 효과별 baseline은 RTP-0/5, EXP-2의 효과 prototype과 EXP-3의 효과별 AS 비용은 RTP-5, EXP-4의 효과 판정은 RTP-5/13에 포함한다. 공통 AS/RHI·material은 RTP-1/2, 공통 제품 수명 스트레스는 RTP-12다. EXP 공수를 별도로 더하지 않는다. RTP-5 내부 중앙 배분은 RT 채택 성공을 보장하지 않으며 결과를 보고 필요한 잔여 통합만 재산정한다.

RTP-0 종료, RTP-1/2 최초 RT hit, RTP-6 고샘플 reference, RTP-8/10 최초 연구 prototype에서 재산정한다. 상한을 넘는 재질·cage 품질·SDK/장치 요구가 발견되면 새 근거와 추가 범위를 기록하고 합계를 갱신한다. 현 추정으로 GPU-9 완료나 runtime 기성을 올리지 않는다.

## 연결 정본

- [현재 원장](RenderPhaseRoadmap.md), [공수 원장](RenderPhaseEffortEstimate.md)
- [GPU 설계](GpuFeaturePlanningPlan.md), [Meshlet·DXR 배선](../design/GpuDrivenMeshletDxrWiring.md)
- [효과별 탐색 실험](DxrShaderTraversalExperimentPlan.md), [시간축 기반](TemporalReconstructionPlan.md)
- [백엔드 비교](BackendParityPlan.md), [Pipeline 목표 구조](../design/RenderPipelineTargetArchitecture.md)
