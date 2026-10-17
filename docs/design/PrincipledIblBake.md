# Evaluated-point IBL bake API

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 무엇을 준비하는가

`IblBaker`는 공간·시선이 고정된 evaluated point에 대한 환경 반사 결과를 준비한다. 모든 재질에 공유할 수 있는 2D material LUT가 아니다. texture, normal, view, layer 또는 environment가 바뀌면 새 요청이 필요하다.

`IblBakePoint`는 176 bytes이며 material factor와 viewTier를 가진다. viewTier.w 0은 legacy Core, 1은 compensated Principled GGX이고 authoring Tier와 별개다. finite 값, magnitude ≤1e6, nonzero front-facing view와 NdotV≥1e-4를 요구한다. `IblBakeSample`은 144 bytes로 base/coat integral·average와 여러 환경 convolution을 묶는다.

## 호출과 GPU 연결

`Initialize`는 이미 검증/쿠킹된 compute bytecode와 optional compile identity를 받아 candidate pipeline을 만든다. runtime Slang compiler를 설치하지 않는다. `Record`는 CPU point, `RecordGpu`는 `SurfaceBatch`를 입력으로 쓰며 `PrepareGpu`/`Declare`로 RenderGraph에 연결할 수 있다. 최대 point는 4096이다.

환경 texture는 ShaderResource로 준비되어 있어야 한다. graph 경로에서는 입력 producer와 output read 의존성을 선언한다. 반환 owner는 environment 및 GPU point owner를 pin한다. `Matches`/`MatchesGpu`는 device, environment generation, exact input을 비교한다.

## 결과 의미와 실패

model 1의 singleScatter에는 GGX compensation이 포함되어 있으므로 소비 시 또 Lambertian compensation을 더하지 않는다. base reflection은 RGB Fresnel-weighted sum으로 정규화하며 coat/sheen이 base convolution을 무조건 재사용하지 않는다.

invalid GPU point는 `baseAverage.w = -1`과 zero radiance로 드러난다. 기록 완료는 GPU 완료나 Scene acceptance가 아니다. completed point/source readback을 검증한 후 독립 Scene packet을 게시한다. 실패 초기화/준비는 기존 accepted 결과를 훼손하지 않는다.

## 수명과 제품 경계

결과와 environment owner를 마지막 submission 완료까지 유지하고 device 종료 전에 해제한다. CPU Texture owner 외에 native texture cache residency/touch가 필요하다. live METEOR의 runtime fragment split-sum은 이 full point bake 배열을 매 frame 만드는 방식과 다르다. [Scene lookup](MaterialGraphSceneLookup.md)을 제품 동작의 기준으로 삼는다.

숫자 probe와 고정 이미지 회귀는 유효한 후속 검증이지만 이번 문서 갱신에서 실행하지 않았다. point 수·분포가 다른 측정치를 viewport 성능으로 직접 환산할 수 없다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphIblBake.h`](../../Engine/RenderEngine/MaterialGraphIblBake.h)
- [`Engine/RenderEngine/MaterialGraphIblBake.cpp`](../../Engine/RenderEngine/MaterialGraphIblBake.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledIblLookup.slang)
