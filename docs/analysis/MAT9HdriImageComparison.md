# MAT-9 HDRI 대조와 Scene IBL 필터 수정 — 2026-10-01

## 판정과 범위

**HDRI 전체 수용은 미달이다.** 앞선 방향광/단색 환경의 GGX 32조건 통과를 실제 HDRI로
확대하지 않는다. 고정 Core/Layered 상수 재질 10종을 `forest`와 기존 MAT-0의 `autumn`
환경에서 각각 비교했다. 발광·흰색 확산·거울 제어를 포함한 **26장**이며, 원래 MAT-0
EEVEE 텍스처/면광원 grid를 수정하거나 이 측정으로 대체하지 않았다.

동일한 scene-linear RMS 1%·p95 normalized 1%·max normalized 5%·독립 reference noise
0.25% 목표를 사용한다. 선형 필터 수정 후 **재질 1/20, 제어 3/6**이 모든 목표를 통과했다.
다수 조건의 4,096-sample reference noise가 목표를 넘으므로 작은 잔여 차이의 수용 판정은
추가 수렴 확인이 필요하다. 큰 차이 역시 단순한 렌더 노이즈로 처리하지 않는다.

- [수정 전 비교](MAT9HdriBaselineComparison.json), [선형 필터 후 비교](MAT9HdriLinearComparison.json).
- [픽셀을 재계산한 판정](MAT9HdriAcceptance.json). 변경된 입력/이미지/조명/cook는 거부한다.
- MAT-9 progress 및 완료 공수 **32/34일**을 유지한다. FPS/최초 준비 시간 수용 자료가 아니다.

## 동일 입력 계약

| 항목 | 조건 |
|---|---|
| Blender | 5.1.1 `b70da489d7f4`, Cycles CPU, seed 0/11, adaptive/denoise off |
| 원래 MAT-0와 구분 | MAT-0는 EEVEE, 800×480 텍스처/디스크 면광원 grid. 이번 것은 추가 Cycles 상수 재질 fixture |
| 캡처 | 64×64, 같은 구체 triangle corner/normal/UV tangent, perspective eye (0,0,3), FOV 45° |
| 색공간 | linear Rec.709, Raw, exposure 0, metrics에는 표시 OETF/톤맵 미적용 |
| 밝기 | 두 환경 모두 radiance ×0.35. 직접광 없음. native는 cook 조명 맵 RGB에 한 번 적용 |
| Native | 실제 Scene graph 생성·컴파일·instance·GBuffer·lookup·합성, AO white, shadow/decal/post off |
| Cook | 제품 EnvironmentCooker, cube 512/7 mip, irradiance 64, specular 6 mip, BRDF 512, RGBA16F |
| Mask | emission 공통 interior 345픽셀, 2픽셀 침식, 모든 재질에 동일 적용 |

`forest` source SHA는 `bdf2298244affa0f85509380fd130ac6d4dfaa3c856df065998f7f4c1a93dc0d`,
`autumn`은 `e60470d3a0f219585df1d74c393b472361c5400a7ff8d071ebe6eca29b7fe2b0`다.
Source와 cooked artifact SHA를 모두 manifest에 고정했다. Autumn cook의 네 맵 GPU 왕복은 exact다.
이번 실행은 `Resources/Environment/forest.ceibl`을 변경하지 않았다.

공유 geometry의 좌표를 유지하면서 Blender World에 outgoing `(x,-z,y)`를 전달한다.
엔진의 longitude `atan2(z,x)`/Y-up/top-down 행과 Cycles의 bottom-up 좌표를 맞춘다.
[Cycles equirectangular 정의](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/camera/projection.h)와
[environment texture 평가](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/svm/image.h)를 확인했다.
같은 HDRI 파일을 지정했다는 사실만으로 좌표/필터/IBL/BRDF/색관리 일치를 주장하지 않는다.

## 수정한 실제 문제

Scene lookup의 s0는 material field texture를 위한 **point** sampler다.
`BakeRadiance`도 그 sampler를 공유해 HDR 큐브를 point-filter했다. Scene 경로의 radiance는
이미 있는 linear s1 `gIblSampler`를 사용하도록 바꿨다. Material field texture 샘플러는 유지한다.
독립 IblBaker의 기존 진단 fixture 계약은 이번에 변경하지 않았다.

