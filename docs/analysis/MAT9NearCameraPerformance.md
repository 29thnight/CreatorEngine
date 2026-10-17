# MAT-9 근접 카메라 spike: 과거 진단과 현재 재검증

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 보존된 캡처가 말하는 것

[MAT9NearCameraPerformance.json](MAT9NearCameraPerformance.json)은 2026-10-01의 600-frame capture를 요약한다. `complete=false`, `unacked=2`, dropped event 0이다. drop이 없다는 이유로 미수집 stream과 경계 fragment까지 완전하다고 보지 않는다.

같은 47-pass 구성의 273개 GPU 제출에서 `LX.Scene.LookupBake` 평균 7.0051ms, median 0.3942ms, p95 87.7035ms, max 107.5056ms였다. [보존 그림](MAT9NearCameraLookup.png)은 해당 시점의 분포다. 전체 278개 lookup span의 평균과 273개 완전 구성의 평균을 섞지 않는다.

캡처에는 정확한 모델·카메라 궤적·viewport·실행 binary SHA가 없었다. 사용자 보고와 코드 구조로 근접 이동 조건을 해석한 것이며 파일만으로 그 환경을 복원했다고 주장하지 않는다.

## 당시 원인과 현재 변화

당시 reference lookup은 화면의 동일 pixel에 저장한 material/view 입력이 bit-exact일 때 재사용하고, miss에서 고비용 directional BRDF/environment 적분을 했다. 카메라 이동이 view·surface·texture 입력을 바꾸면 많은 covered pixels가 다시 계산되는 구조였다. 관측된 spike는 color shading보다 준비 compute에 집중되었다.

현재 live Scene은 `lookupRuntimeEvaluation`의 최종 split-sum을 사용한다. runtime sample buffer는 sentinel이며 옛 full-screen reference bake/readback 경로와 다르다. 따라서 이 spike 숫자는 현재 frame의 비용이나 재현된 현재 결함이 아니다. reference/diagnostic 경로의 존재만으로 live에서 해당 적분이 수행된다고 설명하지 않는다.

## 후보 실험에서 보존할 판단

과거 BRDF/environment 샘플 축소는 별도 shader root의 diagnostic이었다. 256/4096이 fixed HDRI 26조건에 들었다는 사실만으로 제품 기본값·모든 material speedup을 채택하지 않았다. 직접 pass timestamp 결과가 단순 sample-count 비례 속도 향상을 보장하지 않았기 때문이다.

전체 frame timestamp에는 준비/제출 사이 빈 시간이 포함될 수 있다. pass query와 shader variant·동일 RGBA 검증을 함께 사용해야 한다. validation off timing과 correctness validation on은 별도다.

## 현재 다시 확인할 것

1. current binary/source/host identity, 실제 viewport와 camera path, tier·material·environment를 저장한다.
2. live runtime-evaluation 정책과 native-real provenance를 확인한다. exact/approximate 통계를 혼합하지 않는다.
3. moving/stop/cut/disocclusion·near/fullscreen의 CPU/GPU median/p95/max와 완료 FPS를 기록한다.
4. geometry/PSO/upload·Special transport와 profiler UI/Scene lock wait를 분리한다. inclusive scope는 더하지 않는다.

GPU-driven 배칭만으로 IBL·transport 비용이 자동 해결되었다고 보지 않는다. 현재 [Scene 성능 runner](MAT9MaterialScenePerformance.md)와 [lookup 계약](../design/MaterialGraphSceneLookup.md)을 기준으로 새 baseline이 필요하며 이번에는 실행하지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneLookup.h`](../../Engine/RenderEngine/MaterialGraphSceneLookup.h)
- [`Engine/RenderEngine/MaterialGraphSceneLookup.cpp`](../../Engine/RenderEngine/MaterialGraphSceneLookup.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [`Tools/regression/summarize-material-profile.py`](../../Tools/regression/summarize-material-profile.py)
- [`Tools/regression/measure-material-scene-performance.ps1`](../../Tools/regression/measure-material-scene-performance.ps1)
- [`docs/analysis/MAT9NearCameraPerformance.json`](MAT9NearCameraPerformance.json)
