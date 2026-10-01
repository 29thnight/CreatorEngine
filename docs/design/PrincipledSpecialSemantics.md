# Principled Special 재질 — MAT-5

**2026-09-28 · Transmission / Subsurface / Volume 공용 평가와 Forward 실행 계약**

MAT-5는 Special closure의 값·조명·합성과 실행 요구 조건을 구현한다.
제품의 graph codegen·resource binding·cook 자동 route는 MAT-6/MAT-7,
Blender rendered golden·화면 transport 품질·성능 수용은 MAT-9에서 판정한다.
이번 결과를 Special 재질의 Scene/Game 배선 완료로 세지 않는다.

## 입력과 기준

Surface 정본은 [PrincipledSurface.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang),
Special 조명은 [SpecialForwardSurface.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/SpecialForwardSurface.slang)이다.
Volume은 BSDF 필드가 아니라 별도 Material Output closure다.
[PrincipledVolume.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang)의
`VolumeInputs → EvaluateVolume → PrincipledVolume`을 사용한다. 논리 구조를 cbuffer/자산 byte layout으로 저장하지 않는다.

| 입력 | Blender 5.1.1 기본값 | 공용 평가 |
|---|---|---|
| Transmission Weight | 0 | saturate; Alpha와 독립 |
| Subsurface Weight | 0 | saturate; 금속·Transmission 이후 남은 확산 비중 |
| Subsurface Radius / Scale | (1, .2, .1) / .05 | 각 값 ≥0; Scene은 artist 반경을 `Radius×Scale/(4π)`로 환산 |
| Subsurface IOR / Anisotropy | 1.4 / 0 | IOR≥1, anisotropy∈[0, .99] |
| Volume Color / Density | (.5, .5, .5) / 1 | 각 값 ≥0; HDR Color 허용 |
| Volume Absorption Color / Anisotropy | (0, 0, 0) / 0 | Color≥0, anisotropy∈[-.999, .999] |
| Volume Emission Color / Strength | (1, 1, 1) / 0 | 각 값 ≥0; Density와 독립 |

Surface 6소켓은 기존 pinned schema, Volume 6소켓은 설치된 Blender 5.1.1
`b70da489d7f4`의 신규 [default export](../../Tools/blender/fixtures/principled-special-5.1.1/volume-defaults.json)로 고정했다.
이 subset에 blackbody·temperature·density/color attribute·heterogeneous sampling은 포함하지 않는다.
현재 MAT-2 초기 정의 레지스트리에는 Volume Principled 노드가 없다.
MAT-6의 지원 operator 확장과 socket별 진단이 필요하다. 지원하지 않는 Volume 입력을 무시하고 cook하지 않는다.

## Transmission

Blender의 pinned [Cycles Principled 구성](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/osl/shaders/node_principled_bsdf.osl)을
입력 의미 기준으로 사용한다. Glass는 isotropic GGX이며 authored IOR와 Specular Tint를 소비한다.
Base Specular IOR Level에 따른 유효 IOR를 glass에 적용하지 않는다. 반대 면에서 상대 IOR를 뒤집고,
glass thin film의 상대 IOR도 변환한다. 금속 비중을 먼저 빼고 `sqrt(saturate(Base Color))`로 tint한다.
Alpha는 coverage이며 transmission weight나 Fresnel을 바꾸지 않는다.

직접광은 [Walter 등, EGSR 2007](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf)의
GGX microfacet refraction half-vector/Jacobian을 독립 구현했다. `V + eta*L`에서 half-vector를 만들고
반대 반구의 light만 BTDF에 넣는다. 시선 면의 반사는 기존 correlated Smith GGX로 별도 평가한다.
Snell 방향과 전반사 검사를 포함한다. Mirror에서 전반사이면 transmission은 0이다.
거친 표면은 다른 microfacet 방향의 투과가 남을 수 있다.

게임용 환경 모델은 **단일 경계의 에너지 정규화 근사**다. Raw BTDF 적분과 glass의
single/multiple 반사량으로 남은 transmission budget을 구한다. 흰 환경에서 반사·투과·확산의
합이 증가하지 않게 정규화한다. 이 보정은 Cycles의 닫힌 solid 내부 경로 추적과 같지 않다.
두 경계 굴절·caustic·nested medium stack·scene color 접근은 제품 transport의 후속 범위다.
IOR=1은 straight-through delta를 environment에서 처리하고 유한 direct BTDF는 0으로 둔다.
Roughness=0도 environment의 mirror/delta와 direct alpha 하한 0.001을 구분한다.

