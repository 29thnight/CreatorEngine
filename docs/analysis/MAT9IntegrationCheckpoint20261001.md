# PHASE 4.25·GPU 설계 통합 체크포인트 — 2026-10-01

## 현재 결정

제품의 **BRDF 1024 / environment 4096**, **CEIBL005**, **SceneHost identity 10**을 유지한다.
PHASE 4.25와 MAT-9는 **열린 상태(`progress`, 32/34일)**다. diagnostic 256/4096의 제한된
고정 이미지 통과를 제품 샘플 축소나 실제 모델 성능 통과로 채택하지 않는다.

후속 RenderGraph·RHI 수명/동기화·필요한 시간축 기반을 진행하고, GPU-driven 설계에서
영속 GPU Scene·변경분 업로드·재질/PSO 배칭·간접 드로우와 IBL 재사용/갱신 배선을 함께
정한다. **실제 후속 구현과 동일 조건의 재측정 뒤** MAT-9 성능 게이트를 회수한다.
GPU-driven 설계 작성과 성능 수용은 별도다. SSS/투과 품질 오차도 별도 완료 조건으로 남는다.

BASE-0의 선행은 구현·검증된 **MAT-0~MAT-8 기반**이다. MAT-9 전체 수용을 일괄 착수
선행으로 받지 않아 MAT-9 → RenderGraph → GPU-driven → MAT-9의 착수 순환을 피한다.
현재 미달 결과는 알려진 baseline으로 기록하고 구조 변경의 회귀를 계속 검증한다.
산정 행·일수·완료 공수를 추가하지 않는다.

## 이번 통합에 포함한 제품 변경과 측정 기록

| 묶음 | 내용 | 판정 범위 |
|---|---|---|
| 공통 GGX·박막 | 금속/유전체 감쇠 분리, GGX 곱셈 보상, Core graph 계산 모델 일치, E/Eavg·dielectric LUT와 고정 golden | 고정 박막 on/off 각각 16/16 통과 기록; 전체 장면 수용은 아님 |
| HDRI 수렴 | float32 radiance/source 보존, solid-angle footprint, CDF, source bilinear, 1024+4096 MIS bank | 고정 forest/autumn 재질20·제어6 전체 26/26; RMS 최대 0.495878% |
| 쿠킹·캐시·bootstrap | CEIBL005 8-map 형식, proposal bank 영속화, GPU upload/readback, 기본 forest 쿠킹 데이터 | forest artifact 103159928 bytes, cook metadata SHA 일치 |
| Special Surface | artist 반경 환산, glass 준비 budget 일치, grazing normal을 준비 오류로 거부하던 문제 수정 | host10 Debug/Release 각각 18 frame·295717 checks·validation 0; 품질 7/18로 미수용 |
| Volume 기준 | 단일 산란에 맞춘 Blender recipe, 최종 제품 Volume 합성 readback·독립 적분 | 고정 균질 단일 산란 8/8 통과 기록; 다중/비균질 산란 수용은 아님 |
| 근접 카메라 성능 | 사용자 `.ceprof`, BRDF/environment 근사 후보, 직접 패스 timestamp와 readback 픽셀 보존 | 원인 조사·후보 실험; 실제 모델의 이동 FPS 개선 미완료 |

현재 유리/착색 투과의 문제 조건은 RMS 약 15.10~34.79%, SSS/혼합은 약 2.04~4.24%다.
일반 HDRI 범위의 낮은 오차를 전체 Material 지원 범위의 품질 통과로 확대하지 않는다.
기존 RMS≤1%·p95 normalized≤1%·max normalized≤5%·seed RMS≤0.25% 상한을 유지한다.

근접 캡처는 LookupBake 278표본 중 20개가 72.819~107.506ms다. Scene GPU 구간 273표본의
중앙값은 1.0024ms지만 p95는 88.4409ms다. CPU prepare/record와 profiler UI·씬 잠금 대기를
GPU 적분 spike와 분리해 기록했다. inclusive CPU 구간은 서로 더하지 않는다.
256/4096 후보는 HDRI 26/26·최대 RMS 0.982207%지만 안정적인 전 재질 speedup은 미입증이다.

상세 증거:

- [GGX·박막 대조](MAT9GgxClosureComparison.md)
- [전체 HDRI 수렴](MAT9HdriConvergence.md)
- [Special Surface 최종 검증](MAT9SpecialProfileCorrection.md)
- [Volume 기준·대조](MAT9SpecialTransportComparison.md)
- [근접 카메라 성능 조사](MAT9NearCameraPerformance.md)

## 다른 세션의 내용

