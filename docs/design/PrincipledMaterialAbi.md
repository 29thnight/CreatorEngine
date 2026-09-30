# Principled Material 공용 Slang ABI — MAT-1 / MAT-3 / MAT-4 / MAT-5

**2026-09-28 · 공용 평가 경계와 core/Layered/Special Principled 의미**

정본 코드는
[`PrincipledSurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSurface.slang)이다.
이 계약은 [`BlenderMaterialGraphPlan.md`](../plans/BlenderMaterialGraphPlan.md)의
`MAT-1`에서 시작했다. `MAT-3`의 값·Fresnel·IBL 계약과 검증은
[PrincipledCoreSemantics.md](PrincipledCoreSemantics.md)에 기록한다.
`MAT-4`의 coat/sheen/anisotropy/thin film 의미·비용·검증은
[PrincipledLayeredSemantics.md](PrincipledLayeredSemantics.md)가 소유한다.
`MAT-5`의 transmission/subsurface/별도 Volume closure와 Forward 자원 요구 조건은
[PrincipledSpecialSemantics.md](PrincipledSpecialSemantics.md)가 소유한다.

## 1. 평가와 소비

```text
core 입력 / 이후 generated graph 값
    → DefaultMaterialInputs → EvaluateMaterial → PrincipledSurface
    → BuildPbrSurface / EvaluatePrincipledIbl(PrincipledIblIntegral)

Layered 정적 mask의 generated 값
    → EvaluateMaterial → PrincipledSurface + prepared PrincipledLayeredIntegral
    → BuildPbrSurface / EvaluateLayeredIbl

Special 정적 mask의 generated 값 + 별도 VolumeInputs
    → EvaluateMaterial / EvaluateVolume
    → BuildSpecialForwardSurface(prepared integral)
    → 분리된 local/subsurface/transmission + prepared medium segment 합성

현행 glTF texture sample × property
    → MakeStandardMaterialInputs → EvaluateStandardMaterial → PrincipledSurface
        ├─ GBuffer 기존 MRT → UnpackStandardSurface → Deferred
        └─ Forward
    → BuildPbrSurface / EvaluateStandardIbl(float2 DFG)
```

- `MaterialInputs`는 재질의 값 입력이다. 현재 `MakeStandardMaterialInputs`가
  texture×factor와 emission strength를 현행 규칙대로 합성한다.
- `PrincipledSurface`는 평가 결과다. 직접광의 위치·시선·DFG·광원 목록은 포함하지 않는다.
  해당 조명 상태는 `PbrSurface`가 갖는다.
- GBuffer와 Forward가 같은 `EvaluateStandardMaterial` 호환 어댑터를 호출한다. Deferred는 기존 MRT의
  값을 `UnpackStandardSurface`로 복원하고 Forward와 같은 직접광·IBL 어댑터를 쓴다.
- 두 형식은 셰이더 내부의 논리 값이다. C++ cbuffer나 자산 파일의 byte layout으로
  직렬화하지 않는다. 현행 b2 숫자 offset, texture register와 MRT 형식은 유지한다.

## 2. 값의 의미

| 필드 | 현재 계약 |
|---|---|
| `baseColor` | scene-linear RGB. Alpha와 별도 |
| `alpha` | core는 saturate된 opacity. Standard는 기존 coverage 적용 결과. Transmission과 별도 |
| `normal` | world-space 단위 normal. core의 미연결·무효 값은 geometric normal을 사용 |
| `roughness`, `metallic` | core는 saturate. Standard는 원래 texture×factor를 MRT에서 보존하고 조명 경계에서 saturate |
| `emissionColor`, `emissionStrength` → `emission` | scene-linear HDR RGB와 scalar 입력을 곱한 평가 결과. 1.0 초과 값 보존 |
| `occlusion` | 독립 AO.R과 strength로 평가한 값. Deferred의 화면 AO는 복원 후 곱함 |
| `ior`, `specularIorLevel`, `specularTint` | core는 F0·유효 IOR·금속 F82 Tint로 평가. Standard는 기존 F0=0.04와 Schlick/DFG 유지 |
| `coatWeight/Roughness/Ior/Tint/Normal`, `sheenWeight/Roughness/Tint` | MAT-4의 레이어 입력·평가 값. 별도 Normal과 에너지 감쇠 |
| `anisotropy/Rotation`, `tangent`, `thinFilmThickness/Ior` | MAT-4의 방향 프레임과 nm 단위 film. RGB 파장 근사 |
| `transmissionWeight/Ior/F0/FilmIor` | MAT-5 glass 비중과 authored IOR·tint, front/back 상대 IOR |
| `subsurfaceWeight/Radius/Scale/Ior/Anisotropy` | MAT-5 RGB 확산 profile과 local/nonlocal 비중 |
| `VolumeInputs` → `PrincipledVolume` | 별도 Output closure. scattering/absorption/emission 계수. Surface의 density 필드로 직렬화하지 않음 |

`DefaultMaterialInputs`는 Blender core 기본값(base=0.8, roughness=0.5)을 사용한다.
`DefaultStandardMaterialInputs`는 현행 glTF white/roughness=1 호환 기본값이다.
이 구조를 Blender 전체 소켓 schema로 사용하지 않는다. 그 schema는 LX 대응표와 MAT-2가
소유하며, layered 의미는 MAT-4, special 공용 의미는 MAT-5에서 구현했다.

## 3. 정적 기능 마스크

`MaterialFeatureMask`는 `uint`이며 `LX_MATERIAL_FEATURE_MASK`의 숫자 literal에서
셰이더 컴파일 때 고정된다. `PrincipledSurface`에 runtime mask 필드를 넣지 않는다.

| bit | 기능 | 현재 평가 모듈 지원 |
|---:|---|---|
| 0 | Base Color | 지원 |
| 1 | Metallic/Roughness | 지원 |
| 2 | Normal | 지원 |
| 3 | Alpha | 지원 |
| 4 | Emission | 지원 |
| 5 | Occlusion | 지원 |
| 6 | Specular/IOR | MAT-3 지원, 고정 Standard pass binding은 MAT-7 후속 |
| 7~10 | Coat, Sheen, Anisotropy, Iridescence | MAT-4 지원. 제품 route/binding은 MAT-7 후속 |
| 11~13 | Transmission, Subsurface, Volume | MAT-5 공용 평가·Forward 요구 조건 지원. 제품 binding은 MAT-7 후속 |

공용 모듈의 알려진 마스크는 `0x3FFF`다. Layered·Transmission·Subsurface bit에는
core Specular/IOR bit가 필요하다. Volume-only는 그 의존성이 없다.
Special은 `LX_MATERIAL_ROUTE=2`와 별도 consumer를 요구한다.
현행 고정 Standard 경로는 `0x003F`를 선언한다.
`StandardMaterialRoute.slang`은 고정 pass에 bit 6~13을 요청하면 아직 없는
MRT/IBL lookup 또는 Special Forward binding을 지목해 거부한다.
Generic `PbrSurface`도 Special을 거부하고, 공용 모듈은 unknown bit와 잘못된
feature 의존성을 컴파일 오류로 거부한다. 지원하지 않는 lobe를 Standard 출력으로
조용히 바꾸지 않는다. Graph feature 추출, sample/lobe 제거와 자동 route 선택은
`MAT-6/MAT-7`의 작업이며 이 마스크 선언만으로 구현 완료로 세지 않는다.

## 4. 검증 결과

아래는 MAT-1의 최초 결과다. MAT-5의 현재 ABI 게이트는 기존 70개 엔트리와
unknown 4건, 미배선 core/Layered 고정 route 30건, Special 고정 route 24건,
총 **58건 거부**를 검사한다.
core DXIL/SPIR-V·실제 GPU 의미 검사는 [MAT-3 결과](PrincipledCoreSemantics.md#검증)를 따른다.
Layered DXIL/SPIR-V 14개·의존 mask 거부 8개와 독립 GPU/numeric golden은 [MAT-4 결과](PrincipledLayeredSemantics.md#검증)를 따른다.
Special DXIL/SPIR-V CS/PS 28개·route/의존 mask 거부 22개와 GPU/numeric golden은
[MAT-5 결과](PrincipledSpecialSemantics.md#검증)를 따른다.

### MAT-1 최초 검증 기록

- `Tools/regression/verify-material-abi.ps1`: VS18 MSVC로 독립 프로브를 빌드하고
  저장소의 **Slang 2026.14·고정 DXC**만 적재했다. 설치된 SDK 컴파일러를 대체 경로로 쓰지 않는다.
- DXIL·SPIR-V의 GBuffer, Deferred, Forward, Water/Wind, full/reduced,
  legacy/packed UV1·color·skinning과 reference 경로 **70개 엔트리 컴파일 통과**.
  Vulkan binding shift와 DX cbuffer layout, warnings-as-errors는 제품 설정을 따른다.
- MAT-1 당시 Coat `0x80`과 unknown `0x40000000`을 양 backend에서 넣은
  **4개 실패 사례가 지정한 미지원 마스크 진단으로 거부**됐다.
- 기존 Debug Editor host에 `--development-project`로 이 작업 공간을 지정해
  변경한 Slang을 제품의 컴파일러·PSO·GPU readback 경로에서 검증했다.
  C++ 엔진 전체를 새로 빌드한 결과로 보고하지 않는다.

| GPU 검사 | 결과 |
|---|---|
| `dx12.gbuffer` | Diffuse=(0.2,0.4,0.6), ORM=(1,0.75,0.25), Normal=(0.5,0.5,0), Emission=(0,0,0), Bitmask=43981 기대값 통과 |
| `dx12.forwardshade` | Water/Wind b2·texture reflection·variant·실패 거부 통과. Forward+/reference 16,384 픽셀 불일치 0 |
| `dx12.iblshade` | 환경 방향·매끈 금속·AO 소비 통과. 직접광 AO 변경 차이 0.0 |

이는 공용 ABI의 컴파일·현행 Standard 동작 검증이다. Blender golden과의 수치 일치,
실제 제품 씬의 Deferred/Forward 교차 허용 오차와 성능 상한은 `MAT-9`에서 판정한다.
