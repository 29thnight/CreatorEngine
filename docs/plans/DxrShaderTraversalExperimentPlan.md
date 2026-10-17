# 셰이더 교차 탐색의 DXR 전환 가설·채택 실험

## 2026-10-08 제품 페이즈 연결

전체 제품 목표는 [PHASE 4.85 Path Tracing·Hybrid RT](PathTracingHybridPipelinePlan.md)다. 이 문서는 Hybrid RTP-5의 효과별 탐색·품질·비용 판정 입력이며 기본 PT나 ReSTIR PT Enhanced, tetrahedral cages의 전체 계획을 대신하지 않는다. RT 공통 기반은 RTP-1/2를 소비하고 재구현/중복 산정하지 않는다. EXP-2V 우선은 이 실험 가지에 적용하며 Hybrid hard shadow와 기본 PT 착수를 막지 않는다. EXP-4 결과는 RTP-5/13과 GPU-3/9에 함께 반환한다.

2026-10-08 공수 산정: EXP-0/1 효과 baseline은 RTP-0/5, EXP-2/3 효과 prototype·AS 비용은 RTP-5, EXP-4 판정은 RTP-5/13에 포함한다. 공통 기반 RTP-1/2와 제품 수명 RTP-12를 재계상하지 않는다. 전체 RTP 208인일 중 효과 확장 RTP-5는 28인일이며 EXP 별도 공수를 추가하지 않는다. 아래 10월 1일의 null 표기는 당시 미산정 이력이다.

**2026-10-01 · 계획만 작성 · 실험 미착수 · 공수 미산정(`days: null`).**
GPU-3의 효과 선정 근거와 GPU-9의 설계 채택 판정을 위한 실험 계획이다.
DXR 전환은 아직 확정하지 않는다. 기존 상세 배선의 첫 기능인 directional hard shadow는
유지하며, 아래 후보는 결과가 나온 뒤 별도 변경 결정으로 반영한다.
현재 GPU 설계 4행 9일에 prototype/실측 공수를 포함하지 않는다. 5.5일은 이전 산정이다.

## 1. 현재 근거와 검증 가능한 가설

| ID | 현재 코드/방식 | 가설 | 반증 조건 |
|---|---|---|---|
| H-V | `Includes/MaterialGraphSceneVolume.slang`의 LXVolumeEvents: 최대 128 triangle 전수 교차·이벤트 삽입 정렬, 16 object | triangle AS를 통한 경계 탐색은 geometry/ray 수 증가 시 탐색 비용을 줄일 수 있다 | AS build/update·반복 trace·event 처리 포함 전체 시간이 줄지 않거나 매질 의미가 달라짐 |
| H-R | `MaterialGraphSceneRefractionBake.slang`: background 탐색 뒤 medium 처리 | geometry 기반 hit로 화면 공간 굴절의 가림·화면 밖 hit 정확도를 개선할 수 있다 | 배경 radiance 평가/출구 경계 없이 교차만 바꿔 오차 증가, 전체 예산 초과 |
| H-S | `Ssr.slang` Raytrace와 `SsgiTrace.slang` TraceHiZ | RT는 화면 밖/가려진 geometry에서 화면 공간의 miss를 줄일 수 있다 | hit 재질/조명·denoise까지 포함한 비용 대비 품질 이점 미수용 |
| H-F | `FogAccumulate.slang`의 깊이 slice 적분 | fog 광원 visibility만 RT로 바꾸면 가림 품질이 개선될 수 있다 | 적분 비용이 지배적이거나 RT visibility 비용·노이즈가 이점을 상쇄 |

H-V는 현재 구현이 software BVH라는 가정이 아니다. H-R의 화면 공간/매질 경로와
SSR/SSGI/Fog의 실제 제품 호출·활성 설정은 EXP-0에서 다시 추적한다. 테스트 전 source를
바꾼 경우 최신 hash·실행 binary 포함 여부를 확인한 뒤 다시 baseline을 잡는다.
DXR이 triangle 교차/AS traversal을 담당해도 event 정렬, BRDF, radiance, 볼륨 적분은 남는다.
SDF/procedural AABB의 내부 marching은 사용자 intersection shader에 남으므로 별도 가설로
추가하지 않는 한 이 실험의 성공으로 대체 완료를 주장하지 않는다.

## 2. 실행 순서·선행·산출물

