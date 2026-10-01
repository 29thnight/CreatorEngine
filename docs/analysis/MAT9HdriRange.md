# MAT-9 HDR 에너지 보존과 큐브 변환 대조

2026-10-01. 기존 MAT-9의 source/cube·diffuse·mirror 검증 단계다.
전체 MAT-9 완료 또는 전체 HDRI 재질 수용으로 세지 않는다.

후속 [forest 거울 수정](MAT9MirrorSampling.md)은 CEIBL004/source 보존·픽셀 중심 보정으로
같은 7조건을 7/7 통과했다. 아래 v3 수치와 identity는 당시 검증 기록이다.

## 원인 분리

원본 HDR/EXR을 linear Rec.709 RGBA32F로 디코드하고, 모든 원본 텍셀과
큐브 텍셀의 정확한 입체각으로 E/pi를 독립 적분했다. Native normal/view 및
동일한 emission interior 344픽셀을 사용했다. 계산은 이미지 수용 게이트의 대체물이 아니다.

`autumn_field_puresky_1k.hdr`의 RGB 최대값은 123,904 / 112,640 / 95,232다.
기존 `SanitizeRadiance`와 RGBA16Float 큐브는 이를 64,000으로 잘랐다.
기존 cube/source 확산 적분 차이는 **5.5749%**, bake/dense cube 차이는
**0.1215%**였다. 이 조건의 큰 확산광 오차는 중요도 격자의 128칸 축소보다
앞단의 HDR 에너지 손실이 주원인이었다. 원본 source SRV는 한 mip만 노출하므로
implicit LOD가 낮은 mip을 선택하는 문제로 판단하지 않았다.

Float32 단일 중심 변환에서는 작은 태양의 큐브 격자 aliasing으로 확산 적분 차이
0.9371%가 남았다. 변환 시 4×4 texel footprint와 cube solid-angle Jacobian을
사용하면 **0.3251%**로 줄었다. 샘플의 밝기는 자르지 않는다.
forest의 cube/source 확산 차이는 0.0573→**0.0028%**다.

- [autumn 최종 독립 적분](MAT9HdriRangeAutumnIntegrals.json)
- [forest 최종 독립 적분](MAT9HdriRangeForestIntegrals.json)
- 수정 전/중간 분리는 `Build/Obj/Mat9Hdri-integrals-*-before.json` 및 `*-interim.json`.

## 제품 변경

- 원본 큐브·GGX prefilter를 RGBA32Float로 저장한다. irradiance·BRDF는 RGBA16Float 유지.
- 환경 전용 finite/nonnegative sanitation은 유효한 양수 HDR 값을 보존한다.
  일반 표면/최종 출력의 기존 sanitation 정책은 별개다.
- 원본→큐브는 mip 0을 명시하고 4×4 입체각 가중 footprint를 쿠킹 때만 계산한다.
- Forward·Deferred·SkyBox 및 Scene lookup은 실제 자원 형식을 바인딩한다.
- CDF sample의 셀 경계 반올림으로 다른 셀 PDF를 사용하는 사례를 재현했다.
  선택된 셀의 내부 0.01% 경계 여유를 shader·legacy CPU proposal에 동일 적용했다.
  최종 두 cook의 cached PDF/lookup PDF 최대 상대 차이는 각각 4.15e-7 이하.
- CEIBL003은 기존 7-map 배치를 유지하고 cube/prefilter의 float32 형식을 규정한다.
  source/recipe/checksum, atomic 저장, GPU upload/readback, warm 캐시 경로를 연결했다.
  CEIBL001/002는 이전 형식으로 읽을 수 있지만 현재 recipe에 맞지 않아 authoring cache miss다.
- SceneHost identity 7로 이전 제품 shader generation을 무효화한다.

| 기록 | 값 |
| --- | --- |
| recipe SHA | `fc859e1118ab07701e0a3944ec78ce7df7e3922bc3bb9ce7e46cb025cf93a0fe` |
| forest cook SHA | `df981ca1595ebce40a8d449c8dc199483f1db656879f02c3861b98fd8b083d89` |
| autumn cook SHA | `21cda237e5f3f338f78293637fdb6b306323c703d1bd715f8015a0e8ff31189a` |
| 크기 | 기존 61,090,928 → **94,640,240 bytes**, 58.26 → 90.26 MiB |
| 규격 | cube 512/7 mips, irradiance 64 E/pi, prefilter 6 mips, BRDF 512, CDF 512, MIS 1024+1024 |

기존 v2 forest 배포 파일은 `Build/Obj/Mat9Hdri-frozen-cooks/<SHA>.ceibl`에 보존했다.
배포 `Resources/Environment/forest.ceibl`과 Debug Editor 자원의 SHA가 일치한다.
EXR 원본은 배포/부트스트랩 의존성에 추가하지 않았다.

## 수렴한 고정 이미지 대조

Blender 5.1.1 Cycles CPU, 64×64, **131,072 samples, seed 0/11**로
7개 진단 조건을 새로 렌더했다. 원본·밝기 0.35·geometry·camera·Raw linear 조건과
RMS ≤1%, p95 normalized ≤1%, max normalized ≤5%, seed RMS ≤0.25%를 유지했다.
7조건 모두 seed noise 목표를 만족한다. 재질별 마스크 변경은 없다.

