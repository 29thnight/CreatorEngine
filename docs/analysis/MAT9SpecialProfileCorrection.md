# MAT-9 Special Surface 반경·스침각 수정 — 2026-10-01

## 판정

**Scene SSS artist 반경 환산과 Special 준비 단계의 스침각 거부를 수정했다.**
수렴시킨 Blender 기준과의 Surface 18조건은 기존 target **7/18**, 전체 미수용이다.
MAT-9 `progress`, PHASE 4.25 완료 공수 **32/34일**을 유지한다.
투과 폐곡면 출구 경계와 SSS 공간 응답 개선이 남아 있다.

이전 [Volume 8조건 검증](MAT9SpecialTransportComparison.md)은 보존한다.
Surface 결과와 다른 세대의 Volume 결과를 합산하여 Special 전체 통과로 세지 않는다.

[최종 판정](MAT9SpecialSurfaceTargets.json), [전체 이미지 측정](MAT9SpecialSurfaceComparison.json),
[기준 seed 0](MAT9SpecialSurfaceReference.json), [기준 seed 11](MAT9SpecialSurfaceReferenceRepeat.json),
[대조 그림](MAT9SpecialSurfaceComparison.png), [같은 기준의 수정 전후 재산출](MAT9SpecialProfileAb.json).
최종 [실행·소스·바이너리 검증](MAT9SpecialSurfaceVerification.json)과
[Debug 판정](MAT9SpecialSurfaceDebugTargets.json)을 함께 보존한다.
그림은 각 행 Blender / Native / 절대 차이 ×10이며, 판정은 pre-tone linear RGB다.

## 제품 수정

### 1. Scene SSS 입력의 길이 환산

