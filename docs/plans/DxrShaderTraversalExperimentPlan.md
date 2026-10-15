# DXR 탐색 대체의 효과별 실험 계획

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 목적과 현재 상태

이 계획은 현재 shader 탐색을 RT로 교체하면 전체 비용·품질이 나아지는지 판단하기 위한 미구현 실험이다. 현재 live SSR/SSGI·volume/refraction·fog 배선이 DXR을 사용한다는 뜻이 아니다. 공통 AS·material 기반은 PathTracingHybridPipelinePlan의 RTP-1/2가 소유한다. 실험 가지에서 volume을 먼저 다뤄도 Hybrid hard shadow와 기본 PT의 착수를 막지 않는다.

## 네 가지 가설

- H-V: volume의 triangle 경계 탐색을 RT 후보 수집으로 바꾸되 crossing event·정렬·밀도 적분 의미를 보존한다
- H-R: 굴절의 교차 hit와 그 위치의 radiance 평가를 분리한다. closed boundary의 exit 및 여러 layer를 검사한다
- H-S: SSR 화면 안 hit를 먼저 유지하면서 화면 밖/가려진 후보를 RT로 보완할지, SSGI까지 확장할지 별도로 판단한다
- H-F: fog의 visibility 질의만 바꾸고 기존 적분·history 의미는 우선 유지한다

any-hit 호출 순서를 깊이 정렬로 믿지 않는다. 명시 sort/merge 또는 반복 nearest query가 필요하다. 얇은 layer·중첩 경계·epsilon·unmatched crossing·overflow를 정의하고 누락을 성공으로 숨기지 않는다. 현재 shader의 고정 수용량을 넘는 fixture는 잘린 기존 결과가 아니라 독립 oracle과 비교한다.

## EXP-0~4

EXP-0에서 실제 source·capability·오차와 비용 budget, unsupported를 고정한다. EXP-1은 동일 장면의 현재 탐색 baseline을 만든다. EXP-2V/R/S/F는 offscreen prototype로 효과를 분리하고 다른 shading 변경을 섞지 않는다. EXP-3은 공통 AS, static/transform/deform, 1/2 view와 in-flight 수명으로 통합한다. EXP-4는 결과를 채택/조건부/보류/기각으로 정리해 RTP-5/13과 GPU-3/9에 반환한다.

## 측정 계약

cold build와 warm update, 독립 AS 총비용과 이미 공유하는 AS의 증분비용을 모두 낸다. upload·deform·BLAS/TLAS·trace·event 처리·shading/적분·denoise·composite의 CPU/GPU와 VRAM을 포함한다. 해상도·geometry/volume/light 분포를 고정한 paired 반복과 독립 반복에서 p50/p95·이상치·실패를 보고한다. trace 구간 하나만 빠르다고 채택하지 않는다.

EXP의 비용은 기존 RTP-0/5/13 범위에 포함한다는 계획을 유지한다. 공통 기반 RTP-1/2와 수명 RTP-12를 중복 계상하지 않는다. 기존 RTP-5 28인일은 계획 추정이고 이번 감사에서 실험·속도·정확성 결과는 생성하지 않았다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSRPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSRPass.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedSSGIPass.cpp)
- [Engine/RenderEngine/Render/Passes/Lighting/EnhancedVolumetricFogPass.cpp](../../Engine/RenderEngine/Render/Passes/Lighting/EnhancedVolumetricFogPass.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
