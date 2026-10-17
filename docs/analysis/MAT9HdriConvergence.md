# MAT-9 전체 HDRI 26조건 수렴·수용 — 2026-10-01

## 판정

**고정 HDRI 범위는 26/26 통과했다.** 재질 20조건, 제어 6조건이다.
기존 target을 유지했고 진단 shader snapshot은 제품 수용에 사용하지 않았다.
MAT-9 전체 및 PHASE 4.25 완료 판정은 아니다.

| 항목 | 고정 상한 | 제품 측정 최대 |
|---|---:|---:|
| 상대 RMS | 1% | 0.495878% |
| p95 normalized | 1% | 0.468087% |
| max normalized | 5% | 3.592395% |
| 독립 시드 상대 RMS | 0.25% | 0.171682% |

[수용 판정](MAT9HdriConvergenceTargets.json),
[전체 측정](MAT9HdriConvergenceComparison.json),
[이미지 대조](MAT9HdriConvergenceComparison.png).

## 같은 조건에서의 개선

| 조건 | 이전 제품 RMS | 현재 제품 RMS |
|---|---:|---:|
| forest 이방성 | 1.0659% | 0.4774% |
| autumn 금속 | 2.3418% | 0.4959% |
| autumn 이방성 | 1.6345% | 0.2148% |

이전 4096-sample reference의 노이즈가 재질 오차와 섞였다. 이번에는 전체 조건을
Blender 5.1.1 Cycles CPU 131072 samples, seed 0/11로 각각 새로 렌더했다.
autumn 금속의 시드 RMS는 0.0542%로, 이전 제품 오차를 설명하지 못했다.

이전 Native 픽셀은 최초 캡처대로 보존했다. 그 캡처 당시 reference의 samples는 4096이지만
Native가 소비하는 geometry·재질 입력·HDRI source/cook·strength가 새 고샘플 reference와
같음을 확인한 뒤 새 reference 픽셀로 이전 오차를 계산했다.
이 계산은 현재 제품 수용에 사용하지 않는다.
[이전 픽셀의 고샘플 대조](MAT9HdriConvergenceBaseline.json).
현재 수용은 새 reference manifest를 그대로 소비한 **별도 제품 Native 실행**이다.

## 제품 변경

- `CEIBL005`는 기존 1024 방향/PDF bank 뒤에 4096 반사 bank를 저장한다.
  두 bank는 쿠킹·GPU 업로드·파일 재저장·bootstrap/선택 캐시에 포함된다.
- Scene 반사는 **GGX 1024 + environment 4096**의 서로 다른 수를 MIS 가중에 반영한다.
  prepared directional albedo 및 정규화의 GGX 1024 순서는 유지한다.
- 확산 및 sheen proposal은 원래 1024 bank를 쓴다. Scene 반사의 radiance는
  retained source RGBA32F를 float bilinear wrap-U/clamp-V로 조회한다.
  큐브 변환을 통한 추가 angular resampling과 반사 샘플 분산을 구분했다.
- mirror source 경로 및 픽셀 중심 normal/view 보정은 유지한다.
  전체 4096/4096 적분, 1024-only Source/MIS 가중/VNDF 진단을 제품 기본값으로 채택하지 않았다.
- reader는 CEIBL001~004를 계속 읽는다. Scene은 기존 1024 및 새 5120 폭을 검증하며
  이전 bank에서는 원래 sample count로 동작한다. source-bearing v4 GPU capture는 v4로 재저장된다.

기본 forest와 autumn 파일은 각각 **103159928 bytes / 98.38 MiB**이며 v4 대비 **128 KiB** 증가했다.
source 8 MiB 및 맵 수 8개는 유지한다. 기본 forest와 Debug 배포본의 전체 SHA는
`7b4e9ad53d806195b2fdf4af2cb0ac39dc2709524028eb2707d5dd3d9a631acb`로 같다.
recipe SHA는 `c40f0bc775d49d61f81ae29259aca9973b58e6a3dc8da109179b8b97a98b691a`다.
원본 EXR은 bootstrap에서 읽지 않는다.

[forest 쿠킹 검증](MAT9HdriConvergenceForestCook.json),
[autumn 쿠킹 검증](MAT9HdriConvergenceAutumnCook.json).
두 환경 모두 5120 방향/PDF가 유효하고 lookup PDF와 최대 상대 차이는 4.35e-7 미만이다.
두 환경의 8-map GPU upload/readback roundtrip은 각 새 쿠킹 파일과 전체 SHA가 같다.