`LXSceneSubsurfacePS → BuildSceneSubsurfaceProfile → BuildSubsurfaceProfile`로
그래프의 artist Radius×Scale을 **`1/(4π)`**로 환산한 뒤 물리 profile에 공급한다.
[Cycles Random Walk/Burley radius 전처리](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bssrdf.h#L61)와
같은 길이 환산이다. 이전 Scene은 artist 반경을 물리 mean free path에 직접 넣었다.

공용 `BuildSubsurfaceProfile`의 물리 입력 계약과 기존 numeric golden은 유지했다.
이를 Cycles Random Walk의 albedo remapping·내부 경로 추적과 동일한 구현으로 해석하지 않는다.
공간 재분배는 여전히 정규화한 visible-surface dipole이다.

### 2. Glass 준비 적분과 공용 소비의 일치

`MaterialGraphSceneRefractionBake`의 glass 반사 적분은
`IntegrateSpecialForward`와 같이 **GGX 곱셈 보상을 적용하지 않는 별도 glass budget**을 사용한다.
이전 준비 단계는 공용 소비와 다르게 기본 보상 옵션을 사용했다.
반사·투과의 single-interface 에너지 모델을 일치시킨 수정이며,
폐곡면의 두 경계를 추적하거나 큰 투과 이미지 차이를 해소한 것은 아니다.

### 3. 스침각을 자원 준비 오류로 취급하던 문제

전체 이미지에서 Scale=0/off를 추가 대조한 결과, sun의 두 픽셀이 SSS 경로에서
자홍색 `(1,0,1)` 오류 표시를 냈다. 기존 공통 내부 303픽셀에서는 검출되지 않았다.
`MaterialGraphSceneSubsurfaceBake`와 `MaterialGraphSceneRefractionBake`가
standalone directional bake의 `N·V≥1e-4` 조건을 Scene raster 입력에도 적용한 것이 원인이다.
smooth normal이 스침각/반대 방향을 향하는 raster 픽셀도 유효한 입력이다.

Scene용 유효성 검사는 finite·값 범위·tier·nonzero view 조건을 유지한다.
해당 픽셀의 반사 적분은 필요한 경우 0을 반환하며, 준비 완료 marker를 기록한다.
standalone 요청은 기존 front-hemisphere 계약을 그대로 사용한다.
최종 **SceneHost identity 10**으로 이전 셰이더/쿠킹 서명을 거부하도록 갱신했다.

Scale=0/off 검사는 기존 상한 0.002를 유지하면서 **전체 64×64 RGB·alpha**로 확대했다.
이미지 오차 판정에 사용하는 공통 mask와 RMS/p95/max/noise 상한은 바꾸지 않았다.

| Scale=0/off 검사 | 수정 전 host 9 전체 RGB max abs | 최종 host 10 |
|---|---:|---:|
| 방향광 | 1.000000 | **0; 전체 파일 byte exact** |
| 균일 환경광 | 0.829590 | **0; 전체 파일 byte exact** |

[수정 전 전체 이미지 거부 판정](MAT9SpecialSurfacePreGrazingTargets.json)을 보존했다.
이전 보고서의 Scale=0/off bit-exact 주장은 공통 내부 영역에 한정된다.
최종 결과에서 전체 파일 일치까지 별도로 확인했다.

### 4. 독립 Scene 회귀 기준의 갱신

Scene SSS의 CPU profile 기준에도 artist→물리 길이 환산을 적용했다.
최종 색상 기준에는 이전 GGX 수정 전의 Lambertian multiple-scattering lobe가 남아 있어
현재의 독립 double-precision energy LUT·곱셈 보상·반사 budget 계산으로 갱신했다.
Glass CPU 준비 적분도 같은 별도 budget을 사용한다. 허용 오차를 확대하지 않았다.

## 수렴한 기준과 동일 조건의 수정 전후

- Blender **5.1.1** (`b70da489d7f4`), Cycles CPU, **131072 samples, seed 0/11**.
- 기존의 fixed 7 Surface 재질 + emission/white 제어, 방향광/furnace **18조건**.
  새 재질·임의의 유리한 조건을 추가하지 않았다.
- 공유 smooth **80-triangle closed icosphere**, radius 0.5, 64×64.
  geometry SHA-256 `c14af82eea46c529393845d9c31724929931ea3e527f8bf5c488a4aa4df30282`.
- 카메라 `(0,0,3)`, FOV π/4, near/far 0.1/10, Gaussian filter width 0.01.
  방향광 `normalize(0.35,-0.2,0.8)`, irradiance RGB 1; furnace radiance RGB 1.
- reference recipe v2, RNA volume_bounces=0. Surface는 Volume을 연결하지 않는다.
  기본 Principled subsurface method는 `RANDOM_WALK`다.
- 같은 emission-control 내부를 2픽셀 침식한 **303픽셀**에 공통 target을 적용한다.
  normal/view 진단 max abs 0.001641/0.001871은 기존 0.002 상한 이내다.
- 독립 시드 오차 **최대 0.227574%**, 모든 18조건이 기존 **0.25%** 상한 이내다.

아래 두 Native 결과를 **동일한 새 131072-sample 기준**으로 재산출했다.
수정 전 Native는 기존 v1 manifest를 가진 역사적 캡처이므로 입력·geometry·camera/light identity를
대조한 A/B 진단에만 사용한다. 최종 수용 판정에는 현재 제품의 v2 입력 캡처를 사용한다.
이전 보고서의 16384-sample 수치와 직접 이어서 개선 폭을 계산하지 않는다.

| 조건 | 수정 전 RMS | 최종 RMS | 독립 seed RMS |
|---|---:|---:|---:|
| 방향광 / SSS | 6.2598% | **4.1009%** | 0.2276% |
| 방향광 / metal + SSS + coat | 2.0096% | **2.0373%** | 0.1336% |
| Furnace / SSS | 4.2380% | 4.2380% | 0.2265% |
| Furnace / metal + SSS + coat | 3.2178% | 3.2178% | 0.1645% |
| 방향광 / tinted transmission | 33.9664% | 33.9664% | 0.1335% |
| 방향광 / pure glass | 34.7890% | 34.7890% | 0.0730% |
| Furnace / tinted transmission | 15.0987% | 15.0987% | 0.0752% |
| Furnace / pure glass | 0.6969% | 0.6983% | 0.0263% |

반경 단위만 변경한 shader snapshot A/B에서도 이전 16384-sample 기준의 sun SSS는
6.2429→4.0903%로 줄었다. 혼합 sun은 2.0129→2.0442%로 소폭 악화했고,
furnace는 그대로였다. Glass budget 단독 수정의 이미지 영향은 작았다.
이 진단을 제품 전체 수용으로 세지 않는다.

### 기존 target 판정

RMS≤1%, p95 normalized≤1%, max normalized≤5%, noise RMS≤0.25%를 모두 만족해야 한다.
현재 **7/18 통과**: sun IOR=1·Scale=0·SSS off·emission·white,
furnace IOR=1·emission. Scale=0/off 전체 이미지 제어는 별도로 모두 통과한다.

- sun SSS와 mixed는 RMS 미달이다.
- furnace SSS/mixed는 RMS·p95 미달이다.
- tinted transmission은 두 조명 모두 큰 차이가 남는다.
- pure glass furnace는 RMS 0.6983%여도 p95 **1.0898%**로 미달이다.
- furnace Scale=0/off와 white도 p95 미달이다. 기존 flat 진단의 geometry/shading-normal 기여를
  후속 조사에 사용하되 smooth 기준을 flat으로 교체하지 않는다.

수렴한 seed 변동보다 큰 차이가 남으므로 기준 노이즈만을 원인으로 삼지 않는다.

## 실행 검증과 제한

- VS18/v145 **전체 Debug CreatorEditor 빌드**와 Release/Debug Native 비교 도구 빌드 통과.
- 최종 host 10 **Debug/Release DX12 각각 18 frames, 295717 checks, GPU validation 0**, 정상 종료.
  두 빌드의 **294912 RGBA 성분 byte exact**, Scale=0/off도 두 조명 모두 전체 파일 byte exact다.
  최종 589개 source snapshot의 실행 중 변경은 0개다.
- 반경 수정 직후 host 9에서 Debug/Release 각각 SSS **12 frames, 119415 checks**,
  굴절 **24 frames, 185068 checks**를 통과했다. 각 validation 0, 정상 종료.
  프로파일·이웃 확산·재질 격리·최종 색상과 화면 hit/miss·Snell/TIR·rough convolution을 검사했다.
  이 head-on fixture를 나중에 찾은 스침각 오류의 검증으로 대신하지 않았다.
- 공용 Special **202049 checks**, 35 cases×6 views×7 variants,
  28 compile/22 rejection, 기존 4410행 numeric golden과 역사적 불변 성분 보존 통과.
- 최종 IBL bake/consume Debug/Release 각각 **19351 checks**, 152 points·3 frames·6 compile,
  최대 normalized error 0.000005452. Standalone bake의 기존 입력/소비 계약 회귀 통과.
- 현재 reader로 이전 Volume 캡처를 재판정하여 **8/8**을 확인했다. 신규 Volume GPU 캡처는 아니다.
- 새 Editor GUI 조작·Vulkan 실제 GPU 실행·cooked Player 실행·모델 FPS 수용은 이 묶음에 포함하지 않는다.
  제품 준비의 DXIL/SPIR-V compile과 실제 DX12 GPU 실행을 구분한다.

Scene SSS는 가시 surface의 world 거리·projected area·같은 draw owner·normal 조건과
4픽셀 gather를 사용한다. Cycles Random Walk의 내부 산란과 같은 공간 응답이 아니다.
Glass는 완성된 opaque Scene의 depth/background 또는 environment를 한 경계에서 샘플링한다.
출구 경계·그 경계의 굴절/반사·내부 재방문을 추적하지 않는다.
RGB tint를 임의로 두 번 곱하여 폐곡면 검증을 대체하지 않는다.
Scene Blended queue도 미지원이며 opaque 캡처로 alpha+transmission 완료를 주장하지 않는다.

## 재현과 다음 기존 작업

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' -b --threads 3 `
  --python Tools/blender/material_matched_reference.py -- `
  --suite special-surface --samples 131072 --seed 0 --output Build/Obj/Mat9SpecialSurfaceReference-0
# seed 11은 별도 출력 디렉터리로 생성한다. 기존 결과를 덮어쓰지 않는다.
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release `
  -Label special-surface-final10 -Reference Build/Obj/Mat9SpecialSurfaceReference-0 `
  -ReferenceRepeat Build/Obj/Mat9SpecialSurfaceReference-11
& 'C:/Python313/python.exe' Tools/regression/assess-special-material-images.py `
  Build/Obj/Mat9Images-Release-special-surface-final10/comparison.json `
  Build/Obj/Mat9Images-Release-special-surface-final10/targets.json
# exit 2는 정상 측정 후 target 미수용이다. Shader prepare/GPU 실패와 구분한다.
```

RGBA32F/EXR·입력·삼각형·실행 로그는 위 Build/Obj 디렉터리에 보존했다.
최종 source snapshot은 `Build/Obj/mat9-special-surface-final10-source-hashes.json`이다.
작업 HEAD는 `12f970c7ed3a4408d268479f5d5acb5f80372b8c`, 기존 dirty tree에서 진행했다.
이번 요청에서 commit/push하지 않았다.

Python 4개 구문 검사, 웹 빌드, 대시보드 inline JavaScript/전체 439항목 파싱·진행률 finite·
phase-meta 산수와 범위 diff 검사를 확인한다. 이는 문서/정적 검사이며 새 렌더 검증으로 세지 않는다.

기존 순서를 유지한다:

1. **폐곡면 굴절 출구 경계·SSS 공간 응답·smooth grazing normal**을 분리하여 개선하고,
   위 고정 target을 다시 판정한다. 실제 Blended 합성의 alpha/transmission 기준도 남아 있다.
2. Texture-only/factor×texture/normal-map 기준과 같은 지원 feature의 Deferred/Forward 교차 비교.
3. 실제 모델의 이동 카메라/tier/cold-warm 성능 수용. 추가 IBL/Special 준비 비용을 포함한다.
4. 원래 area-light grid의 light ABI/consumer와 rendered gate.

새 완료 행이나 공수를 추가하지 않는다. 상수 HDRI·bounded Volume의 통과 범위를 그대로 유지한다.
