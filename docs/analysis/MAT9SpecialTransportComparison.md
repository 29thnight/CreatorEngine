# MAT-9 Special transport 대조 — 2026-10-01

## 판정

**균질 단일 산란 Volume의 고정 8조건은 통과했다.** 투과·SSS를 포함한 Special 전체는 미수용이다.
MAT-9는 `progress`, PHASE 4.25 완료 공수는 **32/34일**로 유지한다.
이번 작업은 기준 장면과 비교 도구를 수정하고 현재 제품 경로를 측정했다.
제품 Engine/Slang의 투과·SSS·Volume 구현은 이번 묶음에서 변경하지 않았다.

| Volume 판정 항목 | 기존 상한 | 8조건 측정 최대 |
|---|---:|---:|
| 상대 RMS | 1% | 0.100973% |
| p95 normalized | 1% | 0.134198% |
| max normalized | 5% | 0.193067% |
| 독립 시드 상대 RMS | 0.25% | 0.054315% |

[Volume 판정](MAT9SpecialVolumeTargets.json), [전체 측정](MAT9SpecialVolumeComparison.json),
[기준 manifest](MAT9SpecialVolumeReference.json), [이미지 대조](MAT9SpecialVolumeComparison.png).
그림은 각 행 **Blender / Native / 절대 차이 ×10**이며 표시용이다. 판정은 pre-tone linear RGB 픽셀이다.

## 고정 조건과 제품 실행

- Blender **5.1.1** (`b70da489d7f4`), Cycles CPU, 64×64, denoising/adaptive sampling off,
  Gaussian filter width 0.01, Raw/exposure 0/gamma 1.
- Volume 최종 기준은 **131072 samples, seed 0/11**의 별도 렌더 두 번이다.
- 같은 삼각형·normal·UV tangent를 공유하는 반지름 0.5의 **smooth 80-triangle closed icosphere**,
  카메라 `(0,0,3)`, FOV π/4, near/far 0.1/10.
- geometry SHA-256: `c14af82eea46c529393845d9c31724929931ea3e527f8bf5c488a4aa4df30282`.
- 방향광은 `normalize(0.35,-0.2,0.8)`, irradiance RGB 1, angle 0.
  Furnace는 균일한 흰색 환경 radiance RGB 1이다. HDRI/area-light 조건은 아니다.
- 입력 `.inputs`를 Native의 LX graph 생성기가 그대로 소비한다. Volume-only는 Surface를 연결하지 않고
  `LXPrincipledVolume`을 Material Output의 Volume에 연결한다.
- 제품 `MaterialGraphSceneHost`의 준비·색·Volume 선언과 실제 DX12 readback을 사용한다.
  이번에 비교 도구의 누락된 최종 `DeclareVolume` 합성을 연결했다.
- Release Native **8 frame, 131590 checks, GPU validation 0**, 정상 종료.
  VS18/v145 Release 비교 도구 빌드 통과. 제품 compiler의 DXIL/SPIR-V 준비를 거치며,
  이 묶음의 실제 GPU 실행은 DX12다. 전체 Editor 신규 빌드나 GUI 조작 검증을 추가한 것은 아니다.
- 공통 emission-control 내부를 2픽셀 침식한 **303픽셀**을 모든 조건에 동일하게 적용했다.
  case별 mask 선택·노이즈 제거·target 확대는 하지 않았다.
  normal/view 진단의 max absolute error는 각각 0.001641/0.001871로 기존 0.002 상한 이내다.

원래 Core/Layered의 2208-triangle sphere는 보존했다. 현재 Scene Volume은 static closed boundary와
총 **16 objects / 128 triangles**의 교차 예산을 요구하므로 Special에는 명시한 별도 geometry를 쓴다.
이번 결과를 큰 모델·skinned/heterogeneous media·multiple scattering의 검증으로 확대하지 않는다.

## Volume 기준 장면의 수정