SceneHost identity **4→5**로 기존 cooked Scene shader 재사용을 거부한다.
IBL point/sample ABI와 resource binding 슬롯, product 기본 적분 **1,024 samples**는 유지했다.
코드 수정은 필터 차이를 줄였으며 큰 HDRI 오차 전체를 해결한 것은 아니다.

## 결과 — relative RMS

| 조건 | forest point | forest linear | autumn point | autumn linear |
|---|---:|---:|---:|---:|
| Core base | 1.2908% | 1.2842% | 28.6327% | 26.1698% |
| Core metal | 2.9803% | 3.0335% | 36.9344% | 27.2087% |
| Core rough | 1.0527% | 1.0454% | 8.0634% | 7.9319% |
| Core normal input=geometry | 1.0543% | 1.0292% | 16.0015% | 14.6983% |
| Core emission | 0.0962% | 0.0959% | 1.3951% | 1.4363% |
| Coat | 1.2457% | 1.2055% | 23.1551% | 23.0449% |
| Sheen | 1.7815% | 1.7755% | 34.9040% | 34.1491% |
| Anisotropy | 11.4086% | 10.8817% | 74.8181% | 70.4064% |
| Thin film | 7.1893% | 7.1833% | 80.3491% | 84.2521% |
| Mixed layers | 1.4800% | 1.4628% | 25.3326% | 24.2309% |
| Emission control | 0 | 0 | 0 | 0 |
| Diffuse white control | 0.5811% | 0.5811% | 5.5984% | 5.5984% |
| Mirror control | 8.2410% | 2.6332% | 0.7838% | 0.5034% |

4,096-sample seed RMS 최대는 forest **0.8201%**, autumn **1.2633%**다.
Mirror seed RMS는 **0.2361% / 0.0139%**다. 거울 결과에는 cube 재투영·해상도/필터 차이도
남으므로 모든 반사 오차를 재질 BRDF에 귀속하지 않는다.

## 고샘플 분리 진단

가장 큰 autumn film 조건과 발광/거울 제어만 별도 고정 subset으로 **131,072 samples**,
seed 0/11을 렌더했다. 같은 공통 mask 정책으로 344픽셀을 사용한다. 이 subset은 전체
26장 case-set 게이트를 통과할 수 없고 전체 HDRI 수용 결과를 대체하지 않는다.

| autumn film | Relative RMS | p95 normalized | max normalized |
|---|---:|---:|---:|
| 제품 1,024 environment samples | 84.1182% | 37.5236% | 518.6446% |
| 진단 snapshot 32,768 samples | 13.7322% | 9.5771% | 29.5575% |

고샘플 기준의 seed 간 RMS는 **0.1587%**다. 두 native 경로의 거울 결과는 동일하며 RMS
0.5093%, emission은 exact다. 환경 적분 수만 바꿔 큰 오차가 줄어드는 것을 확인했으므로
현재 적분 샘플링의 기여는 실측 근거가 있다. 남은 13.73%를 전부 샘플 수에 귀속하지 않는다.
Source→cube 투영/필터와 방향 분포/정규화 오차도 분리해야 한다.

- [현재 제품 고샘플 대조](MAT9HdriDenseCurrentComparison.json).
- [진단 샘플 수 대조](MAT9HdriSamplingStudyComparison.json).
- `Build/Obj/Mat9Hdri-sampling-study/contract.json`에 원본/진단 shader SHA와 샘플 수를 고정했다.
  `-ShaderRoot`는 probe 전용이며 `shader-root.txt`에 실제 경로를 기록한다.
  전체 수용 판정기는 diagnostic shader snapshot을 거부한다.
- 진단 snapshot은 Build/Obj에만 있다. product shader의 적분 수/작가 설정/기본 cook는 변경하지 않았다.

## 검증 증거

- Release DX12 기본/linear 각 **433,367개 검사**, 26장, GPU validation 0건, 정상 종료.
- Host identity 5를 포함한 현재 제품 고샘플 subset **55,892개**, 진단 snapshot **55,893개** 검사,
  각각 3장·validation 0건·정상 종료.
- Scene raster/shared depth/composition/texture/cache-generation 회귀 **21,067,374개** 검사,
  DXIL/SPIR-V 24개 컴파일, validation 0건, 정상 종료.