원래 Blender는 engine cook이 아닌 HDR/EXR을 읽는다. 원본과 기준 pixel/geometry SHA를
유지하고, engine cook만 바꾸는 별도 reference 사본을 만들었다. 원래 파일은 수정하지 않았다.
쿠킹이 조명 4-map을 바꾸었으므로 이전 작업의 4-map byte-equality 근거를 재사용하지 않는다.

| 조건 | v2 cook RMS | v3 cook RMS | seed RMS | 동일 target |
| --- | ---: | ---: | ---: | --- |
| forest emission | 0% | 0% | 0% | 통과 |
| forest white diffuse | 0.3248% | 0.3574% | 0.1098% | 통과 |
| forest mirror | 2.6413% | **3.1285%** | 0.00347% | 미달 |
| autumn thin film | 3.6083% | **0.8607%** | 0.1587% | 통과 |
| autumn emission | 0% | 0% | 0% | 통과 |
| autumn white diffuse | 5.4994% | **0.4107%** | 0.1597% | 통과 |
| autumn mirror | 0.5093% | 0.5531% | 0.00321% | 통과 |

7개 부분 집합에서 6개가 통과했다. 전체 26장/재질 grid 판정은 대체하지 않는다.
Native 전/후 각각 **121,540 checks, validation 0, 정상 종료**다.

- [수정 전 동일 dense 기준](MAT9HdriRangeBaselineComparison.json)
- [수정 후 동일 dense 기준](MAT9HdriRangeComparison.json)
- [고정 target 판정 — 전체 accepted=false](MAT9HdriRangeTargets.json)

## 남은 forest 거울 / 비용

forest mirror는 footprint 평균과 cube linear 재샘플링 영향으로 기존보다 오차가 증가했다.
현재 CPU cube sampling/Native 차이는 0.1277%, 원본 source/Cycles 차이는 1.2731%다.
후자에는 geometry normal/view·pixel filtering의 차이도 들어 있으므로 모두 큐브 해상도 탓으로
단정하지 않는다. Cube→source mirror resampling 차이는 3.7802%다.
별도 CPU Catmull-Rom 진단은 제품에 적용하지 않았다. 추가 조회 비용이 생기고 여전히 target 미달이다.
다음은 원본 source 보존·mirror 재구성 및 normal/view/filter 기여도를 분리하는 같은 mirror 게이트다.

이번 파일 형식 변경은 GPU 환경 texture 크기와 조회 대역폭을 늘린다. 쿠킹 footprint 계산은
cold 생성에만 발생하지만 float32 조회의 frame 비용까지 없어지는 것은 아니다.
GPU validation ON + 동시 CPU reference 생성 중의 cold 생성 시간은 비용 비교에 쓰지 않는다.
warm cook의 한 실행에서 read/checksum은 439.112ms였고 EXR decode/GPU 생성은 없었다.
이는 FPS·Editor bootstrap 시간 또는 cold/warm 평균 성능 수용을 뜻하지 않는다.
실제 모델 이동 카메라·tier별 GPU 성능 게이트는 MAT-9에 남긴다.

## 검증 및 재현

환경 shader 6개 entry를 각각 DXIL/SPIR-V로 검증했다. forest/autumn 7-map exact
GPU roundtrip 및 validation 0, 독립 CDF/PDF 검증을 통과했다.

CreatorEditor Debug 전체 최종 빌드 성공. Release raster/Scene 회귀는
21,067,374 checks, 3,889,591 GPU components, DXIL/SPIR-V 24 entries, validation 0,
정상 종료다. 공유 깊이·가림·skinning·texture·generation·abort·lookup 재사용 게이트를 유지했다.

실제 Debug Editor HTTP 조작은 11 checks, 정상 종료: default v3 bootstrap,
배경 토글 시 IBL generation 1→1, HDR 선택→새 v3 cook 영속화, 손상 cook 거부와
기존 환경 보존을 확인했다. runtime이 저장한 autumn v3 cook은 authoring cooker 파일과
전체 SHA가 동일하며 독립 CDF/PDF 검증도 통과했다.
이 실행에도 `[profiler] shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]`
종료 진단 한 건이 남았다. 이전 실행에서도 나온 미해결 진단으로, 정상 종료와 구분한다.
Vulkan shader 검증은 수행했지만 이 단계에서 Vulkan GUI 실행을 추가하지 않았다.
Dashboard Vite build 및 inline script 구문 검증, Python 구문 검사와 diff whitespace 검증 통과.

주요 로그: `Build/Obj/mat9-hdri-range-{shaders,autumn-final-cook,forest-final-cook,warm}.log`.
이미지 재현은 `rebind-environment-reference.py` → `measure-material-blender-images.ps1` →
`compare-material-blender-images.py --reference-repeat` 순서다.
독립 source/cube 적분은 NumPy가 있는 Python으로 `measure-environment-integrals.py`를 실행한다.

MAT-9 progress, 기존 완료 공수 32/34일을 유지한다. mirror·전체 HDRI 수렴/수용·special,
texture/normal-map·route parity·실제 씬 성능·원래 area-light gate는 기존 잔여 범위다.
