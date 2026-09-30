# Material Graph Scene의 이미지별 UV·LOD

## 샘플링 계약

`LXMaterialCompiler`는 Image Texture와 `LXTextureSample`을
`LXSampleMaterialImage(image, sampler, vector.xy, context.lod)`로 생성한다.
Color/Alpha는 같은 노드의 한 샘플을 공유한다. 같은 이미지 자원은 하나의 슬롯을
공유하지만 Vector·sampler가 다른 노드는 각각 샘플한다.

실제 Scene host는 generated module 앞에서 `LX_MATERIAL_PIXEL_FOOTPRINT=1`을 정의한다.
`MaterialGraphTextureSample.slang`은 각 샘플의 최종 Vector 식에 `ddx_fine`/`ddy_fine`을
적용하고, 그 샘플에 바인딩된 이미지의 폭·높이·mip 수로 LOD를 계산한다.

```text
footprint = max(length(ddx_fine(uv) * imageDimensions),
                length(ddy_fine(uv) * imageDimensions))
lod = clamp(log2(max(footprint, 1e-20)), 0, imageMipCount - 1)
```

따라서 서로 다른 해상도·종횡비·mip 수는 각각의 LOD를 사용한다. 상수 Vector의
footprint는 0이므로 첫 mip을 사용한다. 같은 이미지의 다른 Vector 식도 별도로 계산한다.
Scene host의 한 텍스처 제한과 첫 텍스처에서 구한 공통 LOD를 제거했다.

평가는 기존 coverage/draw-owner의 divergent 분기 전에 실행한다. Scene의 GBuffer,
lookup 입력 수집 2개 PS와 Color PS가 같은 규칙을 사용한다. 실제 SRGB view는 보간 전에
RGB를 선형화하며 alpha와 Data 이미지는 선형 값이다. filter/address는 각 sampler를 따른다.

## 다른 host와 버전

compute·evaluated-point host는 기본 플래그 0으로 `context.lod`를 그대로 사용한다.
픽셀 quad가 없는 이 경로에는 Scene fine derivative를 삽입하지 않는다.
현재 지원은 mesh의 기본 UV와 지원된 explicit Vector 식이다. named UV set·UV1 선택,
Mapping/Texture Coordinate를 포함한 Blender 전체 노드 목록은 별도 지원 작업이다.

생성기 `CompilerVersion`은 2, semantic key는 `lx.material.compiler/2;common-abi/MAT-5`다.
공용 include 의존성과 생성 코드가 프로그램 identity에 포함된다. LXMC reader는 현재
compiler version과 다른 쿠킹 결과를 거부하므로 v1 프로그램은 다시 생성·검증·쿠킹해야 한다.
`.shadergraph`의 저장 schema와 `LXMaterialContext` ABI는 바꾸지 않았다.

## 독립 reference와 검증 범위

native Scene fixture는 두 제품(Core/Layered)에 이미지 2개·sampler 3개·샘플 3개를 사용한다.

| 샘플 | 이미지 | Vector / sampler | 소비 |
| --- | --- | --- | --- |
| A | SRGB 32×16, 6 mip | mesh UV / Linear Repeat | Base Color |
| B | Data 8×64, 7 mip | mesh UV / Linear Clamp | Roughness alpha |
| C | A와 같은 자원 | 상수 parameter / Closest Clamp | Metallic alpha |

CPU double reference는 mip별 RGBA8 패턴, texel 중심·wrap/clamp, SRGB decode,
bilinear/trilinear interpolation과 analytic UV gradient로 각 샘플을 계산한다.
8개 프레임에서 다음을 검사한다.

- 순차·1워커·4워커의 cold/warm cache와 공유 depth/HDR 합성.
- 서로 다른 fractional LOD, 같은 이미지의 독립 Vector/sampler와 첫 mip.
- Core Vector parameter만 변경할 때 Layered의 정확한 재사용.
- Core 이미지 B를 16×8·5 mip으로 교체할 때 바뀐 해상도·mip 수의 반영.
- UV를 128배 확대할 때 각 이미지의 서로 다른 마지막 mip 제한.
- 변경 없는 프레임의 전 픽셀 재사용, parameter/image 교체의 부분 갱신.

