# MAT-9 살아 있는 화면 split-sum 근사의 Blender 차이 — 2026-10-04

## 판정

이 기록은 **통과 판정이 아니라 차이 표기**다. 2026-10-04 결정(계획서 §5 같은 날짜 절)으로
살아 있는 화면의 재질 IBL 룩업은 split-sum 근사(PR #117, `23a393cb`)만 쓰고, Blender 대비 차이를
%로 적고 넘어간다. 기준 적분 경로(`lookupApproximate = false`, BRDF 1024 / environment 4096)의 수용
상한(RMS≤1%·p95≤1%·max≤5%)은 근사 경로에 적용하지 않는다.

같은 소스(`2ee83fcd` + 아래 도구 수정), 같은 Release DX12 탐침·고정 장면·target으로 두 경로를 찍었다.

| 묶음 | 조건 | 기준 RMS 평균 / 최대 | 근사 RMS 평균 / 최대 | 근사 최대 조건 |
|---|---:|---:|---:|---|
| 평행광·균일 환경 (`material-matched-5.1.1`) | 24 | 0.08% / 0.18% | 1.94% / 14.27% | `furnace-layer_thin_film` |
| HDRI forest·autumn (`Mat9Hdri-v5-dense-0`, 반복 시드 11) | 26 | 0.30% / 0.50% | 8.97% / 60.42% | `autumn-layer_anisotropic` |
| 특수 부피 (`Mat9SpecialVolumeReference-0`, 반복 시드 11) | 8 | 0.07% / 0.10% | 0.07% / 0.10% | 차이 없음 |
| 특수 표면 SSS·유리 (`Mat9SpecialSurfaceReference-0`, 반복 시드 11) | 18 | 5.63% / 34.79% | 5.64% / 34.79% | 거의 같음(`furnace-special_subsurface_off` +0.37%p) |

기준 경로 수치는 10-01 기록(방향광·균일 환경 최대 0.1792%, HDRI 최대 0.4959%)과 같은 수준이고,
특수 표면 18장은 10-01 결과와 이미지 SHA-256 까지 같다. 그사이 기준 경로는 바뀌지 않았다.
특수 표면의 큰 값(투과·유리 34%)은 10-01 부터 남은 폐곡면 투과 미수용이며 근사와 무관하다.

## 읽는 법

- **평행광 조건은 두 경로가 같다.** 환경광이 없으니 룩업 근사가 끼어들 자리가 없다.
  부피도 룩업을 쓰지 않아 같다. 특수 표면(SSS·유리·투과)도 거의 같다 — 그 전송 경로는
  자기 준비 적분을 쓰고, 근사가 닿는 것은 위에 얹힌 반사 성분뿐이다.
- **환경광을 받는 거친 유전체(기본·거칠기·법선·쉰)는 1~3%** 대다.
- **금속은 균일 환경 9.45%, HDRI 24.69~27.61%.** split-sum 은 반사 방향 하나의 prefiltered 조회로
  로브 전체를 대신하므로, 밝기 차가 큰 HDRI 에서 금속 반사가 크게 갈린다.
- **이방성은 균일 환경 13.96%, HDRI 31.15~60.42%.** split-sum 에 이방성 로브가 없다.
- **박막은 균일 환경 14.27%, HDRI 15.88~21.20%.** 해석적 DFG(EnvBRDFApprox)에 박막 간섭이 없다.
- **코트·혼합은 HDRI 에서 6.11~16.65%.** 위 세 원인이 겹친다.
- max normalized 는 몇 픽셀의 하이라이트가 좌우한다(autumn 이방성 232.69%). 평균 감각은 RMS 로 읽는다.

## 묶음별 수치 (%)

### 평행광·균일 환경 24조건

| 조건 | 기준 RMS | 근사 RMS | 근사 p95 | 근사 max | 근사−기준 RMS |
|---|---:|---:|---:|---:|---:|
| `sun-core_base` | 0.04 | 0.04 | 0.01 | 0.02 | 0.00 |
| `sun-core_metal` | 0.07 | 0.07 | 0.00 | 0.44 | 0.00 |
| `sun-core_rough` | 0.04 | 0.04 | 0.01 | 0.01 | 0.00 |
| `sun-core_normal` | 0.04 | 0.04 | 0.01 | 0.03 | 0.00 |
| `sun-core_emission` | 0.05 | 0.05 | 0.07 | 0.08 | 0.00 |
| `sun-layer_coat` | 0.07 | 0.07 | 0.01 | 0.12 | 0.00 |
| `sun-layer_sheen` | 0.04 | 0.04 | 0.01 | 0.01 | 0.00 |
| `sun-layer_anisotropic` | 0.07 | 0.07 | 0.01 | 0.14 | 0.00 |
| `sun-layer_thin_film` | 0.10 | 0.10 | 0.01 | 0.20 | 0.00 |
| `sun-layer_mixed` | 0.07 | 0.07 | 0.01 | 0.13 | 0.00 |
| `sun-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `sun-control_white` | 0.04 | 0.04 | 0.02 | 0.02 | 0.00 |
| `furnace-core_base` | 0.13 | 1.65 | 1.54 | 1.83 | 1.52 |
| `furnace-core_metal` | 0.09 | 9.45 | 7.78 | 7.86 | 9.36 |
| `furnace-core_rough` | 0.13 | 0.38 | 0.52 | 0.93 | 0.25 |
| `furnace-core_normal` | 0.12 | 1.21 | 1.53 | 1.89 | 1.09 |
| `furnace-core_emission` | 0.04 | 0.30 | 0.79 | 0.98 | 0.26 |
| `furnace-layer_coat` | 0.15 | 1.80 | 1.59 | 2.19 | 1.65 |
| `furnace-layer_sheen` | 0.17 | 1.61 | 1.21 | 1.43 | 1.43 |
| `furnace-layer_anisotropic` | 0.17 | 13.96 | 10.97 | 11.14 | 13.79 |
| `furnace-layer_thin_film` | 0.18 | 14.27 | 12.08 | 12.39 | 14.09 |
| `furnace-layer_mixed` | 0.12 | 1.15 | 1.27 | 1.76 | 1.03 |
| `furnace-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `furnace-control_white` | 0.07 | 0.07 | 0.10 | 0.20 | 0.00 |

### HDRI 26조건

| 조건 | 기준 RMS | 근사 RMS | 근사 p95 | 근사 max | 근사−기준 RMS |
|---|---:|---:|---:|---:|---:|
| `forest-core_base` | 0.31 | 1.52 | 0.81 | 1.64 | 1.21 |
| `forest-core_metal` | 0.28 | 24.69 | 10.02 | 31.77 | 24.41 |
| `forest-core_rough` | 0.33 | 0.71 | 0.39 | 0.67 | 0.38 |
| `forest-core_normal` | 0.32 | 1.10 | 0.84 | 1.69 | 0.78 |
| `forest-core_emission` | 0.02 | 0.12 | 0.36 | 0.87 | 0.09 |
| `forest-layer_coat` | 0.29 | 6.11 | 1.03 | 19.66 | 5.82 |
| `forest-layer_sheen` | 0.32 | 1.91 | 0.90 | 1.77 | 1.58 |
| `forest-layer_anisotropic` | 0.48 | 31.15 | 18.63 | 68.55 | 30.67 |
| `forest-layer_thin_film` | 0.25 | 15.88 | 9.34 | 13.55 | 15.64 |
| `forest-layer_mixed` | 0.30 | 2.22 | 0.87 | 5.06 | 1.92 |
| `forest-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `forest-control_white` | 0.36 | 0.36 | 0.30 | 0.70 | 0.00 |
| `forest-control_mirror` | 0.47 | 2.68 | 2.22 | 15.01 | 2.21 |
| `autumn-core_base` | 0.38 | 1.49 | 1.38 | 3.70 | 1.12 |
| `autumn-core_metal` | 0.50 | 27.61 | 3.40 | 56.38 | 27.11 |
| `autumn-core_rough` | 0.38 | 0.86 | 0.76 | 1.10 | 0.48 |
| `autumn-core_normal` | 0.37 | 1.18 | 1.22 | 5.13 | 0.81 |
| `autumn-core_emission` | 0.03 | 0.19 | 0.55 | 2.17 | 0.17 |
| `autumn-layer_coat` | 0.36 | 16.65 | 0.96 | 52.85 | 16.29 |
| `autumn-layer_sheen` | 0.41 | 3.24 | 2.54 | 5.64 | 2.83 |
| `autumn-layer_anisotropic` | 0.21 | 60.42 | 62.08 | 232.69 | 60.20 |
| `autumn-layer_thin_film` | 0.36 | 21.20 | 13.81 | 70.63 | 20.84 |
| `autumn-layer_mixed` | 0.39 | 10.98 | 3.21 | 40.94 | 10.59 |
| `autumn-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `autumn-control_white` | 0.41 | 0.41 | 0.47 | 0.65 | 0.00 |
| `autumn-control_mirror` | 0.33 | 0.43 | 0.15 | 0.90 | 0.10 |

### 특수 부피 8조건

| 조건 | 기준 RMS | 근사 RMS | 근사 p95 | 근사 max | 근사−기준 RMS |
|---|---:|---:|---:|---:|---:|
| `sun-special_volume_emission` | 0.10 | 0.10 | 0.13 | 0.19 | 0.00 |
| `sun-special_volume_absorption` | 0.10 | 0.10 | 0.12 | 0.17 | 0.00 |
| `sun-special_volume_scattering` | 0.05 | 0.05 | 0.00 | 0.00 | 0.00 |
| `sun-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `furnace-special_volume_emission` | 0.10 | 0.10 | 0.13 | 0.19 | 0.00 |
| `furnace-special_volume_absorption` | 0.10 | 0.10 | 0.12 | 0.17 | 0.00 |
| `furnace-special_volume_scattering` | 0.09 | 0.09 | 0.01 | 0.01 | 0.00 |
| `furnace-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |

### 특수 표면 18조건

| 조건 | 기준 RMS | 근사 RMS | 근사 p95 | 근사 max | 근사−기준 RMS |
|---|---:|---:|---:|---:|---:|
| `sun-special_transmission` | 33.97 | 33.97 | 7.77 | 98.33 | 0.00 |
| `sun-special_glass` | 34.79 | 34.79 | 3.01 | 100.00 | 0.00 |
| `sun-special_ior_one` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `sun-special_subsurface` | 4.10 | 4.10 | 0.93 | 1.38 | 0.00 |
| `sun-special_subsurface_local` | 0.09 | 0.09 | 0.03 | 0.05 | 0.00 |
| `sun-special_subsurface_off` | 0.09 | 0.09 | 0.03 | 0.05 | 0.00 |
| `sun-special_mixed` | 2.04 | 2.04 | 0.48 | 1.05 | 0.00 |
| `sun-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `sun-control_white` | 0.09 | 0.09 | 0.04 | 0.06 | 0.00 |
| `furnace-special_transmission` | 15.10 | 15.10 | 15.92 | 16.11 | 0.00 |
| `furnace-special_glass` | 0.70 | 0.70 | 1.09 | 1.18 | 0.00 |
| `furnace-special_ior_one` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `furnace-special_subsurface` | 4.24 | 4.24 | 3.43 | 3.91 | 0.00 |
| `furnace-special_subsurface_local` | 0.95 | 0.95 | 1.15 | 2.55 | 0.00 |
| `furnace-special_subsurface_off` | 0.95 | 1.32 | 1.21 | 1.67 | 0.37 |
| `furnace-special_mixed` | 3.22 | 3.10 | 2.07 | 2.44 | -0.12 |
| `furnace-control_emission` | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| `furnace-control_white` | 1.00 | 1.00 | 1.96 | 3.07 | 0.00 |

## 도구 수정

- `measure-material-blender-images.ps1 -LookupApproximate` 가 `CREATOR_MAT9_LOOKUP_APPROXIMATE` 를 켜고,
  `material_matched_image_probe.cpp` 가 그때 `SceneHostBudget::lookupApproximate = true` 로 준비한다.
  근사 실행의 `Native/lookup-approximate.txt` 가 표식이다.
- 탐침의 첫 장 단정("Blended composition is not installed" 거부)을 지웠다. 그 거부는 `8bfd0be5` 에서
  제품 코드에서 사라졌고, 탐침은 그 뒤로 돌지 않아 기준 경로조차 첫 장에서 멈췄다.
- **유리 장 게시 거부 수정.** `8bfd0be5` 이후 투과·알파 draw 는 공통 Forward+ 로 그려지는데, 탐침은
  GBuffer·지연 조명 뒤 `DeclareVolume` 만 불러 유리가 Forward+ 흐름에 선언되지 않았다. 그래서
  `CommitSubmittedFrame` 이 "LX alpha cache publication requires the complete Forward+ stream." 으로
  게시를 거부했다. 탐침을 제품 렌더러 순서(`DeclareColor` 반환값 → `EnhancedForwardPass` → 남은
  `DeclareVolume`)로 맞췄다. 수정 전 기준 경로 붉음(3번째 장) → 수정 후 18/18, 10-01 이미지와 해시 일치.
  부피 8장·평행광/균일 24장도 수정 전 탐침의 이미지와 해시가 같다.
- **오류 문구가 늘 비던 원인 수정.** `Check(f(error), "label " + error)` 는 MSVC 에서 메시지 인자를 먼저
  만들어 호출 전의 빈 `error` 를 실었다. 17 자리를 `CheckWith(f(error), "label ", error)` 로 바꿔 실패 때
  실제 문구가 나온다.

## 재현

```powershell
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label split-ref-matched
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label split-approx-matched -LookupApproximate
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label split-approx-hdri -LookupApproximate `
  -Reference Build/Obj/Mat9Hdri-v5-dense-0 -ReferenceRepeat Build/Obj/Mat9Hdri-v5-dense-11
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label split-approx-volume -LookupApproximate `
  -Reference Build/Obj/Mat9SpecialVolumeReference-0 -ReferenceRepeat Build/Obj/Mat9SpecialVolumeReference-11
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label split-approx-surface -LookupApproximate `
  -Reference Build/Obj/Mat9SpecialSurfaceReference-0 -ReferenceRepeat Build/Obj/Mat9SpecialSurfaceReference-11
```

기준 경로는 같은 명령에서 `-LookupApproximate` 를 빼고 `split-ref-*` 이름으로 찍었다.

원자료: `MAT9SplitSum{Reference,Approximate}{Matched,Hdri,Volume,Surface}.json`,
[HDRI 근사 이미지 대조](MAT9SplitSumApproximateHdri.png).
