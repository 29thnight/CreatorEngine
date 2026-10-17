# Environment 배경·IBL 설정 (PHASE 4.75 RND-ENV)

**2026-10-01 사용자 결정:** 이 페이즈의 실행·픽셀·성능 완료 판정은 DX12 Debug/Release다. RHI 중립 계약과 필요한 backend 구현은 유지한다. Vulkan 실행 비교·동등성·교차 픽셀 수용은 [PHASE 4.9](BackendParityPlan.md)의 RenderDoc 캡처 → 리소스 확인 → 픽셀별 비교가 단독 소유하며 이 페이즈의 선행·잔여·실패 조건으로 사용하지 않는다.


결정: 2026-10-01. 상태: 배경 토글 슬라이스 적용, 나머지 제품 환경 계약 미구현. 잔여 계획 추정 10인일(ENV-A 1, B 2, C 3, D 2, E 2).
상위 UI/설정 소유권은 [EditorRenderingSurfacesPlan.md](EditorRenderingSurfacesPlan.md),
쿠킹 기반은 [MAT9ThinFilmAndEnvironment.md](../analysis/MAT9ThinFilmAndEnvironment.md)를 따른다.

## 1. 설정 의미

도입할 SceneRenderProfile의 Environment schema에는 환경 참조, showBackground/backgroundIntensity/backgroundColor,
diffuseIBL.enabled/intensity, specularIBL.enabled/intensity를 별개 값으로 정의한다.
배경 off + IBL on, 배경 on + IBL off, 확산/정반사 개별 off가 모두 유효하다.
배경 off는 명시적 linear backgroundColor를 쓰고, IBL off는 직접광/방출/Scene 오브젝트의 가림을 바꾸지 않는다.
색/세기/회전의 작업 색공간·노출 전 의미와 허용 범위·기본값은 schema에서 단일 정의한다.

현재 적용: 기존 RenderPassSettings.m_isSkyboxEnabled를 GT frame packet에 밀봉하고
양 backend의 SkyBox 표시 모드로 소비한다. Scene Show > SkyBox는 view-only HideSkyBox다.
둘 다 cached cube/irradiance/prefilter/BRDF LUT를 재생성하지 않는다. off에서도 같은 깊이 읽기 전용
패스가 linear RGB=(0.18,0.18,0.18)의 중립 회색 배경을 채운다. 일반 Scene post chain을 거치므로
표시 밝기는 노출·톤맵·비네트의 영향을 받는다. 기본 Grid의 선/셀 색은 중립 회색이며 원점/축에
별도 색을 넣지 않는다. fallback 색/배경 세기의 편집 UI 및 diffuse/specular 분리 UI는 아직 없다.

현재 bootstrap은 Resource의 `forest.ceibl`을 설치하고 배경 off + IBL on으로 시작한다.
이 실제 선택에 맞춰 RuntimeSettings의 환경 이름/배경 bool도 함께 갱신한다. 기존 저장 경로를
bootstrap에 적용하는 제품 profile/resolver는 ENV-B 범위다. 개발자의 명시적 HDR/ceibl 선택이
검증을 통과하면 Core에서 source와 background on을 함께 발행한다. Scene 드래그/HTTP 선택은
Scene Show의 hide override도 해제한다. 명시적으로 forest를 다시 고른 경우도 on이 된다.
off 기본값을 매 프레임 강제하지 않으며, 수동 토글은 다음 명시적 환경 선택까지 유지한다.
경로 또는 cooked 파일 검증 실패는 source/배경 bool/Scene hide 상태를 바꾸지 않는다.
SkyBox는 씬 깊이를 읽기만 하며, SSGI는 Hi-Z의 축소 셀에서 hit가 난 뒤 실제 full-resolution
GBuffer normal의 표면 존재도 확인한다. 하늘 픽셀을 표면 radiance로 모으지 않아 배경 표시가
메시 간접광을 바꾸지 않는다. 제품 캡처 검증은
[EditorRenderingSurfacesValidation.md](../analysis/EditorRenderingSurfacesValidation.md)에 기록한다.

## 2. 구현 순서와 완료 조건

| 단계 | 구현 | 완료 게이트 |
|---|---|---|
| ENV-A | 기존 배경 토글 및 view-only 경로, 회색 fallback·기본 forest 숨김·명시적 선택 시 표시 | Scene/Game off/on capture, IBL generation 불변, mesh 조명·가림 불변. default/custom/실패 선택 정책 검증. Profile 저장 왕복은 ENV-B에서 완료 |
| ENV-B | reflected Environment schema·GUID/manifest resolver·default profile/Scene override | Inspector/프로젝트 Graphics/Player가 동일 설정을 소비. 기존 profile 전환·해제·실패 복원, unknown key 및 경로 진단 |
| ENV-C | Standard Deferred/Forward와 LX Core/Layered/Special의 확산/정반사 별도 weight, background 색/세기 | 각 renderer route에서 diffuse-only/specular-only/off/둘 다 on·0/1/변경 세기를 고정 HDR 픽셀로 비교. direct-only/emission/coverage는 불변 |
| ENV-D | 쿠킹·캐시·설정 변경 비용 | 토글/세기만 바꾸면 bake/PSO 재컴파일·환경 생성 0회. HDRI 참조/recipe 변경만 cache invalidation. cold/warm·overwrite/corrupt·마지막 정상 유지 |
| ENV-E | 제품/멀티뷰/성능 수용 | Editor Scene override가 Game/Player에 누출되지 않음. DX12 Debug/Release 실제 package, 최초 준비·동일 장면 GPU/CPU 비용 기록 |

ENV-C는 쉐이더 상수 ABI/생성 Slang/Scene host cache key와 IBL lobe를 함께 바꿔야 한다.
하나의 대표 Pass에만 가중치를 넣고 전체 renderer 지원으로 세지 않는다. 새 weighted 결과 때문에
쿠킹된 원본 환경 맵을 중복 저장하지 않고 소비 쪽에서 평가한다.

선행: MAT-7의 제품/쿠킹 기반. RND-1 probe와 전역 환경의 blend/우선순위를 공유하며,
W9 default profile 선택과 같은 resolver를 쓴다. RND-2 shadow와 독립이다. 미구현 weight를
UI에 동작하는 스위치로 먼저 노출하지 않는다. 대시보드는 RND-ENV를 progress/days:10로 추적한다.
