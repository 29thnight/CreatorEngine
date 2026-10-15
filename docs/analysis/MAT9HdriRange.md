# MAT-9 HDR range·cube 에너지 보존의 현재 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 원인과 보존한 진단

초기 HDR range 조사는 높은 양수 radiance를 64,000 근처로 자르거나 half-float cube에 저장하면 작은 태양의 에너지가 손실되는 문제를 분리했다. [autumn 적분](MAT9HdriRangeAutumnIntegrals.json)과 [forest 적분](MAT9HdriRangeForestIntegrals.json)은 원본/cube의 정확한 입체각 적분을 비교한 역사적 자료다.

해당 진단은 밝은 source의 에너지 손실과 CDF 해상도/샘플 분산을 구분하기 위한 것이다. diffuse integral이 맞아도 mirror angular detail이나 전체 재질 이미지는 틀릴 수 있다.

## 현재 데이터 흐름

environment source/cube와 prefilter는 float32 radiance 보존 경로를 갖는다. irradiance/BRDF의 형식과 모든 최종 surface sanitation 정책까지 동일하게 바꾼 것으로 해석하지 않는다. 유효한 양수 HDR는 보존하고 nonfinite/negative를 처리하는 환경 전용 sanitation을 사용한다.

`IblRectToCube`는 명시적 mip 0의 4×4 footprint를 cube solid-angle 가중으로 적분한다. 이 비용은 cook/환경 준비 시점의 변환이며 매 frame의 pixel shading 비용이 아니다. center sample 하나의 angular aliasing을 줄이지만 sharp reflection의 source 조회를 대체하지 않는다.

CEIBL003은 당시 float32 형식을 도입한 단계다. 현재 CEIBL005는 decoded source와 확장 reflection proposal까지 포함한다. 예전 cook hash/크기를 현재 배포 파일의 identity로 재사용하지 않는다. reader가 옛 형식을 읽는 것과 현재 recipe cache hit는 다른 판정이다.

## 역사적 이미지 판정

[range 단계 7조건](MAT9HdriRangeTargets.json)은 6/7이 목표 안에 있어도 `accepted=false`다. 고정 diagnostic subset만 검사했기 때문이다. forest mirror는 후속 [source sampling](MAT9MirrorSampling.md)에서 별도로 다뤘다. 이 파일의 false를 단순 실패 횟수로 해석하거나 6/7을 전체 MAT-9 통과로 보고하지 않는다.

## 현재 재검증 요구

source/recipe/checksum, 실제 format, atomic 저장, cold/warm cache, GPU upload/readback, CDF cell/PDF 일치, source/cube E/π를 각각 확인한다. reference는 원본 HDR/EXR을 읽고 engine만 새 cook을 소비하므로, reference manifest를 바꾸면 입력 identity가 유지되었는지 별도로 증명해야 한다.

이 문서 갱신에서는 환경 파일을 재쿠킹하거나 이미지·적분 JSON을 변경하지 않았다. 최종 fixed-suite 결과와 live 근사는 [수렴](MAT9HdriConvergence.md), [split-sum 차이](MAT9SplitSumLiveLookupDifference.md)에 분리되어 있다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/IblRectToCube.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/IblRectToCube.slang)
- [`Engine/RenderEngine/Assets/CookedEnvironment.h`](../../Engine/RenderEngine/Assets/CookedEnvironment.h)
- [`Engine/RenderEngine/Assets/CookedEnvironment.cpp`](../../Engine/RenderEngine/Assets/CookedEnvironment.cpp)
- [`Tools/AssetCooker/EnvironmentCooker.cpp`](../../Tools/AssetCooker/EnvironmentCooker.cpp)
- [`Tools/regression/measure-environment-integrals.py`](../../Tools/regression/measure-environment-integrals.py)
- [`docs/analysis/MAT9HdriRangeTargets.json`](MAT9HdriRangeTargets.json)