- CreatorEditor Debug 전체 빌드 및 최신 shader resource 배치 성공.
- Dashboard full parse 439행, 유한 progress. MAT-9 상태와 완료 공수는 변경하지 않았다.
- 독립 reference noise/입력/cook SHA와 진단 subset·snapshot 거부를 확인했다.

GPU validation을 켠 정확도 실행이다. 이 로그의 준비 시간이나 샘플 수를 늘린 진단 실행을
실제 Scene FPS 또는 GPU 비용 수용으로 사용하지 않는다.

## 후속 순서와 면광원 경계

1. 고샘플 reference에서 큰 박막 오차를 분리한다. 진단 shader snapshot의 샘플 수를 바꿔
   environment integration 수렴을 측정하며 product 기본값은 높이지 않는다.
2. 고주파 HDR의 환경 분포와 BRDF를 함께 표집하는 방향별 MIS를 검토·수정한다. 현재
   GGX/Sheen 1,024 샘플은 환경 밝기의 중요도 분포를 사용하지 않는다. Cube radiance/PDF,
   prepared owner/fence, cook/cache generation과 constant environment 보존 gate를 함께 다룬다.
3. 확산은 제품의 MIS-convolved 64 cube를 쓰므로 방향별 반사 변경만으로 해결됐다고 세지 않는다.
   white diffuse 제어 및 source→cube→importance grid→irradiance의 차이를 따로 측정한다.
4. 같은 target과 수렴한 reference로 HDRI 재대조 후 Special·texture/normal-map·route parity·성능 순서로 진행한다.

**면광원은 현재 미지원이다.** `EnhancedLight.position.w` ABI와 실제 Scene light consumer는
directional/point/spot 세 타입만 지원한다. MAT-0의 energy 850/직경 4 disk area를 point light나
작가가 지정하지 않은 다중 point 묶음으로 바꾸고 원래 fixture 통과로 기록하지 않는다.
MAT-9 내 미충족 조명 gate로 유지하며 emission 면적/방향/PDF·단위·extent, product direct lobe
평가와 고정 disk 수렴 기준을 구현해야 원래 grid의 area 조건을 수용할 수 있다.
새 완료 행이나 추정 공수를 추가하지 않는다.

## 후속 MIS 구현

2026-10-01 Scene 환경/BRDF MIS와 CEIBL002의 중요도 맵 쿠킹을 적용했다.
최종 dense autumn 박막 RMS는 3.6083%이며 전체 target은 여전히 미달이다.
이 문서의 baseline/linear 자료는 v1 cook 측정으로 유지한다. v1 cook은
`Build/Obj/Mat9Hdri-frozen-cooks/<SHA>.ceibl`에 보존하고, 원본 조명 맵과
byte-identical인 v2 cook metadata를 가진 `Build/Obj/Mat9Hdri-v2-*` reference를 생성했다.
Blender pixels는 재사용했으며 재렌더로 보고하지 않는다.
[후속 구현·수치·준비 비용·잔여](MAT9HdriMis.md)를 참조한다.

## 재현

`material_matched_reference.py --lighting hdri --environment-config <json>`은 두 source/cook의
경로·SHA와 strength 0.35를 검증한다. `--only-case`는 명시된 진단 subset이며 전체 수용 판정기가
26장 고정 case set을 요구하므로 subset을 전체 통과로 제출할 수 없다.

```powershell
# Build/Obj/Mat9Hdri-environments.json: current frozen source/cook identities
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background --factory-startup `
  --python Tools/blender/material_matched_reference.py -- --output Build/Obj/NewHdriReference `
  --lighting hdri --samples 4096 --environment-config Build/Obj/Mat9Hdri-environments.json
# Repeat into another new directory with --seed 11 and the same samples/config.
Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label hdri-recheck `
  -Reference Build/Obj/NewHdriReference -ReferenceRepeat Build/Obj/NewHdriRepeat
C:/Python313/python.exe Tools/regression/assess-hdri-material-images.py `
  Build/Obj/Mat9Images-Release-hdri-recheck/comparison.json Build/Obj/hdri-recheck-acceptance.json
```

판정 exit 2는 측정 성공이지만 image target 미달이다. exit 1은 입력/출력 동일성 또는 실행 오류다.
모든 출력은 새 디렉터리에 생성하고 기존 reference/native 증거를 덮어쓰지 않는다.
