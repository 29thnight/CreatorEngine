# MAT-9 HDRI / BRDF MIS (2026-10-01)

후속 HDR 에너지 보존·CEIBL003 및 수렴한 제어 이미지 측정은
[MAT9HdriRange](MAT9HdriRange.md)를 따른다. 아래 CEIBL002 수치는 해당 시점의 이력이다.

## 구현

Scene의 GGX base/coat 및 Sheen convolution에 환경 휘도 분포와 BRDF 분포의
balance MIS를 연결했다. 각 분포 1,024개 표본을 사용한다(합계 2,048개).
32,768개 진단 표본을 제품 기본값으로 채택하지 않았다. 환경 PDF는 cube UV의
solid-angle Jacobian을 포함하며, 실제 radiance는 선형 필터의 원본 cube mip 0에서 읽는다.
분포의 셀 색으로 원본 radiance를 대체하지 않는다.

반사의 분모는 기존 prepared directional albedo와 같은 BRDF 적분이다.
환경 밝기에 따라 분모를 다시 추정하면 소비 시 적분의 정규화가 상쇄되지 않는다.
Sheen은 정규화된 LTC PDF로 적분한다. 거칠기 0의 거울 조회는 그대로다.
반사 적분 에너지가 **정확히 0**일 때 convolution을 건너뛴다. 이 최적화 전후
발광·거울·박막 세 이미지의 모든 float 출력 바이트가 같았다.

