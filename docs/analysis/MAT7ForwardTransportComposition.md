# MAT-7 alpha·SSS·굴절·Volume 공통 Forward+ 합성

## 범위와 구현

2026-10-02. Graph/Code의 공통 소비와 제품 편집·복구 다음에 남았던
특수 transport와 일반 alpha의 혼합 순서를 연결한다. 품질·전체 프레임 비용은 MAT-9,
native Vulkan 제품 수용은 PHASE 4.9가 소유한다. 아래 완료 게이트를 통과하여
**MAT-7 공통 재질 소비 통합을 완료**한다. PHASE 4.25 전체 완료로 확대하지 않는다.

- Surface alpha와 transmission을 같은 카메라 깊이 기준의 back-to-front Forward+ 순서에 넣는다.
  Code/Graph가 같은 타일 light list, opaque depth와 HDR을 소비한다.
- SSS·굴절 draw는 불투명 depth를 복사한 임시 GBuffer에서 자기 surface depth/owner를 만든다.
  Scene의 불투명 depth/owner는 보존한다. Core/Layered 일반 alpha는 추가 GBuffer를 만들지 않는다.
- SSS의 reflection/source/filter와 굴절의 capture/bake는 해당 draw의 순서 안에서 실행한다.
  각 굴절 draw 직전에 현재 HDR을 복사하여 이미 합성된 뒤쪽 Code/Graph alpha를 배경에 포함한다.
- 일반 alpha는 SRC_ALPHA/INV_SRC_ALPHA다. alpha=0 표면은 scratch depth를 기록하지 않으며
  최종 컬러도 discard한다. 물리 transmission을 쓴 Opaque/Masked surface는 authored alpha를
  일반 opacity로 해석하지 않고 coverage 검사 뒤 alpha=1로 합성한다.
- Volume은 불투명 배경에 한 번 적용한 뒤 각 Forward 표면에 카메라부터 해당 표면 깊이까지의
  `S + T * L`을 적용한다. 따라서 매질 앞의 표면을 뒤쪽 매질로 감쇠하거나 alpha 뒤 배경에
  Volume을 중복 적용하지 않는다. Code·Core/Layered·SSS·굴절은 같은 transport helper를 쓴다.
- 굴절 배경의 카메라 매질 응답은 hit 위치에서 제거하고 기존 굴절 ray 매질 응답을 적용한다.
  매질 없는 경로는 triangle count=0으로 반환한다.
- host 예약 자원은 `b5`, `t131..134`에 둔다. 저작용 Graph texture `t16..127`과 분리한다.
  Code reflection은 예약 이름/위치가 일치하는 두 host texture만 material table에서 제외한다.
- shader/binding 계약이 바뀌어 **SceneHost identity 12**로 재쿠킹한다. LXMC/artifact v4,
  BRDF 1024 / environment 4096 및 기존 물리 오차 상한은 유지한다.

## 검증 게이트

`Tools/regression/verify-material-forward-transport.ps1`는 DX12 Debug/Release에서 실제
RenderGraph·GBuffer·Forward+·SceneHost를 실행한다. Graph는 전체 Scene product의
바이너리 왕복 후 source-free generation으로 소비한다.

| 검사 | 기준 |
|---|---|
| Code/Core/SSS 혼합 | 단독 렌더에서 얻은 surface radiance와 독립 alpha recurrence 대조 |
| collection 역순/병렬 기록/카메라 정렬 역순 | GPU 합성 순서와 픽셀 대조 |
| opaque occlusion 및 alpha 0/1 | 불투명 depth/owner 보존, 배경 보존/앞쪽 surface 대조 |
| alpha/비alpha/여러 identity glass | IOR=1의 독립 straight-through 기준, 뒤쪽 alpha 포함 |
| Volume 앞·안·뒤 표면과 Code/Graph 혼합 | 독립 Beer-Lambert `exp(-density * interval)` 대조 |
| 모든 픽셀·GPU 상태 | finite HDR, WARNING 이상 0, Graph compile submissions 0 |
| SSS의 타일 overflow | 같은 총 irradiance를 가진 65개 광원과 단독 기준값 대조 |
| 실제 제품 | 새 identity의 Editor Scene/Game 및 재쿠킹·encrypted Player 소비 |

DX12 Debug/Release 혼합 검증은 각각 **24 frames, 독립 기준 2,268 RGB 성분,
30,073 checks, Graph compile submissions 0, GPU validation 0**을 통과했다.
Code/Core emission은 authored radiance와도 대조하여 빈 draw를 기준값으로 쓰지 않는다.
각 구성에서 소스 615개의 SHA-256 변동 0을 확인했다.
`Build/Obj/MaterialProductProbe/transport-gate-final.log`, `forward-transport-{Debug,Release}.log`.
현재 추가 회귀 결과:

| 경로 | 실행 결과 |
|---|---|
| DX12 Debug 기존 굴절 | 24 frames, 198,940 checks, GPU 169,701 성분, validation 0 |
| DX12 Debug 기존 SSS | 12 frames, 119,415 checks, GPU 108,072 성분, validation 0 |
| DX12 Debug/Release 기존 Volume | 각각 33 frames, 8,448 pixels, 36,065 checks, GPU 58,812 성분, validation 0, 소스 615개 변동 0 |
| 실제 Editor Debug/Release | 각 11 captures, checks 1,465/1,464, 종료 0, 소스 1,256개 변동 0 |
| 실제 encrypted Player Debug/Release | 각 3 programs, 1 material, 120 promotions, Scene compile 0, text parser 0, payload 보존, 소스 642개 변동 0 |

기존 Volume 회귀의 Surface+Volume 조건에서 카메라 ray가 뒤집히는 결함을 발견했다.
far homogeneous 지점이 카메라 뒤로 복원되는 projection에서도 가시 배경은 앞에 있었다.
기존 composite가 `normalize(far - near)`를 쓰면서 해당 배경까지의 limit를 0으로 잘라
감쇠를 누락했다. 가시 endpoint에서 방향/거리를 얻고 무한 endpoint는 중간 clip ray와
무한 transport limit를 사용하도록 수정했다. 카메라 합성·굴절 배경·굴절 결과를 각각
독립 적분 기준과 다시 대조했다. 순차/1 worker/4 workers의 Surface+Volume 조건을 포함한
전체 33프레임을 Debug/Release에서 통과했다. 허용 오차는 기존 1.5e-3을 유지했다.
`transport-regression-volume-{Debug,Release}.log`, `transport-volume-source-hashes.json`.
배경은 Forward 합성 전에 읽어 독립 기준으로 보존하고, 실제 굴절 배경과 굴절 sample도
별도로 검사하여 감쇠 누락이 최종 합성에서 상쇄돼 가려지지 않게 한다.

실제 Editor 검증은 HTTP로 값 변경·Undo/Redo·거절·Reload·재import·Save/Apply·Scene/Game·
Play/Stop·삭제/복구·재시작을 수행하고 실제 HDR 픽셀과 owner를 검사한다. private project는
사용자 프로젝트의 UI 배율 1.5를 상속한다. Debug 첫 프레임 준비에 76,656 ms가 걸렸다.
최초 준비만 별도 bounded warmup으로 기다리고 후속 capture의 fence/픽셀 기준은 유지한다.
최초 Debug 시도는 120초 fence timeout으로 중단돼 성공 증거에 포함하지 않았다.
Player의 기존 180초 smoke timeout을 넘긴 cold GBV 실행도 성공으로 세지 않았고,
전용 검증만 600초 상한으로 다시 실행했다. 제품 timeout은 변경하지 않았다.
이 시간은 GPU validation이 켜진 정확도 검증이며 loading/FPS 성능 통과가 아니다.
제품 편집/패키지 회귀 뒤의 Volume composite ray 수정은 Volume 조건의 위 실행으로 검증했다.
Core 제품 경로·생성 ABI·C++ runtime은 그 수정으로 바뀌지 않았다. 최종 소스로
CreatorEditor·Player·AssetCooker와 probe를 Debug/Release 모두 다시 빌드했다.

## 지원 한계와 후속 소유

정렬은 object/draw 단위다. 교차 표면·self-overlap의 triangle sorting/OIT는 지원하지 않는다.
굴절은 화면 공간의 한 장으로 합성된 뒤쪽 컬러와 불투명 depth를 사용한다. 배경의 반투명
층별 깊이를 저장하거나 여러 투명 교차점을 ray tracing하는 방식이 아니다. 화면 밖 miss는
기존 environment fallback을 따른다. 강한 매질의 역변환은 `T >= 1e-4`로 제한한다.
이 근사의 색 오차와 전체 transport 비용은 MAT-9에서 판정한다.

닫힌 균질 Volume의 기존 16 objects / 128 triangles / 64 transport lights 상한은 유지한다.
표면의 직접광은 공통 Forward+ light list와 32개 tile overflow fallback을 소비한다.
SSS·굴절의 scratch 5 MRT + D32는 픽셀당 40 byte이며 해당 표면이 있을 때 한 번 생성해 재사용한다.
Volume helper가 추가된 shader의 최초 PSO 준비와 활성/비활성 Volume의 GPU·VRAM 비용은
MAT-9 cold/warm 및 전체 프레임 비용 게이트에서 각각 측정해야 한다.

SSAO/SSR/SSGI 전체 품질 수용, Blender parity, 근접 모델 카메라 이동 비용과
GPU-driven 이후 비용 게이트는 MAT-9/해당 renderer phase에 남긴다. 이번 연결을 그 게이트의
완료나 FPS 개선으로 세지 않는다. PHASE 4.25는 열린 상태를 유지한다.
