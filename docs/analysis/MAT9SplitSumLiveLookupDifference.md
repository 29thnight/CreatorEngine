# MAT-9 live split-sum 정책과 Blender 차이의 해석

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 결정

live Scene의 material IBL은 split-sum을 최종 결과로 사용한다. 카메라가 멈추면 reference 적분으로 정교화되는 임시 품질 단계가 아니다. 현재 `lookupRuntimeEvaluation` 경로는 fragment에서 평가하며, `SceneLookupCache::Prepare`도 runtime evaluation에 명시적 approximate 정책을 요구한다.

고비용 reference의 RMS≤1%·p95≤1%·max≤5% 목표는 reference gate로 유지한다. live 근사에는 그 판정을 이전하지 않고 Blender 대비 차이를 별도로 기록한다. 이 정책은 “Blender parity 통과”나 오차 목표를 완화해 통과시킨 결과가 아니다.

## 구현상 차이

reference 경로는 view/tangent/Fresnel에 따른 방향 convolution과 RGB 정규화를 준비한다. live split-sum은 isotropic prefiltered environment와 근사 DFG를 사용하므로 anisotropic lobe, F82/film의 완전한 각도별 반응, sheen 방향성을 같은 정확도로 보존하지 않는다. [Scene lookup](../design/MaterialGraphSceneLookup.md)과 [runtime IBL shader](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang)가 현재 계약이다.

runtime evaluation frame의 sample buffer는 legacy root용 sentinel이며 전체 화면의 reference sample array가 아니다. `reserved0==1` 통계는 pixel cache/readback 대신 fragment 평가를 사용한 frame을 뜻한다. approximate/reference cache와 통계를 섞어 재사용률이나 bake 비용을 계산하지 않는다.

## 보존된 2026-10-04 대조

아래는 당시 Release DX12 고정 fixture의 relative RMS 평균/최대다. 현재 checkout에서 재측정한 값이 아니다.

| 묶음 | 조건 수 | reference 평균/최대 | split-sum 평균/최대 |
|---|---:|---:|---:|
| sun·white furnace | 24 | 0.08% / 0.18% | 1.94% / 14.27% |
| forest·autumn HDRI | 26 | 0.30% / 0.50% | 8.97% / 60.42% |
| bounded Volume | 8 | 0.07% / 0.10% | 0.07% / 0.10% |
| Special Surface | 18 | 5.63% / 34.79% | 5.64% / 34.79% |

원본 자료: [matched reference](MAT9SplitSumReferenceMatched.json) / [approximate](MAT9SplitSumApproximateMatched.json), [HDRI reference](MAT9SplitSumReferenceHdri.json) / [approximate](MAT9SplitSumApproximateHdri.json), [Volume reference](MAT9SplitSumReferenceVolume.json) / [approximate](MAT9SplitSumApproximateVolume.json), [Surface reference](MAT9SplitSumReferenceSurface.json) / [approximate](MAT9SplitSumApproximateSurface.json). [HDRI 대조 이미지](MAT9SplitSumApproximateHdri.png)도 역사적 자료다.

큰 HDRI 차이는 특히 anisotropy·metal·film에서 나타났다. Special Surface의 큰 투과 오차는 reference에서도 남은 폐곡면 transport 문제이며 split-sum만의 영향으로 분류하지 않는다. 평균/최대 요약은 perceptual score가 아니며 몇 highlight가 max를 지배할 수 있다.

## 유지할 검증

현재 host의 실제 live 선택, camera cut/disocclusion, environment/material/viewport 변경, exact/approximate 분리, runtime statistics 의미를 확인해야 한다. source-free package와 cold/warm PSO도 별도 gate다. 이후 품질 개선은 새 캡처/identity와 차이 수치로 보고하며 역사적 JSON·이미지를 바꾸지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneLookup.h`](../../Engine/RenderEngine/MaterialGraphSceneLookup.h)
- [`Engine/RenderEngine/MaterialGraphSceneLookup.cpp`](../../Engine/RenderEngine/MaterialGraphSceneLookup.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang)
- [`docs/analysis/MAT9SplitSumReferenceMatched.json`](MAT9SplitSumReferenceMatched.json)
- [`docs/analysis/MAT9SplitSumApproximateMatched.json`](MAT9SplitSumApproximateMatched.json)
- [`docs/analysis/MAT9SplitSumApproximateHdri.json`](MAT9SplitSumApproximateHdri.json)