| ID | 작업 | 선행 | 산출물 | 공수/상태 |
|---|---|---|---|---|
| EXP-0 | 현재 소비 경로·지원 probe·측정 환경·수용 예산 봉인 | 해당 소스/제품 route 접근 | call-path 목록, scene manifest, budget/tolerance, device capability | null / todo |
| EXP-1 | 기존 compute/raster baseline과 단계별 GPU 시간 | EXP-0 | raw captures·CPU oracle·픽셀/교차/VRAM 결과 | null / todo |
| EXP-2V | H-V 독립 offscreen triangle-AS prototype | EXP-1, 필요한 GD0 RT 계약 | DXR boundary events와 동일 medium evaluator 비교 | null / todo |
| EXP-2R | H-R 단일 경계 hit + 후속 폐곡면 출구 실험 | EXP-1, GD0, 경계 의미가 겹치면 EXP-2V | hit/radiance/transport 분리 결과 | null / todo |
| EXP-2S | SSR 먼저, SSGI는 별도 비용/품질 실험 | EXP-1, GD0, sealed material hit 평가 | screen-space/RT/hybrid 비교 | null / todo |
| EXP-2F | fog visibility만 교체, 적분 유지 | EXP-1, GD0, light/medium oracle | visibility 품질·전체 fog 비용 비교 | null / todo |
| EXP-3 | 같은 프레임의 AS 공유·변경·두 view/in-flight 스트레스 | 통과한 EXP-2 후보, 제품 통합은 RG6 | 공유/독립 AS 비용, abort/retirement·통합 frame 결과 | null / todo |
| EXP-4 | 효과별 채택/조건부/보류/기각 판정 | 해당 후보 EXP-2/3 결과 | decision record와 GPU-3/GPU-9 상세 설계 변경 | null / todo |

먼저 EXP-0/1 → EXP-2V를 진행한다. 탐색이 실제 병목이 아니거나 정확도 gate를 통과하지
못하면 그 원인을 기록하고 후보 확장을 자동으로 계속하지 않는다. 다른 효과는 독립 근거로
선정한다. prototype은 독립 offscreen으로 시작할 수 있으며 RG6 전체를 일괄 선행으로 받지
않는다. 기존 제품 renderer에 설치하는 EXP-3는 RG6·관련 version/수명 계약을 소비한다.
GPU-2/C# 전체 완료는 실험의 선행이 아니다. Vulkan 지원/fallback·native 실행 비교는
[PHASE 4.9](BackendParityPlan.md)로 이관하며 DX12 실험 완료를 막지 않는다. DX12 결과를 Vulkan 성능 증거로 이월하지 않는다.

## 3. 비교 조건과 크기 sweep

같은 source commit+dirty diff hash, shader hash, cooker/asset generation, binary hash,
driver/device, Debug/Release, resolution, camera/light/material, ray/sample budget를 manifest에
고정한다. source 최신 binary 확인 후 캡처한다. Debug는 correctness/validation,
Release는 성능 판정의 주 근거다. GPU validation 켠 결과와 꺼진 성능 결과를 분리한다.

최소 1280×720/1920×1080, 정적·transform 이동·geometry 변형 scene, 1/2 view를 비교한다.
H-V는 triangle 12/32/64/128 및 object 1/4/16을 먼저 측정한다. 현재 상한 밖의
512/2048/8192 triangle은 독립 reference fixture에서만 비교하고 현행 shader의 clamp 결과를
정답으로 사용하지 않는다. extended sweep은 CPU oracle 또는 동등 의미의 bounded software
reference를 마련한 뒤 진행한다. active pixel/ray 수를 함께 sweep해 crossover를 찾는다.

AS cold build, warm static reuse, transform-only TLAS 갱신, geometry BLAS rebuild/update를
분리한다. update는 해당 capability와 build 조건을 검증한 경우만 측정한다. 공통 shadow AS를
이미 가진 경우의 incremental 비용과 이 효과 때문에 AS를 만드는 standalone 비용을 모두
보고한다. AS 공유를 무료로 가정하거나 cold 비용을 전체 평균에 숨기지 않는다.

각 조합 warmup 10초 이상, 측정 30초 이상을 기본으로 5회 교차 순서(A/B, B/A) 실행한다.
thermal/clock 상태가 불안정하면 안정화 후 반복한다. 외부 앱/진단 부하·캡처 오버헤드를
기록하고 CPU/GPU 작업을 같은 조건으로 맞춘다. p50/p95와 run간 분산 및 paired delta를
보고한다. 차이가 측정 변동 범위 안이면 성능 우위로 판정하지 않는다.

## 4. 정확도 계약

