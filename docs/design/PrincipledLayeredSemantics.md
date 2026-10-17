# Layered Principled 의미 — MAT-4

**2026-09-28 · 공용 Slang lobe와 독립 GPU 검증 · 제품 route/binding은 MAT-7 후속**

## 기준

기본값은 Blender 5.1.1의 [고정 소켓 export](../../Tools/LatticeExample/fixtures/blender-5.1.1-shader-nodes.json)를 따른다.
레이어 순서·coat tint·emission 감쇠는 같은 버전의
[EEVEE Principled](https://github.com/blender/blender/blob/v5.1.1/source/blender/gpu/shaders/material/gpu_shader_material_principled.glsl),
anisotropic GGX와 tangent 회전은
[Cycles Principled](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/osl/shaders/node_principled_bsdf.osl)를 기준으로 삼았다.
Sheen은 [Cycles LTC closure](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_sheen.h)의
Zeltner/Burley/Chiang 모델과 같은 버전의 계수표를 사용한다.

**EEVEE 5.1.1은 Principled anisotropy와 thin film 입력을 평가하지 않는다.**
MAT-0의 EEVEE material grid는 두 입력의 효과를 입증하는 golden이 아니다.
이 두 기능의 재질 의미 비교에는 Cycles 기준이 필요하다. 이 항목을 EEVEE 전체 parity로 표시하지 않는다.

Thin film은 [Cycles optical 구성](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_util.h)을 참고한
가시광 Fourier Airy 간섭식이다. **512개 complex XYZ sensitivity를 linear Rec.709로 변환한
LUT와 3차까지의 간섭 항**을 사용한다. 금속 경계의 위상은 n+ik, 크기는 film-relative F0/F82다.
이전 650/550/450nm 모델은 historical numeric baseline에만 보존한다.
IOR·두께·각도·금속 광학 특성 복원의 근사 한계와 rendered parity 수용은 MAT-9에서 판정한다.

## 입력

모든 색은 scene-linear RGB, Normal/Tangent는 world-space, 두께는 nm, 회전은 turn이다.
미연결 Coat Normal/Tangent의 RNA 기본값은 영벡터다. shader 기본값과 무효 입력은 geometry 문맥으로 복구한다.
`DefaultMaterialInputs(geometricNormal, geometricTangent)`와 `EvaluateMaterial`이 이 문맥을 받는다.
geometry가 없는 독립 호출은 normal=+Z, tangent=+X다.

| 입력 | 기본값 | 평가 |
|---|---|---|
| Coat Weight / Roughness / IOR | 0 / 0.03 / 1.5 | weight≥0, roughness 0~1, IOR≥1 |
| Coat Tint / Normal | RGB 1 / geometry normal | 음수 색 제거, normal 단위화 및 geometry fallback |
| Sheen Weight / Roughness / Tint | 0 / 0.5 / RGB 1 | weight≥0, roughness 0.001~1, 음수 색 제거 |
| Anisotropic / Rotation / Tangent | 0 / 0 / geometry tangent | anisotropy 0~1, rotation의 소수부, normal에 투영 후 단위화 |
| Thin Film Thickness / IOR | 0nm / 1.33 | thickness≥0, IOR≥0.00001 |

Weight·tint의 연결 값은 1 초과 gain을 허용한다. 이 경우 bounded white furnace의 에너지 상한은 적용하지 않는다.
Coat tint=0 채널은 완전히 흡수한다. HDR tint의 pow exponent는 유한 float 범위 안의 ±80으로 제한한다.
Tangent=0/normal과 평행/NaN은 geometry tangent, 그것도 유효하지 않으면 normal에 수직인 축으로 복구한다.

## 공용 평가와 에너지

구현은 [PrincipledLayered.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang),
[PrincipledThinFilm.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang),
[PrincipledBrdf.slang](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledBrdf.slang)에 있다.

1. 가장 바깥의 Sheen, 다음 Coat, 마지막 base 반사·확산 순서다.
2. Sheen의 view albedo와 tint에서 최대 RGB 반사량을 구해 아래 레이어를 감쇠한다.
   Coat Normal이 활성화되면 sheen normal도 base/coat normal 사이에서 혼합한다.
3. Coat는 별도 Normal·Roughness·물리 dielectric Fresnel을 사용한다. Base의 Specular Level,
   F0 tint, metallic, thin film을 coat에 적용하지 않는다. IOR=1이면 coat 반사는 0이다.
4. Coat의 GGX layering albedo와 darkened closure weight로 하위 레이어를 감쇠하고, 굴절 경로 길이에 따른
   `coatTint^(1/cosTransmitted)`를 곱한다. Emission도 같은 하위 레이어 감쇠를 받는다.
5. Base·Coat GGX는 correlated Smith visibility를 사용한다. 금속·유전체·coat를 각각 기판 Fss와
   Blender 5.1.1 E/Eavg LUT로 보상한 뒤 혼합한다. 추가 Lambertian 보상 lobe는 없다.
   Base diffuse는 유전체 layering albedo의 나머지에 `(1-metallic)`을 한 번 곱한다.
   강한 비등방성의 LUT 근사는 prepared integral의 남은 에너지 budget으로 직접광·IBL을 함께 제한한다.
6. Anisotropy는 `aspect=sqrt(1-0.9*anisotropy)`, `ax=roughness²/aspect`,
   `ay=roughness²*aspect`다. alpha의 수치 하한은 0.001, 축별 상한은 1이다.
   직접광과 IBL bake가 같은 분포를 사용한다.
7. Thin film은 dielectric 및 metallic base 반사에 적용한다. 금속 n+ik는 F0/F82에서 추정한다.
   Thickness≤0.1nm 또는 film IOR=1은 기존 반사와 같다. 0.1~1nm에서는 film IOR만 smoothstep으로 연결한다.
   상부 경계의 전반사와 스침각 반사도 포함한다.
8. Alpha는 opacity로 남고 lobe나 emission에 미리 곱하지 않는다. AO는 IBL에만 적용한다.

MAT-3의 일반 core/glTF 함수와 legacy IBL 모델 0은 유지한다. Scene의 Principled 그래프는
Core 기능 분류에서도 위의 GGX base를 사용한다. 박막을 끈다고 계산 모델이 바뀌지 않는다.
이를 기존 2채널 DFG와 동일하다고 가정하지 않는다.

## IBL 계약과 후속 배선

`PrincipledLayeredIntegral`은 base와 coat의 보상된 반사 RGB, directional albedo, 평균 Fresnel을 갖는다.
필드명 `singleScatter`는 ABI를 유지하지만 모델 1에서는 이미 GGX 보상을 포함한다.
`viewTier.w=1`은 이 계산 모델을 뜻하며 Core/Layered 작가 기능 분류와 독립이다.
`IntegrateLayeredIbl`은 1,024 GGX 표본과 64 Fresnel 표본으로 이를 생성하는 **bake/probe 함수**다.
Roughness=0은 mirror 해석식을 사용한다. 제품 pixel shader에 이 표본 루프를 연결하지 않는다.

Layered의 `BuildPbrSurface`는 prepared integral을 다섯 번째 인수로 요구한다.
`EvaluateLayeredIbl`은 base irradiance, base prefiltered, coat prefiltered, sheen irradiance를 별도로 소비한다.
Emission은 출력에 따로 있고 `Sum()`은 조명 합만 반환한다. 서로 다른 lobe에 단일 base prefilter를 자동 재사용하지 않는다.
Anisotropic integral은 view azimuth/tangent 방향까지 필요하므로 기존 isotropic DFG 2D lookup으로 대체할 수 없다.
Integral/lookup generation은 해당 재질의 IOR·F0/F82·roughness·anisotropy·film과 시선 프레임에 맞아야 한다.
이를 변경하는 runtime instance override가 있으면 관련 generation을 갱신해야 하며, 이전 응답을 그대로 소비하지 않는다.

Lookup bake의 차원·cache·환경 convolution, graph→입력 생성은 MAT-6/MAT-7이다.
새 cbuffer/MRT/texture binding, Layered Scene/Game 노출이나 Deferred/Forward 교차 검증은 이번 슬라이스의 완료 범위가 아니다.

## 정적 기능과 비용

| mask | 기능 | 추가 평가 자원/계산 |
|---|---|---|
| `0x003F` | 기존 glTF Standard | 기존 Schlick/DFG, Layered include·state 없음 |
| `0x007F` | MAT-3 core | IOR-aware Fresnel |
| `0x00FF` | core+Coat | 별도 GGX·coat integral·tint 경로 |
| `0x017F` | core+Sheen | 32×32×float3 고정 LTC, 네 계수점 보간·분포 |
| `0x027F` | core+Anisotropy | tangent frame·두 alpha 축, view azimuth integral |
| `0x047F` | core+Thin Film | 12KiB spectral Fourier LUT·3차 Airy·F0/F82→n+ik |
| `0x01FF` / `0x07FF` | Coat+Sheen / 모든 Layered | 해당 기능의 정적 합성 |

Layered 범위는 `0x07FF`다. Layered bit에는 Specular/IOR bit가 필요하다.
MAT-5는 알려진 공용 범위를 `0x3FFF`로 확장했다. Special bit(11~13)는
[별도 Forward 계약](PrincipledSpecialSemantics.md)을 사용하며 generic PbrSurface와 고정 Standard pass에서는 거부한다.
Unknown bit와 고정 Standard pass의 비기본 IOR/Layered 요청도 진단과 함께 거부한다.
기능을 꺼야 할 때 runtime weight만 0으로 만드는 대신 MAT-6/MAT-7이 정적 mask를 제거한다.
따라서 기존 glTF에 LTC·thin film이나 lobe별 state 비용을 부과하지 않는다.

LTC 원시 계수는 **12KiB**, shader 상수로 고정한다. 계수의 Apache-2.0 고지와 재생성은
[고정 fixture](../../Tools/blender/fixtures/principled-layered-5.1.1/README.md)를 따른다.
`compile-cost.csv`는 7개 permutation의 DXIL/SPIR-V byte 수를 기록한다.
이 파일은 bake와 readback을 포함하는 compute probe의 크기이며 제품 GPU 시간이나 material shader 크기로 해석하지 않는다.
Cook variant 상한·resource 비용 badge와 GPU 시간 gate는 MAT-7~MAT-9의 책임이다.

## 검증

- `Tools/regression/verify-principled-layered.ps1`: VS18 MSVC `/W4 /WX`, 저장소 Slang 2026.14와 고정 DXC만 사용.
- Blender 소켓 기본값 13개, 고정 Sheen 계수 1,024개의 SHA-256을 검사한다.
- DXIL/SPIR-V 14개 컴파일, 잘못된 Layered 의존 mask 8개 거부. SPIR-V는 컴파일 검증이며 Vulkan GPU 실행은 아니다.
- RTX 4070 Ti D3D12의 35개 사례×8개 시선×7개 permutation을 CPU double 기준과 비교한다.
  Thin film CPU 기준은 complex 경계 진폭/위상과 double Fourier 합으로 shader의 실수 전개를 검산한다.
- 수치·독립성 검사 **216,776건**, 흰색 환경 energy bound **5,712건**을 통과했다.
  허용 정규화 오차는 0.00005, 최대 오차는 **0.0000011920929**다.
  스침각/정면 Fresnel, coat IOR=4에서 F0 아래로 내려가는 물리 반사도 포함한다.
- 기본값·normal/tangent fallback·회전 반복·IOR/두께 경계·금속·전체 lobe 조합·연결 gain·clamp,
  Alpha/AO 독립성·광원 선형성 및 흰색 환경의 bounded energy를 검사한다.
- GPU/CPU raw CSV, 최대 오차 위치, feature numeric golden과 compile byte 수를 남긴다.
  Numeric golden은 엔진 closure 기준이다. Blender rendered EXR을 대체하지 않는다.
- 고정 feature numeric golden **1,960행/7,840개 성분**도 통과했다.
- MAT-3 8,280개 검사, 기존 pass 70개 컴파일과 미지원/unknown/미배선 route 34개 거부,
  현행 Debug host의 `dx12.gbuffer`·`dx12.forwardshade`·`dx12.iblshade`를 다시 검사한다.
  세 명령 모두 통과했으며 Forward/reference 16,384픽셀의 불일치는 0이었다.

Blender/Cycles rendered golden 교차 판정, 제품 route parity·성능과 thin film 근사 수용은 MAT-9에 남는다.

### MAT-9 박막 모델 교체 — 2026-09-30

금속 비율은 diffuse weight에서 한 번 적용한다. 박막 diffuse layering은 dielectric view-angle
Fresnel로 감쇠하고, GGX multiple-scatter 보정의 Fss는 박막 아래 기판 응답을 사용한다.
Thickness=0, film IOR=1, dielectric과 film IOR가 같은 경우의 기존 불변성 검사는 유지한다.
정적 film mask가 없는 shader에는 LUT와 박막 계산이 포함되지 않는다.

고정 Blender 24장 대조의 박막 relative RMS는 평행광 18.348%→2.835%, 균일 흰색 환경
15.646%→1.352%다. Debug/Release 이미지 데이터가 일치한다. 전 재질 parity 통과로 표시하지 않는다.
고정 spectral numeric baseline은 `numeric-golden-spectral.csv`와 `spectral-manifest.json`이며,
박막 fixture 외에는 이전 `numeric-golden.csv`와의 일치를 추가로 요구한다.
별도 1nm 직접 적분은 LUT 보간 오차와 3차 절단 오차를 분리한다.
상세 측정과 한계는 [MAT9ThinFilmAndEnvironment](../analysis/MAT9ThinFilmAndEnvironment.md)에 기록한다.

대시보드 JavaScript 파싱과 MAT 10행/34일/완료 18일 집계는 통과했다.
전체 `verify-plan-dashboard.ps1`은 HEAD 기준과 동일하게 기존 미산정 `days: null` 처리 24건과
PHASE 4.6 표시 계산 1건을 보고한다. 이 검사기의 범위 밖 결함을 MAT-4 통과로 덮지 않는다.

### MAT-9 전환 경계와 확대 판정 — 2026-10-01

Cycles의 >0.1nm cutoff와 IOR만 완화하는 전환에 맞춰 최종 반사색의 이중 보간을 제거했다.
SceneHost identity는 3이다. 이전 spectral golden은 보존하고 두 0.1nm fixture의 CPU complex
기준 112행을 `numeric-golden-transition.csv` / `transition-manifest.json`에 추가했다.
0.1nm no-film 제어 224성분 일치를 요구한다. Layered 216,776개 검사·14개 DXIL/SPIR-V
컴파일·8개 거부와 non-film 역사적 수치를 유지했다. 고정 film/off 8쌍·두 조명의 rendered
16조건 중 내부 목표를 통과한 것은 4조건이며, 전체 rendered 수용은 미달이다.
[확대 판정과 공통 BRDF 후속](../analysis/MAT9ThinFilmAcceptance.md)을 따른다.

### MAT-9 공통 GGX 수정 이후의 현재 계약 — 2026-10-01

위 4/16은 수정 전 기록이다. 현재 SceneHost identity는 **4**, numeric baseline은 별도
`numeric-golden-ggx.csv` / `ggx-manifest.json` 버전 4다. 기판 Fss 보상은 금속·유전체를
각각 계산한 뒤 혼합한다. 박막-off Core Principled 그래프도 같은 계산 모델을 사용한다.
Raw compensated integral을 먼저 clamp하지 않고, prepared 에너지 budget을 최종 State에서
직접광·IBL에 함께 적용한다. 순백 금속 4종·32시선의 GPU 대조 **3,352개 검사**에서 실제
raw 에너지 초과 8시선을 포함해 이를 확인했다. 이전 광학/profile 기준 파일은 보존한다.
고정 박막 on/off 32조건이 기존 목표를 통과했다.
[현재 수용 범위와 한계](../analysis/MAT9GgxClosureComparison.md)를 따른다.