초기 Special reference v1은 `volume_bounces=1`, sun shadows off였다.
엔진의 homogeneous single scattering과 이 조건은 같은 transport가 아니었다.
Blender는 RNA bounce 값을 kernel 제한에 **1을 더해** 전달하므로, 직접 단일 산란을 비교할 때는
RNA `volume_bounces=0`이 필요하다.
[Blender 5.1.1 integrator](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/scene/integrator.cpp#L173).
방향광의 매질 내부 경로에도 Beer 감쇠가 적용되도록 Volume-only 조건의 `sun.use_shadow=true`를 설정했다.
외부 occluder는 없고, 비볼륨 조건의 기존 shadow off는 유지한다.

| 산란 진단 조건 | 방향광 RMS | Furnace RMS | 사용 범위 |
|---|---:|---:|---|
| v1: RNA bounce 1, 방향광 내부 감쇠 불일치 | 9.5112% | 3.2772% | 역사적 진단, 수용 제외 |
| RNA bounce 1, 방향광 내부 감쇠 적용 | 3.2136% | — | 단일 조건 진단, 수용 제외 |
| **v2: RNA bounce 0, 내부 감쇠 적용** | **0.0515%** | **0.0880%** | 독립 seed 0/11 포함 최종 판정 |

v2는 reference recipe version과 transport 설명을 manifest에 고정했다.
판정 도구는 v1·diagnostic subset·flat geometry·bounce/shadow diagnostic을 전체 수용에 사용하지 않는다.
v1의 큰 Volume 차이를 제품 오차로 단정하지 않는다.

### 독립 수치 적분

같은 삼각형의 ray entry/exit와 isotropic 계수로 직접 계산했다.
`Color=0.5`, `Density=0.18`, `Anisotropy=0`이며 σs=0.09, σt=0.18이다.
계수 관계는 [Blender Principled Volume](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/osl/shaders/node_principled_volume.osl#L35)과 같다.
Gauss-Legendre 96/192점 적분에 카메라·광원 방향의 Beer 감쇠와 isotropic phase `1/(4π)`를 포함했다.
엔진의 CPU closure reference를 가져오지 않는다.

| 독립 기준과의 방향광 RMS | 측정 |
|---|---:|
| 96점 ↔ 192점 적분 수렴 | 0.00004051% |
| Native ↔ 192점 적분 | 0.051739% |
| Cycles v2 ↔ 192점 적분 | 0.005175% |

[독립 적분 결과](MAT9SpecialVolumeIntegral.json).
이 검사는 고정 방향광 단일 산란에 한정되며 다중 산란의 기준값이 아니다.

### 최종 Volume 8조건

| 조건 | 엔진↔Blender RMS | Blender seed RMS |
|---|---:|---:|
| 방향광 / emission | 0.0955% | 0.000041% |
| 방향광 / absorption + emission | 0.1010% | 0.000038% |
| 방향광 / isotropic scattering | 0.0515% | 0.000371% |
| 방향광 / surface emission 제어 | 0.0000% | 0.000000% |
| Furnace / emission | 0.0955% | 0.000041% |
| Furnace / absorption + emission | 0.1010% | 0.000038% |
| Furnace / isotropic scattering | 0.0880% | 0.054315% |
| Furnace / surface emission 제어 | 0.0000% | 0.000000% |

## 투과·SSS 초기 진단과 남은 차이

초기 v1 **24조건**은 16384 samples, seed 0/11로 렌더했다.
Release Native **394222 checks, GPU validation 0**, 정상 종료.
공통 numeric/noise target을 만족한 행은 **11/24**이지만 v1 recipe 자체가 수용 대상이 아니므로
Special 전체 또는 그 부분 집합을 완료로 세지 않는다.
[초기 측정](MAT9SpecialBaselineComparison.json), [초기 판정](MAT9SpecialBaselineTargets.json),
[초기 대조 그림](MAT9SpecialBaselineComparison.png).

| 초기 smooth 조건 | 엔진↔Blender RMS | 독립 seed RMS |
|---|---:|---:|
| 방향광 / tinted transmission | 33.9166% | 0.4583% |
| 방향광 / pure glass | 34.7308% | 0.2376% |
| Furnace / tinted transmission | 15.1081% | 0.2210% |
| 방향광 / SSS | 6.2429% | 0.6748% |
| Furnace / SSS | 4.2344% | 0.6780% |
| 방향광 / metal + SSS + coat | 2.0129% | 0.3863% |
| Furnace / metal + SSS + coat | 3.2339% | 0.4751% |

일부 reference noise도 기존 0.25% 상한을 넘으므로 최종 판정에는 추가 수렴이 필요하다.
특히 투과의 관측 차이는 측정된 seed 변동보다 크지만, 오차 전체의 원인을 하나로 확정하지 않는다.
현재 제품은 single-interface screen/environment refraction과 screen-space SSS diffusion이며,
Cycles의 closed-solid refraction·random-walk SSS와 transport가 다르다.
폐곡면의 출입 경계·geometric/shading normal·tint 경로와 SSS 공간 응답을 분리해서 조사해야 한다.

Native의 **SSS Scale=0은 SSS off와 방향광/Furnace 모두 bit exact**였고,
IOR=1의 투과 조건과 surface emission 제어도 차이 0이었다.
이는 해당 fallback의 검사이며 비영 SSS/투과의 품질 수용이 아니다.

### smooth/flat 분리 진단

같은 80-triangle 위치를 유지하고 normal만 flat으로 바꾼 Furnace 네 조건을 추가 측정했다.
Native **65907 checks, validation 0**, 정상 종료. 독립 repeat가 없는 진단이다.

| 조건 | smooth RMS | flat RMS |
|---|---:|---:|
| 흰색 diffuse 제어 | 0.9963% | 0.000351% |
| SSS off | 0.9478% | 0.042376% |
| pure glass | 0.6856% | 3.261172% |

흰색 제어의 smooth p95는 1.9592%였다. Flat에서 diffuse 차이가 줄어든 것은
저다각형 smooth normal과 기하학적 hemisphere 처리의 기여를 조사할 근거다.
Flat glass는 p95 4.4910%·max 27.6505%로 여전히 미달하며 RMS도 커졌다.
Flat 결과로 smooth 검증을 대체하거나 모든 Special 차이를 normal 탓으로 돌리지 않는다.
[분리 진단 측정](MAT9SpecialFlatDiagnostic.json).

## Alpha + transmission의 구현 경계

`SceneHost::Prepare`는 Blended queue에 `LX Scene Blended composition is not installed.`를 반환한다.
비교 도구 첫 frame에서도 실제 Blended 분류를 만들어 거부되는 것을 검사한 뒤 정상 opaque capture를 진행한다.
Alpha를 낮추고 opaque로 렌더하여 지원 완료로 세지 않는다.
기존 계획의 **alpha와 transmission 조합** 완료 기준은 남아 있으며 새 공수/완료 행을 만들지 않는다.

## 재현과 저장 증거

새 reference 출력 디렉터리와 새 Native label을 사용해야 한다. 기존 결과를 덮어쓰지 않는다.

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' -b --threads 3 `
  --python Tools/blender/material_matched_reference.py -- `
  --suite special-volume --samples 131072 --seed 0 --output Build/Obj/Mat9SpecialVolumeReference-0
# 같은 명령으로 seed 11 및 별도 출력 디렉터리를 생성한다.
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release `
  -Label special-volume-final -Reference Build/Obj/Mat9SpecialVolumeReference-0 `
  -ReferenceRepeat Build/Obj/Mat9SpecialVolumeReference-11
& 'C:/Python313/python.exe' Tools/regression/assess-special-material-images.py `
  Build/Obj/Mat9Images-Release-special-volume-final/comparison.json `
  Build/Obj/Mat9Images-Release-special-volume-final/targets.json
```

독립 적분은 NumPy가 있는 Python으로 `measure-special-volume-integral.py reference native output.json`을 실행한다.
RGBA32F/EXR·`.inputs`·삼각형·원본 실행 로그는 위 `Build/Obj` 디렉터리에 보존했다.
보고서 JSON은 reference/cook/geometry/pixel identity와 실제 readback 측정의 고정 사본이다.
작업 시 HEAD는 `12f970c7ed3a4408d268479f5d5acb5f80372b8c`이고 이전 MAT-9 변경이 포함된 dirty tree였다.
이번 묶음에서 commit/push하지 않았다.

기존 Core/Layered 24조건의 reference identity와 HDRI 26/26 판정을 변경된 공통 reader로 재검사했다.
새 reference recipe/geometry enum의 unknown 값은 거부한다. 최종 Volume 픽셀 재판정 8/8,
independent repeat가 없는 진단 fixture 거부, Python 5개 구문 검사, 대시보드 웹 빌드·inline script
구문/상태 검사와 범위 diff 검사를 통과했다. 성능 측정의 추가 gate는 이번 결과에 포함하지 않는다.

## 다음 기존 작업 순서

후속 [Special Surface 반경·스침각 수정](MAT9SpecialProfileCorrection.md)은 새 131072-sample seed0/11
기준의 18조건을 판정했다. Scene artist 반경 환산과 가장자리 준비 오류를 수정했으며 target은 7/18,
전체 미수용이다. 아래 기존 순서를 유지한다. 앞의 Scale=0/off bit-exact 관측은 공통 내부 영역에
한정되며, 후속 최종 제품에서 전체 이미지 byte exact를 별도로 검증했다.

1. **투과 출입 경계·SSS 공간 응답의 품질 개선**과 reference 수렴, 동일 fixed target 재검증.
   Alpha + transmission은 실제 Blended 합성 경로까지 확인해야 한다.
2. Texture-only/factor×texture/normal-map reference와 같은 지원 feature의 Deferred/Forward 교차 비교.
3. 추가 IBL 샘플·Special 준비를 포함한 실제 모델의 이동 카메라/tier/cold-warm 성능 수용.
4. 원래 area-light grid의 light ABI/consumer 및 rendered gate.

고정 Volume 8조건 통과와 이전 상수 HDRI 26조건 통과는 각각의 범위에 한정한다.
특정 조건의 성공으로 전체 MAT-9나 PHASE 4.25를 닫지 않는다.