H-V는 모든 필요한 positive crossing, camera-inside winding, entry/exit beyond far plane,
겹치는 매질·shared edge·tangent·mirrored transform·같은 거리 events를 검증한다.
DXR any-hit 방문 순서는 정렬된 hit 순서가 아니다. 이벤트 수집 후 정렬/병합 또는 반복
nearest-hit 알고리즘 중 하나를 prototype에서 명시하고 ray epsilon으로 얇은 층을 건너뛰는
오류와 event overflow를 검증한다. unmatched/overflow는 명시적 실패/fallback이며 누락 성공이
아니다. RT scene geometry와 기존 software boundary의 winding·sidedness·object ID를 맞춘다.
삼각형 count 확장 자체를 품질 향상이라고 판정하지 않는다.

H-R은 교차 정확도와 radiance/transport를 분리한다. offscreen geometry에 screen color를
억지로 샘플링하지 않고 동일 hit material/light/environment evaluator로 비교한다. current
단일 경계 근사와 폐곡면 다중 경계 모델의 차이는 별도 품질 실험으로 표시한다.
H-S는 동일 on-screen geometry 비교와 offscreen/occluded 품질 fixture를 분리한다. ray hit
개수만으로 GI/반사 품질 성공을 판정하지 않는다. roughness/sample count/denoise 조건을
고정하고 경계 누락·temporal 안정성·reference radiance 오차를 보고한다.
H-F는 적분 sample/해상도/계수를 고정하고 visibility만 교체한다.

EXP-0에서 CPU double-precision triangle/event oracle과 승인된 reference 이미지의 tolerance,
최대 ray distance, bias, material 의미, memory/CPU/GPU 예산을 baseline 파일에 고정한다.
결과를 본 뒤 threshold를 느슨하게 바꾸지 않는다. tolerance 변경은 사유와 양쪽 재측정을
남긴다. GPU validation 0, generation 혼합/조기 해제 0, fallback 정상 동작은 필수다.

## 5. 비용 판정과 설계 반영

GPU timestamp로 upload/deform, BLAS/TLAS build, traversal/event sort, hit shading,
medium integration, denoise/history, composite와 전체 frame critical path를 각각 측정한다.
CPU prepare/record, peak VRAM·scratch·persistent AS/table, ray/event 수, AS 재생성 횟수도
보고한다. 분리된 pass 시간을 단순 합산해 병렬 frame 이득으로 주장하지 않는다.

| 판정 | 조건 | 설계 처리 |
|---|---|---|
| 채택 | 정확도/수명 gate 통과; EXP-0 예산 안; 개선이 반복 측정 변동보다 큼 | 해당 효과 DXR route·AS 공유·fallback을 상세 계약/후속 구현 슬라이스에 추가 |
| 조건부 채택 | 특정 규모·정적 reuse·hardware에서만 이점, 또는 품질 개선에 비용 증가 | crossover·지원/설정·비용을 명시하고 선택 모드로 추가; 기본값 전환은 별도 결정 |
| 보류 | 정확도는 통과하나 우위 불확실, 지원 hardware/예산 측정 부족 | 기존 route 유지; 재실험 조건만 남김 |
| 기각 | 의미 손실/안전 실패 또는 예산 초과·품질 이점 없음 | 제품 DXR route를 추가하지 않고 원인 기록 |

품질 향상과 성능 향상은 각각 판정한다. 품질이 좋아도 느려지면 성능 개선으로 표현하지
않는다. 후보별 결과가 다르면 일괄 전환하지 않는다. 기존 directional hard-shadow 첫
슬라이스를 교체하려면 EXP-4 decision에 이유·새 선행·회귀/공수 변경을 명시한다.

결과는 `docs/analysis/DxrShaderTraversalExperiment.md`와 기계 판독 manifest/metrics에
남길 계획이다. 현재 이 파일들은 생성하지 않으며 미실행 결과를 채워 넣지 않는다.
decision에는 가설, 현재 call path, 환경/해시, oracle, raw capture 위치, 정확도·비용 결과,
판정/지원 범위, fallback, 잔여 위험, 후속 구현 공수(null 또는 근거 있는 산정)를 포함한다.
GPU-9는 이 판정 없이 shader traversal의 DXR 전환을 확정 설계로 승격하지 않는다.

## 연결 문서

- [GPU 기능 설계 계획](GpuFeaturePlanningPlan.md)
- [Meshlet·DXR 상세 배선](../design/GpuDrivenMeshletDxrWiring.md)
- [Microsoft DXR 사양](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html)

이 문서는 실험 실행 계획이며 새 source/build/runtime/performance 완료 증거가 아니다.