## Subsurface

[PrincipledSubsurface.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang)은
[Jensen 등, SIGGRAPH 2001](https://graphics.stanford.edu/papers/bssrdf/bssrdf.pdf)의 diffusion dipole을
바탕으로 **RGB별 정규화된 확산 근사**를 독립 구현한다. Cycles Random Walk/Random Walk Skin 구현은 아니다.
공용 `BuildSubsurfaceProfile`은 Base Color를 albedo로, Radius×Scale을 물리 mean free path로 해석한다. Reduced scattering에
anisotropy를 적용하고 IOR의 diffuse Fresnel boundary로 real/virtual source depth를 만든다.
평면의 radial integral이 1이 되도록 정규화한 profile density와 해석 CDF를 제공한다.

**2026-10-01 Scene artist 반경 환산:** `LXSceneSubsurfacePS → BuildSceneSubsurfaceProfile`은
그래프의 authored Radius×Scale을 **`1/(4π)`**로 환산하여 위 물리 profile에 공급한다.
이는 [Cycles의 Random Walk/Burley radius 전처리](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bssrdf.h#L61)와
같은 길이 환산이다. 공용 물리 closure와 기존 독립 numeric golden의 입력 단위는 유지한다.
Random Walk의 albedo remapping·폐곡면 내부 transport까지 구현했다는 의미는 아니다.
SceneHost identity는 최종 10으로 올려 이전 Scene 셰이더/쿠킹 서명을 무효화한다.

각 RGB 채널의 Radius/Scale 또는 albedo가 0이면 해당 채널은 local delta다.
다른 채널의 확산을 함께 끄지 않는다. SSS Weight는 반사를 제외한 diffuse budget을 local과
nonlocal로 나눈다. Transmission=1이나 Metallic=1이면 SSS budget은 0이다.

`EvaluateSpecialLight`는 local radiance, `subsurfaceSource`, transmission을 분리해서 반환한다.
Source를 이웃의 profile·material ID·depth/normal 조건으로 재분배하는 pass는 MAT-7의 자원 계약이다.
`EvaluateSpecialIbl`은 prepared `subsurfaceIrradiance`를 받으며 local channel만 local irradiance를 사용한다.
여기서 irradiance는 기존 엔진의 Lambert diffuse convention에 맞게 `/PI`가 포함된 입력이다.
각 destination의 하위 coat/sheen 감쇠와 SSS budget은 공용 소비 함수가 한 번 적용한다.
준비된 irradiance를 만들 때 같은 destination budget을 다시 곱하면 안 된다.
Source는 입사광의 coat/sheen 감쇠와 cosine/PI를 포함하고, 재질의 SSS color/weight는 포함하지 않는다.
Profile에 따라 이웃 source를 적분한 뒤 destination color/weight를 소비 함수에서 한 번 적용한다.
Redistribution의 면적·normal convention은 같은 generation에서 고정한다.

SSS 입력을 전체 화면 RGB blur로 대체하지 않는다. 현행 `EnhancedSSSPass`는 material profile/ID를
소비하지 않는 전체 화면 효과이며 이번 closure에 연결하지 않았다. Local AO를 nonlocal SSS에 다시 곱하지 않는다.

## Volume

Coefficient mapping은 pinned [Blender EEVEE Volume Principled](https://github.com/blender/blender/blob/v5.1.1/source/blender/gpu/shaders/material/gpu_shader_material_volume_principled.glsl)의
기본 입력 의미를 따른다. Density>0.00001일 때:

```text
sigmaS = Density * max(Color, 0)
sigmaA = Density * max(1 - Color, 0) * max(1 - sqrt(max(Absorption Color, 0)), 0)
```

Emission Strength>0.00001이면 nonnegative Emission Color×Strength를 사용한다.
Density=0이어도 emission은 남는다. Extinction `sigmaS+sigmaA`는 world 길이의 역수,
emission은 길이당 radiance source다. Density의 단위 환산은 cook/asset 계약에서 같은 world scale을 사용해야 한다.

`VolumePhase`는 Henyey–Greenstein이며 적분이 1인 phase다. Cosine은 두 transport 방향 사이의 cos로
입력하며, 음수/양수 anisotropy의 peak 주변 denominator를 안정적인 식으로 계산한다.
`incidentSource`는 phase를 이미 적용한 angular illumination integral이다.
직접광 목록을 준비할 때 `VolumePhase(cosine,g)*incidentRadiance`를 합한다. 함수가 phase를 재적용하지 않는다.

Homogeneous segment의 Beer–Lambert `T=exp(-extinction*distance)`와 absorption/scattering/emission source를
해석 적분한다. Optical depth가 작을 때 polynomial로 cancellation을 피하고,
extinction=0이면 emission×distance가 된다. Negative distance는 0이다.
`ComposeVolumeSegments(first,second)`는 시선에서 가까운 segment부터 `L1+T1*L2`, `T1*T2`로 합성한다.
Single scattering이며 heterogeneous/multiple scattering integration은 아직 지원하지 않는다.
현행 전역 fog/froxel pass와 이 재질 closure는 별도 계약이다.

최종 합성은 다음 순서다:

```text
L = medium.radiance
    + medium.transmittance * (Alpha * surfaceRadiance + (1-Alpha) * background)
```

Volume은 Surface Alpha에 종속되지 않는다. Volume-only Material Output은 downstream에서
Surface 평가를 생략하고 Alpha=0으로 공급한다. `0x2000` mask만으로 미연결 Surface를 자동 검출하지 않는다.
이미 surface BSDF에 포함한 굴절 배경을 Alpha 배경으로 다시 합산하지 않도록 MAT-7 transport가 별도 입력을 준비해야 한다.

## Special Forward 실행 계약과 비용

[LXMaterialExecution.h](../../Lattice/Material/LXMaterialExecution.h)은 feature 집합에서
Forward와 refraction/subsurface/volume 요구 조건을 분석한다. Unknown bit·core IOR 의존성·Deferred 선택·
필수 자원 누락을 각각 진단한다. 현재 header는 requirement validation이며 cook/pass 자동 선택기는 MAT-7이다.

| feature | mask bit | 요구 자원 |
|---|---|---|
| Transmission | 11 / `0x0800` | prepared refraction radiance, isotropic glass prefilter/integral |
| Subsurface | 12 / `0x1000` | material profile/ID 기반 nonlocal irradiance |
| Volume | 13 / `0x2000` | geometry path length와 적분한 medium segment |

공용 알려진 mask는 `0x3FFF`다. Transmission/SSS와 Layered에는 core Specular/IOR bit 6이 필요하다.
Volume-only는 bit 6 없이 가능하다. Special bit가 있으면 `LX_MATERIAL_ROUTE=2`를 명시해야 하며,
누락/Deferred=1/다른 값은 shader compile에서 거부한다. Generic `PbrSurface`와 현행 고정 Standard pass는
Special을 소비하지 못하므로 거부한다. 함수 반환 RGB에 SSS·굴절을 조용히 섞어 없애지 않는다.

`IntegrateSpecialForward`는 **bake/probe 전용**이다. Metal·dielectric·glass integral과 상부 Layered integral,
raw refraction integral을 준비한다. Raw refraction은 1,024 GGX 표본이다.
`BuildSpecialForwardSurface`는 prepared integral을 요구하며 PS 소비에는 표본 루프가 없다.
IOR·Specular Tint/Level·metallic·roughness·anisotropy/tangent·film·front/back·view가 변경되면 관련 lookup generation을 갱신한다.
SSS profile 변경은 nonlocal 준비 결과, Volume coefficient/path 변경은 segment generation을 갱신한다.
View-dependent response를 constant 재질별 값 하나로 재사용하지 않는다.

`SpecialForwardEnvironment`는 base/glass/coat prefiltered, sheen irradiance, refracted radiance,
subsurface irradiance를 별도로 받는다. 기본 환경 한 개를 모든 convolution에 재사용하지 않는다.
AO는 local diffuse/specular IBL에만 적용한다. Emission은 coat/sheen 감쇠 후 별도 값이며 `Sum()`에 포함하지 않는다.

`compile-cost.csv`는 bake를 포함한 CS와 prepared 자원을 소비하는 PS의 DXIL/SPIR-V byte 수를 구분한다.
이 프로브의 크기는 제품 GPU 시간이나 cook variant 수의 상한 판정이 아니다.
Lookup cache·동적 instance invalidation·resource table·RT/SSS pass ordering은 MAT-6/MAT-7,
실제 frame 비용·화면 근사 품질은 MAT-9에서 닫는다. 기존 glTF mask `0x003F`에는 Special module/state를 연결하지 않는다.

## 검증

- `Tools/regression/verify-principled-special.ps1`: VS18 MSVC `/W4 /WX` probe,
  저장소 Slang 2026.14·고정 DXC로 DXIL/SPIR-V CS/PS **28개 컴파일**, 잘못된 route/의존성 **22개 거부**.
- RTX 4070 Ti D3D12 **35사례×6시선×7마스크**, independent CPU double 대조와 metamorphic 검사
  **202,049개**, white furnace bound **4,410개**. 최대 정규화 오차 **0.0000011920929**.
- Authored IOR/Specular Level 분리, front/back TIR·IOR=1, 금속/Transmission/SSS budget,
  RGB radius·Scale=0 local fallback, Alpha/AO 독립, coat/sheen/film 합성, volume cutoff/HDR/anisotropy peak,
  작은 optical depth·zero path·opaque medium·segment 분할 합성 및 source emission을 검사했다.
- Profile density의 독립 adaptive quadrature와 CDF 대조, 확산 total mass와 HG solid angle 정규화,
  host의 Deferred/필수 자원 누락 거부를 검사했다.
- Blender 12개 기본값, Volume exporter의 exact 재생성, 별도 numeric golden **4,410행/17,640성분** 통과.
  Golden은 CPU closure baseline이며 Blender rendered 이미지가 아니다.
- MAT-3 core **8,280개**, MAT-4 Layered **216,776개** 및 pinned golden 회귀 통과.
  기존 pass **70개 컴파일**, unknown/미배선 route **58개 거부**.
- 현행 Debug Editor host에서 변경 Slang으로 `dx12.gbuffer`, `dx12.forwardshade`, `dx12.iblshade` 통과.
  Forward/reference 16,384픽셀 불일치 0, Water/Wind·IBL/AO 회귀 통과. C++ Editor 전체 신규 빌드가 아니다.

Raw 결과는 `Build/Obj/PrincipledSpecialProbe/`, 기존 제품 회귀는
`Build/Obj/MaterialAbiProbe/mat5-current-host-results.jsonl`에 있다.

## MAT-6 / MAT-7 인계

### 2026-10-01 MAT-9 rendered 검증 경계

현재 제품 경로의 고정 homogeneous single-scattering Volume 8조건을 Blender 독립 seed와
비교하여 기존 target 전체 통과를 확인했다. 기준의 bounce/내부 감쇠와 비교 도구의 최종
Volume 합성을 맞췄으며, 이 묶음에서 제품 Special 구현을 변경하지 않았다.
지원 geometry 예산 안의 80-triangle static closed boundary 범위다.
투과·SSS 초기 rendered 차이와 Scene Blended queue 미지원은 남아 있으며 이 문서의 수치
closure 통과를 해당 품질 수용으로 해석하지 않는다.
자세한 조건·오차·남은 gate는 [MAT9SpecialTransportComparison](../analysis/MAT9SpecialTransportComparison.md)을 따른다.

후속 [Special Surface 수정](../analysis/MAT9SpecialProfileCorrection.md)은 Scene artist 반경 환산,
glass 준비/소비 budget 일치와 스침각 입력의 잘못된 거부를 수정했다. Scene 유효성 검사는
finite/tier/nonzero view를 유지하고, standalone directional bake는 기존 hemisphere 계약을 보존한다.
최종 Debug/Release18장 byte exact, Scale=0/off 전체 이미지 byte exact를 확인했다.
수렴한 독립 reference와의 target은 7/18로 Surface 전체 미수용이며 Volume8조건과 별도 범위다.

1. MAT-6에서 초기 지원 node/socket의 deterministic Slang·feature extraction·source 진단을 연결했다.
   [MaterialSlangCodegen.md](MaterialSlangCodegen.md)를 따른다. full Blender Volume의 attribute/blackbody는
   미등록 node로 보존·거부하고, 엔진의 `LXPrincipledVolume` 6입력 subset만 생성한다.
2. Prepared integral/environment/profile/medium generation의 reflection resource table과 lifetime을 구현한다.
3. Requirement 분석 결과로 Special Forward를 자동 선택하고, 자원 준비 실패 시 마지막 정상 material generation을 유지한다.
4. Scene/Game의 refraction·material별 SSS·Volume geometry path를 연결하고, preview도 같은 generation을 소비한다.
5. MAT-9에서 Blender rendered grid와 근사 수용·제품 transport·성능을 확인한다.