계산 방식의 근거: [PBRT 4e MIS / balance heuristic](https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency#MultipleImportanceSampling).

`SceneHostIdentity=6`; SceneLookup의 t27~29 및 cbuffer의 중요도 유효 플래그를 추가했다.
DX12/Vulkan 제품 호출부가 동일한 3개 맵을 전달한다. 이전 owner 재사용 조건에는
환경 generation과 3개 핸들을 포함하며, compute 전이·픽셀 SRV 복원·완료 fence 게시 규약을 유지한다.
Vulkan 호출부 연결과 SPIR-V 컴파일은 실행 검증과 구분한다.

## 쿠킹 / 캐시

`CEIBL002`는 기존 RGBA16F 조명 맵 4개와 RGBA32F 중요도 맵 3개를 저장한다.
512 cube에서 rows 512×3072, marginal 1×3072, cached samples 1024×2다.
런타임 HDR 생성은 행마다 한 번씩 훑는 compute kernel로 mip-0 CDF를 만들고,
완료 fence 뒤에 7개 GPU readback을 원자적으로 저장한다. 따뜻한 캐시는 7개 맵을
업로드하며 CDF 생성 shader/PSO를 준비하지 않는다. old CEIBL001 authoring 파일은
CPU 분포 파생으로 읽을 수 있으나, recipe v2는 새 파일로 재쿠킹한다.

forest / autumn을 제품 EnvironmentCooker로 재쿠킹하고, 모든 face/mip와 중요도 맵의
GPU 재업로드·readback 후 파일 전체가 byte-identical임을 확인했다.
기존 **4개 조명 맵도 각각 이전 cook과 byte-identical**이다.
CDF 단조성·유한 값·방향 정규화와 cached PDF / direction lookup PDF를 독립 검증했다.
최대 상대 PDF 차이는 forest 3.54e-7, autumn 3.62e-7이다.

| 항목 | 실측 / 배포 값 |
| --- | --- |
| default forest 파일 | `Resources/Environment/forest.ceibl`, CEIBL002 |
| 파일 크기 | 35,843,184 → 61,090,928 bytes (34.18 → 58.26 MiB) |
| recipe SHA-256 | `f11cadb4a011376fea68dc4b2a1120aa5dd1a4587e5c0b3f1f7460a6d0bfde9a` |
| forest SHA-256 | `ced62de8158fd344d97040a921757dadeb07a4197a678a1efb74560b1bfcbad6` |
| forest cold 7-map generation | 3,048.21ms, GPU validation ON |
| forest GPU upload/readback | 140.795ms, GPU validation ON |
| forest warm cache check | 311.144ms, disk read + checksum/identity validation, GPU generation 없음 |
| autumn cold generation | 1,546.31ms, validation OFF |
| autumn upload/readback | 66.2115ms, validation OFF |

검증 레이어 조건이 다르므로 forest / autumn 시간을 직접 비교하지 않는다.
`EnvironmentCooker` 숫자는 에디터 전체 시작 시간이나 steady-state FPS가 아니다.
기존 조명 pixels와 Blender 원본이 같으므로 새 `Build/Obj/Mat9Hdri-v2-*` reference는
Blender pixels를 재사용하고 cook metadata만 갱신했다. `cook_migration`에 old/new SHA,
4-map SHA와 재렌더하지 않았음을 기록했다. 원본 reference는 수정하지 않았고 기존 cook은
`Build/Obj/Mat9Hdri-frozen-cooks/<SHA>.ceibl`에 보존했다.

## 이미지 대조

동일한 scene-linear target(RMS ≤1%, p95 normalized ≤1%, max normalized ≤5%,
independent reference seed RMS ≤0.25%)을 유지했다. 같은 emission interior mask 정책이다.

| 사례 | 기존 BRDF만 | MIS | reference |
| --- | ---: | ---: | --- |
| autumn 박막 | 84.1182% | **3.6083%** | 131,072 samples, seed 0/11 차이 0.1587% |
| forest 박막 | 7.1833% | 0.8678% | 4,096 samples, seed 차이 0.6109%; 수용 불가 |
| autumn anisotropy | 70.4064% | 7.8616% | 4,096 samples, noise target 미달 |
| autumn Sheen | 34.1491% | 4.3516% | 4,096 samples, noise target 미달 |
| autumn white diffuse | 5.5984% | 5.5984% | 별도 irradiance/source/cube 오차가 남음 |

26장 전체 MIS 측정은 native GPU validation 0, 433,367 checks, 정상 종료.
현재 target 전체 수용은 **재질 3/20, 제어 3/6, accepted=false**다. 이는 7-map 배포 전
CEIBL001을 읽고 같은 mip-0 분포를 CPU 파생한 측정이다. 최종 CEIBL002 GPU 분포를 직접
소비하는 dense 3장도 native validation 0, 55,892 checks, 정상 종료했다.
박막 RMS 3.6083%, p95 2.4319%, max 6.2777%로 여전히 수용 목표를 넘는다.
Emission은 exact, 거울 RMS 0.5093%다. Shader snapshot이나 disabled MIS 결과를 전체 수용에 넣지 않는다.

- [전체 MIS 대조](MAT9HdriMisComparison.json)
- [전체 수용 판정](MAT9HdriMisAcceptance.json)
- [최종 v2 cook dense 대조](MAT9HdriMisDenseComparison.json)
- [쿠킹/PDF 검증](MAT9HdriMisCookVerification.json)
- [강제 재계산 비용](MAT9HdriMisTiming.json)

## 비용 / 한계

Release, 64×64, GPU validation OFF, 동일 mesh/view/재질에서 scene epoch만 바꿔
cache 재사용을 막았다. 각 조건 9회 중 repeat 1~8의 GPU timestamp 중앙값이다.
전체 frame query이며 MIS kernel만의 시간이나 실제 Scene FPS가 아니다.

| 사례 | MIS 전 | 최초 MIS | 0-energy skip 적용 후 |
| --- | ---: | ---: | ---: |
| 발광 제어 | 4.921ms | 10.921ms | 1.944ms |
| 거울 | 0.309ms | 0.309ms | 0.306ms |
| 박막 | 6.675ms | 8.496ms | 9.224ms |

박막 재계산의 증가 비용은 남아 있다. GPU clock/측정 실행 간 변동도 있어 위 숫자를
고해상도 모델 FPS 개선 또는 성능 수용으로 확장하지 않는다. CPU frame preparation 중앙값은
각 조건 0.35~0.47ms(최초 pipeline 준비 제외)였다. 이전 pixel owner가 같으면 convolution은
실행하지 않는다. 전체 Editor/model camera 조작 성능 게이트는 MAT-9 잔여다.

## 남은 순서

검증: CreatorEditor Debug 전체 빌드 및 최신 자원 배치 성공. Release raster/Scene 회귀
21,067,374 checks, GPU components 3,889,591, DXIL/SPIR-V 24 entries, validation 0.
cache baked 21,584 / reused 23,917, mutation·공유 깊이·skinned·texture·generation·abort 게이트 통과.
새 CDF compute kernel은 별도로 DXIL/SPIR-V 모두 VerifyFile 통과했다.
실제 Debug Editor HTTP 조작은 11 checks, 정상 종료: default 7-map bootstrap,
배경 토글 IBL generation 1→1, HDR 생성→v2 cache 영속화, 손상된 cook 거부와 이전 환경 보존.
런타임이 생성한 autumn cache도 7-map CDF/PDF 및 기존 조명 4-map byte equality 검증 통과.
이 실행의 stderr에는 `[profiler] shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]`
종료 진단 한 건이 있었다. 정상 종료와 구분해 기록하며 이 작업에서 원인을 규명하지 않았다.
Vulkan GUI 실행은 이번 검증 범위에 포함하지 않았다. Dashboard Vite build 성공.

1. source→cube / diffuse irradiance 오차와 forest 거울 오차 분리·개선.
2. 수렴한 HDRI reference로 동일 target 재수용(특히 박막 / anisotropy).
3. 기존 MAT-9의 special transport, texture/normal-map, route parity 및 실제 씬 성능.
4. 원래 MAT-0 disk area 조건은 light ABI/consumer 미지원 게이트로 유지.

MIS 구현·쿠킹 연결 완료와 MAT-9 이미지/성능 전체 수용을 구분한다.
기존 MAT-9 progress, 32/34일을 유지하며 완료 행·새 공수를 추가하지 않는다.
