# MAT-9 forest 거울 반사 정밀도

2026-10-01. [HDR 에너지 보존 작업](MAT9HdriRange.md)의 forest mirror 잔여를 수정했다.
고정 7조건은 모두 기존 목표를 통과했다. 전체 HDRI grid 및 MAT-9 완료 판정은 별도다.

## 원인과 구현

같은 원본 HDR·메시·카메라·344픽셀 emission interior를 사용했다.
원본을 Native normal/view에서 CPU로 직접 조회하면 RMS **1.2801%**, 공유 triangle에
픽셀 중심 ray를 교차시켜 조회하면 **0.4713%**다. 기존 cube를 소비한 Native는
**3.1285%**였다. 큐브의 footprint 평균·재투영·재필터링과 래스터 표면 보간을 분리했다.

- **원본 HDR 보존:** CEIBL004에 decoded linear Rec.709 RGBA32F 2D source를 추가했다.
  `IblSourceCopy`가 소스 mip 0을 그대로 보존한다. 거칠기 0의 base/coat 반사는
  이 source를 조회한다. 경도 U wrap, 위도 V clamp 및 float bilinear 4-fetch를 사용한다.
  선명한 HDR 반사에서 하드웨어 필터의 subtexel 정밀도 영향을 피한다.
- **픽셀 중심 보간:** Scene fragment에서 world position과 normal/tangent/bitangent/UV의
  fine derivatives가 만드는 같은 triangle plane을 사용한다. 그 plane의 동차 투영을
  실제 픽셀 중심에 맞춰 풀어 보간 값을 보정한다. 추가 index/vertex fetch는 없다.
  GBuffer·color·lookup·special fragment에 같은 계산을 적용한다.
- **제품 연결:** 8-map cook→cache→bootstrap→Scene t30 binding, owner/generation/reuse와
  graph 상태 전이를 연결했다. SceneHost identity는 8이다. CEIBL001/002/003은 원래
  배치로 읽으며, source가 없는 cook은 cube 조회를 사용한다.

공유 triangle을 이용한 독립 double ray 계산과 비교한 Native normal RMS는
8.6362e-5→**9.0021e-8**, view RMS는 1.4367e-5→**5.7863e-8**로 감소했다.
보정 후 CPU source 조회 RMS 0.4713%와 Native 0.4693%가 같은 수준이다.
[분리 측정 JSON](MAT9MirrorSampling.json)은 진단이며 이미지 수용 게이트를 대체하지 않는다.

## 고정 이미지 재대조

Blender 5.1.1 Cycles CPU, 64×64, 131072 samples, seed 0/11을 유지했다.
Blender는 원본 EXR/HDR을 읽는다. 원래 기준 이미지·입력·geometry를 변경하지 않고,
engine cook만 새 파일로 지정한 reference 사본에서 Native를 재촬영했다.
RMS ≤1%, p95 normalized ≤1%, max normalized ≤5%, reference seed RMS ≤0.25%를 유지했다.

| 조건 | v3 RMS | v4 RMS | p95 normalized | max normalized | 판정 |
| --- | ---: | ---: | ---: | ---: | --- |
| forest emission | 0% | 0% | 0% | 0% | 통과 |
| forest diffuse | 0.3574% | 0.3584% | 0.2985% | 0.6993% | 통과 |
| forest mirror | 3.1285% | **0.4693%** | 0.2811% | 3.5924% | 통과 |
| autumn thin film | 0.8607% | 0.8631% | 0.6150% | 1.2478% | 통과 |
| autumn emission | 0% | 0% | 0% | 0% | 통과 |
| autumn diffuse | 0.4107% | 0.4117% | 0.4681% | 0.6476% | 통과 |
| autumn mirror | 0.5531% | **0.3275%** | 0.1415% | 0.3734% | 통과 |

- [Native 비교와 기준 SHA](MAT9MirrorComparison.json), [contact sheet](MAT9MirrorComparison.png).
- [7/7 고정 target 판정](MAT9MirrorTargets.json): 전체 `accepted=false`를 유지한다.
- emission은 byte 일치. Native 121542 checks, GPU validation 0, 정상 종료.

## 쿠킹·캐시와 비용

