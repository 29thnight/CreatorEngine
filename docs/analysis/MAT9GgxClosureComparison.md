# MAT-9 공통 GGX 반사·레이어 감쇠 대조 — 2026-10-01

## 판정

**고정 박막 on/off 32조건이 기존 이미지 목표를 모두 통과했다.** 박막 8종 × 두 조명과
같은 입력의 thickness=0 대조군을 사용했다. 이전 cutoff 수정 단계의 박막 on 판정은
4/16이었다. 이번 판정은 on **16/16**, off **16/16**이며 허용치를 높이지 않았다.
MAT-9 전체 완료나 모든 씬의 성능 수용을 의미하지 않는다. 완료 공수는 **32/34일**이다.
이후 [HDRI 대조](MAT9HdriImageComparison.md)에서 Scene radiance 필터와 host identity 5를
적용했다. 이 문서의 단색 환경 수용을 HDRI 전체 수용으로 확대하지 않는다.

- scene-linear relative RGB RMS ≤1%, p95 normalized ≤1%, maximum normalized ≤5%.
- 독립 Blender 시드 간 RMS ≤0.25%. normalized는 `abs(native-reference)/max(1,abs(reference))`.
- [수용 JSON](MAT9GgxFilmAcceptance.json), [36장 비교·이미지 SHA](MAT9GgxClosureComparison.json).
- [이전 4/16 결과](MAT9ThinFilmAcceptance.md)는 변경 전 증거로 보존한다.

## 원인과 변경

1. 금속과 유전체를 섞은 총반사율로 유전체 diffuse를 다시 감쇠했다. 이제 metallic 가중치는
   한 번만 적용하고, diffuse는 유전체 closure의 layering albedo로 감쇠한다.
2. GGX 부족 에너지를 Lambertian lobe로 더해 직접광의 반사 모양이 달랐다. 이제 금속·유전체·
   coat의 기판 Fss를 각각 계산하고 `1 + Fms*(1-E)/E`를 각 GGX 반사 lobe에 곱한 뒤 합친다.
   IBL 적분·방향별 환경 convolution도 같은 응답을 사용한다. 추가 diffuse 보상은 없다.
3. 박막이 꺼지면 Scene Principled 그래프가 기존 Core 계산으로 되돌아갔다. Core 그래프에도
   같은 GGX base를 적용했다. feature/tier는 작가 기능 분류로 유지하고 `viewTier.w`는 계산 모델
   선택으로 명확히 했다. GPU Scene host, CPU SurfaceEvaluator, Scene packet 검증을 맞췄다.
   일반 Core/glTF 공용 함수와 독립 legacy IBL 모델 0은 유지한다.

Blender 5.1.1의 [GGX 에너지 보상](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_microfacet.h)과
[Principled closure 조합](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/svm/closure.h)을 기준으로 했다.
`ggx_E` 32×32, `ggx_Eavg` 32, dielectric albedo 16³의 **5,152 float / 20,608 byte**를
Apache-2.0 고지·원본 SHA와 함께 고정했다. 실제 GPU 배치 크기나 FPS 비용으로 환산하지 않는다.
`export-ggx-energy-tables.py`는 원본 파일 SHA가 다르면 생성을 거부한다.

Coat layering은 closure의 darkened weight로 감쇠한다. 박막의 평균 보상은 film 색의 평균으로
대체하지 않고 기판 Fss를 사용한다. film IOR=1 및 기판과 일치하는 무효 인터페이스의
기존 불변식은 보존한다. Glass의 단일 인터페이스 transmission budget은 이번에 재설계하지 않았다.

## 결과 — relative RMS

| 조건 | film sun | off sun | film furnace | off furnace |
|---|---:|---:|---:|---:|
| 0.25nm, IOR 1.4, metal 0.6 | 0.0537% | 0.0553% | 0.1444% | 0.1460% |
| 100nm, IOR 1.5, dielectric | 0.2039% | 0.0410% | 0.2050% | 0.1176% |
| 1000nm, IOR 2, dielectric | 0.0453% | 0.0410% | 0.1309% | 0.1176% |
| 100nm, IOR 1.5, metal 1 | 0.0689% | 0.0616% | 0.1086% | 0.1142% |
| 1000nm, IOR 2, metal 1 | 0.0693% | 0.0616% | 0.1152% | 0.1142% |
| 200nm, IOR 3, metal 0.6 | 0.0594% | 0.0553% | 0.1241% | 0.1460% |
| 550nm, IOR 1.4, metal 0.6, rough 0.88 | 0.0421% | 0.0427% | 0.1685% | 0.1638% |
| 550nm, IOR 1.4, metal 0.6, coat 0.7 | 0.1079% | 0.1027% | 0.1792% | 0.1671% |