v4와 비교해 source·environment cube·irradiance·BRDF·CDF rows/marginal·첫 1024 bank는 byte 일치한다.
prefilter mip 4에는 작은 float 차이가 있으므로 네 lighting map 전체의 byte 일치를 주장하지 않는다.
forest prefilter 전체 상대 L2 차이는 6.185e-8, 최대 절대 차이 0.00060463이며 autumn은
3.630e-10 / 0.000095367이다. strict lighting byte-equality 진단은 미통과로 기록하며
픽셀 수용 상한을 변경하지 않았다. [전체 맵 변화 기록](MAT9HdriConvergenceCookDelta.json).

## 검증 범위

- original forest EXR 및 autumn HDR, strength 0.35, Raw scene-linear Rec.709.
- 같은 triangle soup의 64×64 sphere, eye (0,0,3), FOV π/4, Gaussian filter 0.01,
  max bounce 1. 직접 조명은 없다.
- 원래 고정된 Core/Layered 10개 입력 × 두 환경 + emission/white/mirror 제어 × 두 환경.
- 공통 emission coverage를 2 pixels erode한 344 interior pixels를 모든 재질에 사용한다.
  재질별 mask·입력·target 변경은 없다. 현재 capture manifest와 reference는 정확히 같다.
- Release Native: **433369 checks, validation 0, 정상 종료**, 전체 26조건 통과.
- 기존 방향광·균일 환경 24조건 회귀도 **24/24 통과**했다.
  Release Native **400520 checks, validation 0, 정상 종료**이며 최대 RMS 0.179170%,
  p95 0.195771%, max normalized 0.448656%다. 독립 시드 RMS 최대는 0.223588%다.
  [회귀 판정](MAT9HdriConvergenceCoreTargets.json),
  [회귀 측정](MAT9HdriConvergenceCoreComparison.json),
  [회귀 이미지](MAT9HdriConvergenceCoreComparison.png).
- Debug Editor 전체 빌드와 38-file shared runtime manifest:
  `833e1b42210a8d9e73fe7c344671387bfd3e5d187b42878f0931bbe0a21854f3`.
- 실제 Debug Editor HTTP **11 checks, 정상 종료**: v5 default bootstrap,
  배경 토글 IBL generation 1→1, HDR 변경 후 v5 캐싱, 손상 파일 거부·기존 환경 보존.
  runtime autumn v5는 authoring 파일과 SHA
  `63a59a27284f0a4ba0c6aa0cc17b7fb5d82d58ef0d1ad5bd040ec8828a168f58`가 같다.
- shader backend verification은 DXIL/SPIR-V 범위이며 Vulkan GUI runtime 수용은 아니다.
- 기존 profiler 종료 진단 `shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]`는 남았다.
  정상 종료와 구분하며 해결 항목으로 세지 않는다.
- 계획·대시보드 반영 뒤 웹 빌드, inline script 구문, 변경 Python 도구 구문,
  `git diff --check`를 통과했다. 기존 줄바꿈 변환 경고는 수용 실패로 세지 않는다.

주요 실행: `Build/Obj/Mat9Images-Release-hdri-v5-full-converged`,
`NeutralEnvironment/Debug-DX12-hdri-v5-converged`, `mat9-hdri-v5-*` 로그.
reference: `Mat9Hdri-v4-dense-{0,11}`의 source 렌더 픽셀을 보존하고
`rebind-environment-reference.py`로 새 engine cook만 연결한 `Mat9Hdri-v5-dense-{0,11}`.

## 기존 잔여 및 성능 한계

환경광의 반사 leg는 1024→4096이고 source 조회도 추가 비용이 있다.
cached 방향을 쓰므로 매 픽셀 CDF 역탐색은 하지 않지만, **최초 준비·이동 카메라·cache miss 비용의
성능 수용은 아직 별도 게이트**다. 이번 quality 실행과 CPU reference 렌더는 FPS 수용 근거가 아니다.
default warm load는 원본 decode 및 GPU 환경 생성 없이 cooked resource를 올린다.

현재 수용은 상수 Core/Layered의 두 HDRI 범위다. 기존 special transport,
texture/normal-map filtering, Deferred/Forward route parity, 실제 모델의 cold/warm/이동 카메라 및
tier별 성능, 원래 grid의 area-light ABI/consumer 게이트는 계획에 그대로 남긴다.
MAT-9 progress·기존 완료 공수 32/34일을 유지하고 새 공수 행을 추가하지 않는다.
