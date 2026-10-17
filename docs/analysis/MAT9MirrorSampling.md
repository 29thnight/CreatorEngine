# MAT-9 mirror source sampling과 픽셀 중심 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## sharp reflection에서 필요한 경로

roughness 0의 mirror는 매우 좁은 방향을 읽는다. cube 변환의 angular resampling이 diffuse 적분에는 작아도 mirror에서 큰 오차를 만들 수 있다. 현재 precomputed reference 경로는 retained decoded source가 있으면 `BakeSourceRadiance(reflect(-view,normal))`를 사용한다.

source 조회는 equirectangular 방향 변환 뒤 texel center 기준 `uv*size-0.5`로 계산한다. longitude는 wrap, latitude는 clamp하고 네 texel을 float lerp한다. hardware sampler의 고정 subtexel 정밀도가 강한 HDR emitter에서 보이는 문제와 cube resampling을 분리하기 위한 경로다.

geometry의 normal/view도 실제 raster pixel center와 맞아야 한다. 동일 mesh를 쓴다는 사실만으로 vertex interpolation이나 카메라 ray가 reference와 일치하는 것은 아니다. emission coverage·normal/view control을 먼저 확인한다.

## cook 계약

CEIBL004가 decoded source 보존을 도입했고 현재 CEIBL005가 이를 유지하며 reflection sample bank를 확장했다. source 없는 이전 cache를 읽을 수 있어도 sharp-source 품질을 동일하게 제공한다고 주장하지 않는다. source identity와 recipe에 따라 현재 authoring cache를 재생성해야 한다.

이 source mirror 경로는 reference convolution의 계약이다. 현재 live split-sum의 prefiltered reflection이 모든 mirror/film/anisotropy 조건에서 같은 결과라는 뜻은 아니다.

## 보존된 판정

[MAT9MirrorTargets.json](MAT9MirrorTargets.json)은 fixed 7-condition subset에서 7/7 목표 충족을 기록하지만 `accepted=false`다. 파일이 명시하듯 전체 MAT-9를 닫을 수 없는 부분집합이다. forest mirror RMS가 약 3.1285%에서 0.4693%로 줄었다는 값은 해당 당시 동일 reference 대조의 역사적 결과다.

[비교 JSON](MAT9MirrorComparison.json)과 [sampling 기록](MAT9MirrorSampling.json)을 보존하며 새 현재 수치로 올리지 않는다. 독립 reference는 131072 samples, seed 0/11의 고정 조건이었다.

## 남은 검증

새 환경의 seam/pole, 밝은 작은 광원, roughness 0 근방 전환, source 없는 legacy cache, camera movement를 확인해야 한다. fixed controls 통과는 전체 rough convolution·textured material·live 성능을 대체하지 않는다. [전체 HDRI 수렴](MAT9HdriConvergence.md)이 후속 범위를 설명한다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang)
- [`Engine/RenderEngine/Assets/CookedEnvironment.h`](../../Engine/RenderEngine/Assets/CookedEnvironment.h)
- [`Tools/regression/material_matched_image_probe.cpp`](../../Tools/regression/material_matched_image_probe.cpp)
- [`docs/analysis/MAT9MirrorTargets.json`](MAT9MirrorTargets.json)
- [`docs/analysis/MAT9MirrorComparison.json`](MAT9MirrorComparison.json)
- [`docs/analysis/MAT9MirrorSampling.json`](MAT9MirrorSampling.json)