거친 혼합 금속의 off 오차는 sun **35.5239→0.0427%**, furnace **19.6764→0.1638%**로 줄었다.
박막 on RMS 최대는 **0.2050%**다. 공통 침식 mask 345픽셀, cosine **0.57404~0.99843**,
normal/view diagnostic 및 Blender 5.1.1 `b70da489d7f4` / Cycles CPU 1,024 samples / seed 0·11을 유지했다.
낮은 cosine 구간의 rendered 이미지 수용이나 보편적인 JND 기준으로 확대하지 않는다.

## 회귀·캐시 계약

- Scene host identity **3→4**. 이전 Scene shader product의 재사용을 막는다.
- IBL point **176 byte**, sample **144 byte**와 자원 바인딩 슬롯은 유지했다.
  모델 1의 `singleScatter`는 이제 보상된 반사 적분이다. 다시 multiple scatter를 더하지 않는다.
- 강한 anisotropy에서는 등방성 E LUT가 근사다. prepared directional integral과 남은 diffuse
  budget에서 정규화해 직접광·IBL에 똑같이 적용한다. pixel shader에서 적분하지 않는다.
- alpha 상한 1을 Cycles와 맞췄다. 기존 white furnace 상한 **1.005**를 유지했다.
- RGB-3 / spectral / 0.1nm cutoff 수치 파일을 보존하고 버전 4 `numeric-golden-ggx.csv`를 별도 추가했다.
  CPU double oracle이 생성한 1,960/4,410행과 변경되지 않은 optical/profile 성분을 각각 검증한다.
- 판정기 v2는 off의 실제 픽셀·독립 시드 지표도 재계산한다. film만 통과하고 off가 실패한
  중간 결과와 수정된 off metric을 거부하는 검사 2건을 통과했다.

Native 최종 36장: **597,673개 검사**, GPU validation **0건**, 정상 종료.
최종 raw 에너지 제한 보정 후 `Mat9Images-Release-ggx-budget-final`에서 재촬영했다.
직전 판정의 36개 재질/제어 이미지와 3개 diagnostic, 총 **39개 float 이미지의 SHA가 모두 동일**했다.
수치 테스트와 제품 회귀의 최종 증거는 아래 기록을 사용한다.

| 검증 | 결과 |
|---|---|
| 기존 Core/Layered 이미지 24장 | 400,495개 검사, validation 0건, emission 제어 오차 0 |
| 상수 재질 20조건 | 동일 수치 목표 이내, RMS 최대 0.1999%, p95 최대 0.1958%, max 최대 0.8245% |
| Layered | 216,776개 검사·5,712개 furnace 검사·14개 DXIL/SPIR-V 컴파일·8개 거부 |
| 금속/유전체 불변식 | CPU 135개 검사 |
| 순백 금속 에너지 budget | 4종·32시선·GPU 3,352개 검사, raw 에너지 초과 8시선 포함 |
| Special | 202,049개 검사·4,410개 furnace 검사·28개 컴파일·22개 거부 |
| IBL bake | Debug/Release 각각 19,351개 검사·18,240 GPU 성분 |
| Surface batch | Release 19,387개 검사·19,092 GPU 성분·8프레임 |
| Scene packet | Debug/Release 각각 165개 검사·2개 in-flight 제출 |
| Scene raster·합성 | Release 21,067,374개 검사, shared depth·texture·generation·lookup reuse 포함 |
| Editor | 최신 shader resource를 포함한 Debug 전체 빌드 |
| 계획/대시보드 | 439행 full parse·유한 progress·Vite build |

[기존 Core/Layered 비교 JSON](MAT9GgxCoreComparison.json)은 이미지 SHA와 독립 시드 변동을 기록한다.
거친 Core sun RMS는 1.941%에서 0.0380%로 줄었다. 대조 contact sheet에서도 같은 조명/형상을
확인했다. CPU golden과 GPU 일치만으로 이 rendered 판정을 대신하지 않았다.
Shader 단일 적분을 미리 1로 clamp하면 direct 보상의 초과가 숨겨지므로 raw 적분을 보존한다.
남은 에너지로 **직접광·IBL을 같이** 정규화하며, 순백 금속 회귀로 이를 확인했다.
본 실행은 GPU validation을 켠 정확도 검사다. 로그의 lookup 시간은 성능 수용 자료로 사용하지 않는다.

## 재현과 남은 범위

```powershell
Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label ggx-film-recheck `
  -Reference Tools/blender/fixtures/material-thin-film-5.1.1 -ReferenceRepeat Build/Obj/Mat9FilmSweep-reference-11
C:/Python313/python.exe Tools/regression/assess-thin-film-images.py `
  Build/Obj/Mat9Images-Release-ggx-film-recheck/comparison.json Build/Obj/ggx-film-recheck.json
```

출력 폴더는 새 이름을 사용한다. 독립 seed-11 reference는 기존 fixed contract로 재생성할 수 있다.
현재 shader를 포함한 실제 Scene 이동 카메라·cold/warm·tier 성능은 이번 이미지 검증으로
수용하지 않는다. 원래 grid의 area-light/HDRI 조건, Special transport와 texture/normal-map 대조,
Deferred/Forward route parity 및 실제 Scene 성능 판정이 남는다.