| 세션 | 정리한 내용 | 완료로 해석할 범위 |
|---|---|---|
| GPU-Driven 배선 계획 찾기 | [Meshlet·Mesh Shader·DXR 상세 계약](../design/GpuDrivenMeshletDxrWiring.md), 공통 세대/ABI·cook·독립 가시성·bounded indirect·RG/RHI·fallback·abort/fence·구현 슬라이스 | 설계 문서 작성. GPU-1/GPU-3 진행, GPU-9 미완료, 실제 구현/지원 probe/성능 수용 미검증 |
| 프로파일링으로 프레임 급감 원인 찾기 | 노드 canvas 반복 계산·씬 잠금·표시 대기·재질 준비 분석과 Inspector/preview 개선 | 기존 제품 통합 기록으로 보존. [당시 성능 보고서](MAT9MaterialScenePerformance.md)의 FPS를 현재 1024/4096 근접 이동 결과로 재사용하지 않음 |
| RHI 스레드 캡쳐 미응답 분석 | 유휴 RHI 루프의 봉인 요청 처리 지점 부재와 `finish_pause`의 256회 yield 재시도 진단 | 소스 분석. 미응답은 봉인 acknowledgement 미수집이며 교착 확정이 아님. 수정/실기동 검증은 미완료 |
| C++ module 도입 검토 | 최신 소스 조사에서 EnhancedRenderGraph·AuthoringParsedDocument·Scene·DataSystem 후보와 헤더 전파 비용 구분 | 조사 기록. 최신 후보 평가에는 모듈 구현이나 새 빌드 시간 측정이 없음 |

GPU 기능의 설계·구현·측정 완료를 구분해 [GPU 기능 계획](../plans/GpuFeaturePlanningPlan.md)에
연결한다. 같은 세션의 [DXR 전환 가설·실험 계획](../plans/DxrShaderTraversalExperimentPlan.md)도
함께 보존한다. Volume 경계 탐색·굴절·SSR/SSGI·Fog visibility의 정확도와 AS 최초 생성/재사용/
갱신을 포함한 전체 비용을 비교하여 채택·조건부 채택·보류·기각을 판정한다. 실험은 미착수다.
GPU-driven 배칭이 현재 화면별 IBL 적분을 자동으로 줄인다고 가정하지 않는다.

## 이번 커밋의 범위와 검증 근거

- 통합 시작 시 직전 host10 실행의 589-file source snapshot과 대조하여 **제품 소스 drift 0**을
  확인했다. 이후 완료된 MAT-9 변경은 이 기준선이며 측정 도구의 pass timer만 추가됐다.
  커밋 준비 중 별도 세션 **페이즈 4.3 사전 정찰**이 BASE-0 구현을 시작했다.
  이번 커밋은 완료된 MAT-9·GPU 상세 설계·DXR 실험 계획의 기준 시점으로 고정한다.
  작성 중인 BASE-0 하네스는 작업 트리에 보존하며 미완료 구현을 완료 항목으로 포함하지 않는다.
- 두 작업이 함께 수정한 `EnhancedSceneRenderer.cpp`의 MAT-9 게시본은 직전 snapshot SHA-256
  `a74310852caf4ae04acf644e0601a74202c2fea24da21b8779d19bbe6ae159ee`와 일치한다.
  BASE-0의 캡처/그래프 진단 변경은 작업 트리에 남겨 해당 세션의 빌드·검증이 계속되도록 한다.
- 정상 Debug Editor runtime SHA-256:
  `f93b7f3b9bd84f0747f067caac841b275ec0684f7aea3d79c752e7b14e12ed1a`.
  [직전 전체 Debug 빌드와 Debug/Release 실행 기록](MAT9SpecialSurfaceVerification.json)을 보존한다.
- Release 측정 도구 SHA-256:
  `69ddbc4f304a0babe2cb5c31877f191d320f1905556d2a74d29edf1f89c069f1`.
  pass timer 추가 후 빌드·실제 실행과 네 diagnostic 조건의 전체 RGBA byte 일치를 확인했다.
- 14개 Python 구문·56개 JSON 파싱·6개 PowerShell 구문 검사와 변경 형식 검사를 통과했다.
  게시 전 생성된 GGX 표의 파일 끝 빈 줄 하나를 제거했다. 계산 내용은 검증본과 동일하다.
  대시보드 439항목 전체 파싱·유한 진행률·phase-meta 산수와 문서 웹 빌드도 통과했다.
  계획/대시보드 수정은 문서 검사이며 새 Editor/GPU 성능 수용으로 세지 않는다.
- 사용자 원본 프로파일 캡처와 고정 Blender 입력·픽셀·numeric golden, 라이선스/원본 SHA,
  비교 JSON·이미지를 함께 보존한다. 과거 결과의 바이너리 SHA나 판정을 현재 결과로 덮어쓰지 않는다.

정본은 [Material 계획](../plans/BlenderMaterialGraphPlan.md),
[후속 선행 그래프](../plans/RenderPhaseRoadmap.md),
[RenderGraph 계획](../plans/RenderGraphDependencySchedulingPlan.md)이다.