선형 필터의 SRGB RGB와 Data alpha 입력 transport에는 절대 오차 0.001을 적용한다.
그 외 입력은 기존 정규화 0.0001이다. nearest alpha는 0.0001 기준을 유지한다.
Direct3D의 필터 좌표·LOD에는 제한된 분수 정밀도가 허용된다.
[Microsoft Direct3D functional specification §7.18.16](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
이 fixture의 관측 차이는 해당 필터 정밀도 범위와 일치하지만 GPU 내부의 특정
양자화 방식을 증명한 것은 아니다.

transport를 통과한 실제 입력으로 독립 CPU double IBL/직접광을 계산한다.
IBL 36성분의 정규화 0.0001과 최종 HDR half의 `0.001 * max(1, abs(expected))`
기준은 유지한다. 공통 LOD를 잘못 적용하면 mip 패턴에서 이 transport 상한보다
큰 차이가 나므로 샘플별 LOD 오류를 구분할 수 있다.

## 동결 소스의 결과 (2026-09-29)

같은 소스의 Debug/Release 빌드와 RTX 4070 Ti native D3D12 GPU validation을 통과했다.
생성기 golden 동결 대조는 별도 native host에서 20 graph·13,306개 검사,
DXIL/SPIR-V 93개와 GPU 3,600성분을 통과했다.

| 검증 | 각 구성의 결과 |
| --- | --- |
| 전체 Scene 회귀 | 20,892,205개 검사 · GPU 3,735,460성분 |
| 실제 합성 | 44 graph · 가시 43,644픽셀 |
| 준비/선언 실패 복구 | 240개 거부 · accepted frame 보존 |
| 신규 이미지별 샘플링 | 8프레임 · 가시 1,352픽셀 |
| fractional / 마지막 mip | 1,014 / 338픽셀 |
| lookup 작업 합계 | 새 적분 21,021픽셀 · 정확한 재사용 22,623픽셀 |
| 최대 필터 transport 차이 | 절대 0.000956118 (상한 0.001) |
| 정규화 0.0001 게이트 | 최대 0.0000859499 (입력 transport·독립 물리 계산 포함) |
| D3D12 GPU validation | WARNING 이상 0건 |

Core/Layered 기본 제품과 footprint 제품의 Scene VS/GBuffer PS/lookup PS 2종/Color PS는
DXIL·SPIR-V 40개, lookup bake/clear CS는 4개 artifact다. 기존 probe signature의
`compiled=24`와 별도이며 SPIR-V 컴파일 성공을 native Vulkan 실행으로 세지 않는다.

기존 1920×1080 희소 coverage cold/warm GPU timestamp는 Debug 53.7335/10.4984 ms,
Release 64.2373/7.05165 ms였다. GPU validation이 켜진 단일 측정이고 dense Scene,
카메라 이동 또는 제품 성능 수용 기준이 아니다. 시간값은 세션별로 변할 수 있다.

증거는 `Build/Obj/MaterialProductProbe/texture-footprint-{Debug,Release}.log`,
`texture-footprint-build-{Debug,Release}.log`, `texture-footprint-codegen-frozen.log`다.
`texture-footprint-source-hashes.json`의 101개 소스 SHA-256을 두 구성 이후 다시 확인했다.
확장 signature·입력 오차 상한·소스 동결의 최종 판정은 `texture-footprint-gate-final.log`다.
반복 실행은 `Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`다.

## 제품 완료와의 경계

이 작업은 실제 Scene host의 샘플링 계약이다. native Vulkan 실행·실제 Editor Live Tick,
named UV set과 Blender 전체 노드 지원, Special/투명 합성, 비동기 generation/PSO 교체,
자동 Scene host 쿠킹과 일반 Scene의 시간/메모리 수용은 별도다. MAT-7은 진행 중이다.