| 항목 | 값 |
| --- | --- |
| recipe SHA | `31ea098d2c3f881500d8a0dba6c2cdabdaf4af110d2adca8dd026a54a219a8e2` |
| forest cook SHA | `fc0c61b3d72ad816585087d12ec0145a9bd0100ce3cf613a1fd18007f3af7ff7` |
| autumn cook SHA | `f2ce3a108c33c82755a7e82932aaa50c8a5a9e087e4f7ac680fe5048ce0b8613` |
| source | 두 환경 모두 1024×512 RGBA32F, 8 MiB |
| 파일 크기 | 94640240→103028856 bytes, 90.26→98.26 MiB |

기존 4 lighting map 및 3 CDF/sample map은 v3와 byte 일치한다.
두 환경의 8-map GPU upload/readback roundtrip은 전체 파일 SHA가 일치하며 validation 0이다.
[forest 검증](MAT9MirrorForestCook.json), [autumn 검증](MAT9MirrorAutumnCook.json).
forest source는 Blender에서 decoded한 원본 F32와 byte 일치한다.
autumn은 제품의 기존 DirectXTex HDR decoder를 유지한다. Blender HDR decode와 원본 RGB
norm 차이 0.2753%가 있어 두 decoder의 decoded byte 일치를 주장하지 않는다.
제품 decoder의 source는 GPU roundtrip에서 그대로 보존되며, rendered 비교는 위 표와 같다.

Resource와 Debug Editor 배포 forest의 SHA가 일치한다. 원본 EXR은 배포하지 않는다.
warm default cook read/checksum 한 실행은 473.68ms이며 EXR decode·GPU 생성은 없었다.
이는 Editor FPS나 평균 startup 성능 수용이 아니다. Source 메모리와 sharp reflection의
추가 조회, fragment 보정의 비용은 실제 모델 성능 게이트에서 측정해야 한다.

## 빌드·실제 실행

- Environment shader DXIL/SPIR-V 각각 7 entries 검증 성공.
- CreatorEditor Debug 전체 빌드 성공, 38-file shared runtime manifest
  `a16ad5c661aeacc71245e13f9699a8fee851b7eaedf401087ae1ab81dddc8277`.
- Release raster/Scene: 21067374 checks, 3889591 GPU components, 24 compiled entries,
  validation 0, 정상 종료. 공유 깊이·가림·skinning·texture·generation/abort·lookup reuse 통과.
  이 실행의 lookup 시간은 성능 수용에 사용하지 않는다.
- 실제 Debug Editor HTTP 조작: 독립 최종 실행 11 checks, 정상 종료. v4 default bootstrap,
  배경 토글 시 IBL generation 1→1, HDR 변경 후 v4 영속화, 손상 cook 거부·기존 환경 보존.
  runtime이 저장한 autumn v4 cook은 authoring 파일과 전체 SHA 일치.
- 기존 방향광/균일 환경 Core·Layered 이미지 24장 재검증: 400520 checks, validation 0.
  같은 고정 target 24/24, 상수 재질 20조건 RMS 최대 0.1792%, p95 최대 0.1958%,
  max normalized 최대 0.4487%. [회귀 대조](MAT9MirrorCoreComparison.json).
- Dashboard Vite 1.79s, inline script parse, Python 구문 및 diff whitespace 검사 통과.
- 기존 profiler 종료 진단 `shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]`는
  최종 Editor 실행에도 남았다. 정상 종료와 구분하며 이번 작업의 해결 항목으로 세지 않는다.
- Vulkan은 shader 검증 범위이며 이 단계에서 GUI runtime을 확인한 것은 아니다.

주요 로그는 `Build/Obj/mat9-mirror-*`, 이미지는 `Mat9Images-Release-hdri-v4-controls`,
최종 실제 Editor 실행은 `NeutralEnvironment/Debug-DX12-mirror-final`이다.

## 재현 및 기존 잔여

`rebind-environment-reference.py` → `measure-material-blender-images.ps1` 순서로 원래
source 기준 픽셀을 보존한 대조를 수행한다. `measure-mirror-sampling.py`는 NumPy 환경에서
같은 geometry의 픽셀 중심 ray와 Native normal/view를 분리한다.

forest mirror 잔여는 고정 조건에서 통과했다. 다음은 **전체 HDRI 26조건의 수렴·수용**이다.
기존 special transport·texture/normal-map·route parity·실제 모델 cold/warm/이동 카메라 성능,
원래 area-light ABI/consumer gate도 계획의 기존 범위로 남긴다.
새 공수 행은 추가하지 않으며 MAT-9 progress·기존 완료 공수 32/34일을 유지한다.
