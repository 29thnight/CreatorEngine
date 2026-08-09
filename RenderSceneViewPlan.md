# Light·Camera 관리 재편 — RenderScene(보유) × RenderView(프레임) 이층 구조

작성: 2026-08-09 · 계기: "Unreal은 RenderScene 중심, Unity SRP는 View/CullingResults
중심 — 우리는 둘을 결합해야 하지 않나"라는 질문.

---

## 1. 판단 — 그 구도는 맞고, "결합"은 절충이 아니라 정석이다

| | Unreal | Unity SRP |
|---|---|---|
| API 표면 | `FScene`(보유 상태) + `FSceneRenderer`/`FViewInfo`(프레임) 둘 다 노출 | `CullingResults`(뷰별 가시 목록)만 노출, 보유 씬은 네이티브 내부에 은닉 |
| 등록 모델 | 프리미티브·라이트가 `AddToScene`/`RemoveFromScene`으로 **등록/해제** | C# 쪽에서는 안 보이지만 내부 씬도 등록 기반 |
| 프레임 단위 | 뷰마다 가시성 계산 → `FViewInfo`가 가시 목록·뷰 상수 소유 | `Cull()` 호출마다 `CullingResults` 생성 |

즉 **두 엔진 다 내부적으로는 같은 이층 구조다** — 영속 보유층(등록 기반
씬 표현) 위에 프레임마다 뷰별 가시 결과를 뽑는다. 차이는 어느 층을 API로
노출하느냐뿐이다. 그러므로 "둘을 결합"은 두 유파의 절충이 아니라, 둘 다
쓰고 있는 표준 구조를 우리도 완성한다는 뜻이다.

★ 그리고 우리는 이미 반반씩 갖고 있다. 메시는 Unreal식 보유층이 완성돼
있고(GUID 키 프록시 맵 + `ProxyCommandQueue` 증분 등록), 카메라는
`CameraView`(MultiCameraRenderPlan)가 뷰별 표시 슬롯·시간축 패스를 이미
소유한다. **빠진 것은 라이트의 보유층 편입과 뷰의 가시 결과(CullingResults)
두 조각이다.**

## 2. 지금 무엇이 일어나는가 — 실측 (2026-08-09)

### 2.1 라이트 — 보유층 밖에서 매 프레임 재수집

| | |
|---|---|
| 저장소 | `Scene::m_lights`(값 벡터) + `m_lightComponents`(부기용, 소비자 없음) — `Scene.h:334,338` |
| 갱신 | `LightComponent::Update`가 **매 프레임** `GetLight(index)`로 되찾아 덮어씀 — `LightComponent.h:43-47` |
| 수집 | `Scene::UpdateLight`가 매 프레임 `memset(255*sizeof)` + 전체 선형 순회 — `Scene.cpp:1279-1293` |
| 변환 | `CaptureFromCamera`가 **카메라마다** 다시 `EnhancedLight`로 재변환 — `EnhancedSceneRendererLive.cpp:1632-1652` |
| 죽은 코드 | `LightController::AddLight`·`SetLightWithShadows` 호출자 0 — `LightController.cpp:44-58,72-106` |

메시가 GUID 키 증분 프록시인 것과 대조적으로, 라이트만 매 프레임 전체
재구축이다. N≤255라 비용 자체는 작다 — 문제는 비용이 아니라 **단일 진실이
없다는 것**이다. 그 증거가 패스마다 제각각인 상한과 세기 이중 곱이다:

- 상한 불일치: Deferred 64(초과분을 조용히 드롭, `EnhancedDeferredPass.h:27`)
  vs VolumetricFog 20 vs Forward+ 타일당 32. **잘리는 기준이 "가깝거나 밝은
  것"이 아니라 등록 순서다** — 그것이 문제이지 수가 작은 것이 문제가 아니다.
  (DX11 시절의 전역 255(`MAX_LIGHTS`)와 `LightProperties` cbuffer는 소비자가
  0이라 ①에서 함께 걷었다. 한도는 소비하는 패스가 정한다.)
- 세기 제곱: `ApplyLightData`가 `color *= intensity`를 하고 라이브가
  `color.w = intensity`를 또 실어 셰이더가 `rgb * a`를 한다
  (MultiCameraRenderPlan §15에 기록). 변환 지점이 흩어져 있어 생긴 부류다.

### 2.2 컬링 — 계산은 있는데 소비자가 0

| | |
|---|---|
| 게임 스레드 | `Scene::CullMeshData`(매 프레임, `Scene.cpp:455-627`)가 컴포넌트 리스트 값 복사 → 옥트리 컬 → 결과 **버림** → 카메라별 전수 AABB 테스트 → `PushCullData` |
| 소비자 | `GetCullDataBuffer`/`GetShadowRenderDataBuffer` — **읽는 코드 0건**(grep 확인, 선언·정의뿐) |
| 실제 드로우 | `CaptureFromCamera`가 프록시 스냅샷 **전체**를 무필터로 큐잉 — `EnhancedSceneRendererLive.cpp:1570-1591` |

즉 컬링 비용(O(카메라 × 전체 메시), 스레드풀 태스크 포함)은 매 프레임
내면서 그 결과로 드로우 하나 안 줄인다. **낭비 두 겹** — 계산도 버리고,
드로우도 다 낸다.

### 2.3 카메라 — 고정 슬롯과 하드코딩

| | |
|---|---|
| 컨테이너 | `CameraContainer : Singleton`, 고정 10슬롯 — `Camera.h:146-247` |
| 배정 | **모든** `CameraComponent::Awake`가 `GetCamera(1)` 하드코딩 — `CameraComponent.h:43` → 다중 게임 카메라 원천 불가 |
| 뷰 | `kMaxLiveCameraViews = 2`(씬뷰+게임뷰), 초과분 무시 |
| ABA | 카메라 파괴 후 같은 주소 재할당 시 뷰 오인 계승(MultiCameraRenderPlan §14 후속 과제) |

### 2.4 이미 맞게 되어 있는 것 (건드리지 않는다)

- 메시 프록시: GUID 키 맵 + 3-프레임 링 커맨드 큐 + 스냅샷 밀봉 —
  `RenderSceneBridge.cpp:49-61`, `ProxyCommandQueue.h`
- 카메라 스냅샷: 뷰별 이중 버퍼 publish/latch — `RenderPassData.h:59-151`
- 뷰별 시간축 패스: SSGI·Fog가 `CameraView` 멤버(잔상 회귀로 확립된 규약)
- 투명 정렬: 카메라를 아는 밀봉 단계에서 백투프론트

## 3. 설계 — 두 층의 책임 경계

```
[보유층 — RenderScene]                    등록/해제로만 변한다
  m_proxyMap        프리미티브 프록시 (있음)
  m_lightMap        라이트 프록시     (신설 · ①)
  가속 구조         옥트리 등          (보류 · §6)

[프레임층 — RenderView]                   매 프레임 밀봉된다
  cameraSnapshot    행렬·프러스텀      (있음 · FrameCameraSnapshot)
  visibleDraws      뷰 프러스텀을 통과한 드로우  (신설 · ③)
  visibleLights     뷰에 닿는 라이트, 우선순위 정렬  (신설 · ②)
  시간축 패스        SSGI·Fog          (있음 · CameraView)
  표시 슬롯          slots[3]           (있음 · CameraView)
```

경계 규약 셋:

1. **보유층은 카메라를 모른다.** RenderScene에 "어느 뷰에서 보이는가"류의
   상태를 두지 않는다. 가시성은 전부 프레임층 산물이다.
2. **패스는 뷰만 본다.** 패스가 `Scene`·`LightController`·전역 싱글턴을
   직접 뒤지지 않고, `frameContext`(= RenderView의 조각들)로 받은 것만
   소비한다. 지금도 대체로 그렇다 — 이 규약을 라이트까지 확장하는 것이다.
3. **상한은 뷰가 한 번 정한다.** "어떤 라이트를 몇 개까지"는 visibleLights를
   만드는 자리에서 한 번 결정하고, 패스는 받은 목록을 자르지 않는다.
   패스별 상한 4종이 제각각 드롭하는 지금 구조를 이 규약이 없앤다.

★ **RenderView는 새 클래스가 아니라 `CameraView`의 승격이다.** 표시 슬롯
묶음이던 것에 가시 결과를 더해 "렌더링의 단위"로 만든다. frameContext가
`draws`/`lights` 벡터의 주소를 드는 현행 제약(MultiCameraRenderPlan §6 —
뷰1 제출 후 뷰2 밀봉 순서 강제)도, 뷰가 자기 벡터를 소유하면 자연 소멸한다.

## 4. 순서 — 네 슬라이스

각 슬라이스는 독립적으로 그림이 같거나 좋아져야 하고, 빌드가 깨지는 중간
상태를 만들지 않는다.

### ① 라이트를 보유층으로 (단일 진실) — 구현 완료 2026-08-09 · Release(유니티) 그린

프록시 타입 분리와 한 몸으로 넣었다. 광원을 보유층에 들이려면 "프록시"가
메시 전용 클래스가 아니어야 했기 때문이다 — 실제로 그 클래스에는
`//아 각 타입별로 분리하고 싶다...`가 붙어 있었다.

**계층.** 공통을 두 층으로 갈랐다.

```
RenderProxy                     신원 · 월드 변환 · DestroyProxy(순수 가상)
├─ PrimitiveRenderProxy         그리는 것의 공통 + 타입 태그 + As<T>()
│   ├─ MeshRenderProxy          재질 · 메시 · 애니메이션 · 그림자 플래그
│   ├─ TerrainRenderProxy       지형 메시 · 지형 재질 · 버퍼
│   ├─ FoliageRenderProxy       인스턴스 · 타입 · 색인
│   ├─ DecalRenderProxy         텍스처 셋 · 슬라이스
│   └─ SpriteRenderProxy        쿼드 · 스프라이트 · 빌보드 · 커스텀 PSO
└─ LightRenderProxy             색 · 세기 · 감쇠 · 타입 (신설)
```

`As<T>()`는 타입 태그가 맞을 때만 그 타입 포인터를 준다. dynamic_cast가
아니라 태그를 쓰는 이유가 하나 더 있다 — `DestroyProxy`가 태그를 Expired로
바꾸므로 **파괴 통보된 프록시는 자동으로 널이 되고**, 회수 전에 도착한
갱신 커맨드가 죽은 프록시에 값을 쓰지 못한다.

효과는 `PrimitiveProxyType::`을 쓰는 자리가 프록시 헤더·cpp 안으로
모였다는 것이다(이전에는 소비자 다섯 곳이 태그를 물은 뒤 필드에 손댔다).

★ **`PrimitiveRenderProxy`는 직접 만들 수 없다**(생성자가 protected, 기본
생성자 없음). 태그로 내려보는 설계라 기반 클래스를 그대로 인스턴스화하면
태그와 실제 타입이 어긋나 `As<T>()`가 거짓을 말한다. 태그 기본값도
`Expired`로 뒀다 — 어느 경로로든 설정이 빠지면 아무 타입으로도 안 보이는
쪽이 틀린 타입으로 보이는 것보다 낫다. (리뷰에서 잡힌 자리다.)

**저장소.** 광원은 `m_lightProxyMap`으로 프리미티브와 나눴다 — 소비자가
겹치지 않으므로 순회에서 타입을 거를 일이 없다. 회수 큐
(`RegisteredDestroyLightProxyGUIDs`)와 락도 따로 둔다.

**세기 이중 곱을 닫았다.** 프록시가 **저작 색과 세기를 따로** 들고,
`MakeEnhancedLight`가 `color.rgb=색 · color.a=세기`로 담는다. 곱은
셰이더의 `rgb*a` 한 번뿐이다. 셰이더는 건드리지 않았다 — 변환 지점을
하나로 모으는 것만으로 닫히는 부류였다.

★ **모든 씬의 밝기가 바뀐다.** 세기 1.6이면 이전이 2.56배였다. 전후
캡처를 남길 것.

**함께 걷은 것.** `LightController`(광원 배열 보관소 — 소비자 0이 됐다),
`Scene::UpdateLight`(매 프레임 전체 재수집), `ProxyFilter`(호출자 0),
그리고 DX11 상수 버퍼 형상 넷 — `MAX_LIGHTS`(255) · `LightProperties` ·
`LightCount` · `cameraView`. 광원 배열을 통째로 cbuffer에 싣던 경로가
사라진 뒤로 소비자가 없었는데 "255개까지 지원"이라는 인상만 남아 있었다.

**남은 것.** `Scene::m_lights`와 `LightComponent::m_lightIndex`는 남는다 —
기즈모가 "메인 라이트"를 가리는 편집기 부기라 렌더 경로와 무관하다.
`ApplyLightData`도 `Awake`에서 한 번만 부른다(매 프레임 호출은 걷었다).

**빌드.** `Release|x64`(유니티 켬) 7개 라이브러리 **오류 0**.
비유니티 `Debug|x64`는 이 변경이 고친 것 하나(`RenderSceneBridge.cpp`가
`Skeleton::MAX_BONES`를 쓰면서 `Skeleton.h`를 직접 include하지 않고
있었다 — 헤더 정리로 전이 경로가 끊겨 드러났다) 외에, **이 변경 밖의
두 가지에 막혀 완주하지 못했다**:

| 막은 것 | 성격 |
|---|---|
| `Interfaces/FoliageType.h`가 `Model.h` include를 걷어냄 | **다른 작업의 미커밋 변경**. 그것을 타고 오던 `Mesh`·`Vertex`가 끊겨 `EnhancedSceneRenderer.cpp`·`Mesh.cpp`·`DirectXHelper.h`가 깨진다 |
| DX12 `*Test.cpp` 셋이 `RHIEncoder.h`를 include하지 않음 | 유니티에서만 이웃 TU 덕에 통과하던 자급자족 결함 |

★ **후자가 이 변경 탓이 아님을 어떻게 확인했나.** `EnhancedPostChainTest.cpp`의
include 폐포(25개)를 직접 계산해 ⑴ 이 변경이 만든·고친 헤더가 하나도 없고
⑵ `RHIEncoder.h`가 애초에 폐포에 없음을 보였다. 파일 자체도 HEAD와 동일하다
— 전처리 입력이 같으므로 이 변경이 영향을 줄 수 없다.

- 검증(미실시): 비유니티 Debug 완주, 라이트 추가/삭제/이동 픽셀 대조,
  `dx12.selftest`, 밝기 전후 캡처.

### ② visibleLights — 뷰별 라이트 목록 — 구현 완료 2026-08-09

`SelectLightsForView`(`EnhancedLightPacking.h`)가 뷰마다 고른다. 라이브와
자가 검증이 같은 함수를 쓴다 — 갈리면 "검증은 통과하는데 실전만 다른
그림"이 되고 그 부류는 원인을 찾기가 특히 나쁘다.

**무엇을 빼는가.** 뷰 절두체와 광원 영향 구(중심 = 월드 위치, 반지름 =
`m_range`)의 교차로 지역 광원을 거른다. 방향광은 거리 개념이 없어 언제나
포함한다. 직교 투영에서는 절두체를 만들지 않고(`CreateFromMatrix`가 원근
전용) 컬링을 건너뛴다 — 없는 절두체로 거르는 것보다 다 싣는 쪽이 옳다.

**무엇을 앞에 세우는가 — 기여도 우선.**

```
방향광  score = intensity                       (언제나 지역 광원보다 앞)
지역광  d = max(0, |eye - pos| - range)
        score = intensity / (1 + d*d)
```

카메라가 영향 구 안에 있으면 `d=0`이라 세기 그대로가 되고 멀어질수록
이차로 준다. `1`을 더하는 것은 0 나눗셈을 피하려는 것이고, 덕분에 점수가
유한해 비교가 안전하다.

★ **거리 우선이 아니라 기여도 우선인 이유**: 거리만 보면 가까운 약한
점광이 씬을 밝히는 강한 광원을 밀어낸다. 한도에 걸렸을 때 사라지면 안 될
것은 "가까운 것"이 아니라 "밝기에 기여하는 것"이다.

★ **`stable_sort`다.** 점수가 같은 광원들의 순서가 프레임마다 뒤집히면
한도 경계에 걸린 것이 깜빡인다 — 투명 정렬에서 같은 이유로 stable_sort를
쓴 것과 같은 부류다.

**상한은 그대로 두되 뜻이 바뀌었다.** 패스별 한도(Deferred 64 ·
VolumetricFog 20 · Forward+ 타일당 32)는 셰이더 배열 크기에서 오는 것이라
남는다. 달라진 것은 **잘리는 기준**이다 — 목록이 기여도 순이므로 "앞의
N개"가 곧 "가장 중요한 N개"다. 예전에는 등록 순서였고, 그래서 씬을 밝히는
태양이 나중에 등록됐다는 이유로 빠질 수 있었다.

Deferred가 잘릴 때마다 세우던 `outError`는 걷었다. 뷰가 고른 결과를
한도만큼 받는 것은 이제 설계된 동작이고, 매 프레임 오류 문자열을 세우면
진짜 실패가 그 안에 묻힌다. 잘린 수는 status가 낸다.

**관측.** `dx12.live status`에 한 줄:

```
광원 — 씬 N개 · 뷰 N개(절두체 밖 N) · Deferred 한도 64 [· 한도 초과 N개(기여도 낮은 쪽부터 빠진다)]
```

어두워졌을 때 원인이 컬링인지 한도인지가 이 줄에서 갈린다.

**그림자 캐스터.** `EnhancedShadowPass`가 "배열의 첫 방향광"이 아니라
**가장 센 방향광**을 고른다. 예전에는 등록 순서가 그림자의 주인을 정해,
보조 방향광을 나중에 더하면 그림자가 그쪽으로 옮겨 갔다. 뷰가 미는 목록이
이미 방향광을 세기순으로 앞에 세우므로 결과는 같지만, 그 순서에 말없이
기대면 목록을 만드는 쪽이 바뀔 때 조용히 깨진다 — 패스가 스스로 정한다.

**하지 않은 것.** 스포트를 원뿔이 아니라 구로 다룬다(보수적 — 빠뜨리지는
않고 가끔 필요 없는 것을 남긴다). 목록 길이가 실제로 문제가 될 때 좁힌다.

### ③ visibleDraws — 뷰별 프러스텀 컬링 — 구현 완료 2026-08-09

MultiCameraRenderPlan §14 후속 과제("draws 수집 중복")와 한 몸이었다.

```
[프레임당 1회]  BuildDrawPool   프록시 스냅샷 → 드로우 풀 (변환·재질·팔레트 해석)
[뷰마다]        CaptureFromCamera  절두체 × AABB → draws/forwardDraws (+ 투명 정렬)
```

**수집을 뷰 밖으로 뺐다.** 메시·재질·본 팔레트를 뽑는 일은 카메라를 보지
않는데도 뷰마다 반복했다 — 뷰가 둘이면 같은 복사를 두 번 했다. 이제
`TickLive`가 프록시 커맨드를 민 직후 한 번 모으고, 뷰는 거르기만 한다.
데칼도 같은 풀에서 한 번 모은다(순서는 그대로 — 데칼 패스가 순서를
블렌드 결과로 쓴다).

**컬링.** 프록시가 월드 AABB를 든다(`m_worldBounds` · `m_hasWorldBounds`).
생성자와 갱신 커맨드 양쪽에서 담는다 — 월드 행렬이 바뀌면 상자도 바뀌므로
한 번 담고 마는 값이 아니다.

★ **스키닝 메시는 자르지 않는다**(`m_hasWorldBounds=false`). 메시의 AABB는
바인드 포즈 것이고 애니메이션이 그 밖으로 정점을 민다. 믿고 자르면 팔을
뻗은 캐릭터가 화면 가장자리에서 통째로 사라진다 — **못 자르는 것보다 나쁘다.**
스키닝까지 자르려면 포즈를 반영한 바운즈가 먼저다.

★ 직교 투영에서는 거르지 않는다(②의 광원과 같은 이유).

**관측.** `dx12.live status`에 한 줄:

```
드로우 — 풀 N개 · 절두체 밖 N개 · 제출 N개
```

자른 수가 늘 0이면 컬링이 도는지 자체가 의심스럽고, 씬 대비 너무 크면
절두체나 바운즈가 어긋난 것이다.

**걷어낸 것.**

| | |
|---|---|
| `Scene::CullMeshData`의 카메라 루프 | 옥트리 질의 결과를 버리고, 스레드풀 태스크 일곱이 컴포넌트 리스트를 전수 재순회해 AABB를 재던 자리. 카메라가 늘수록 O(카메라 × 전체 메시) |
| `PushCullData`·`PushShadowRenderData` 계열 + `m_findProxyVec`·`m_findShadowProxyVec` | 쌓기만 하고 `Get*Buffer`를 읽는 코드가 저장소에 없었다 |
| `PushShadowRenderQueue` 계열 + `m_shadowRenderQueue` | 호출자 0 |
| `EnhancedSceneRenderer::CaptureLiveFrame` | 호출자 0. 남겨 두면 드로우 풀을 채우지 않은 채 밀봉하는 두 번째 진입점이 된다 |

함수 이름도 `Scene::CullMeshData` → `UpdateRenderData`로 바꿨다. 컬링을
하지 않는 함수가 그 이름을 달고 있는 것이 다음 사람을 속인다.

### ②③ PIX 실측 검증 — 2026-08-09

pixtool로 에디터를 띄워(`launch` + `programmatic-capture --until-exit`)
CLI 시나리오가 씬을 짓고, `pix.capture begin/end`로 GPU 캡처 4개를 떴다.
판정은 status 줄 + PIX 이벤트 CSV + `save-resource`로 뽑은 최종 RT.
시나리오·드라이버는 세션 스크래치(`pixverify/`)에 있다.

**시나리오**: 카메라 (0,2,-10) 정면. 도형 3개(z=5~8), 점광 PFront(z=5, r6)·
PBack(z=-40, r6), 방향광 1. 측정 2에서 도형과 PFront를 z=-200으로 옮긴다.

| 측정 | 기대 | 실측 |
|---|---|---|
| 1 기준선 | 광원 뷰 2(절두체 밖 1=PBack) · 드로우 제출 3 | **일치** |
| 2 물체 후방 이동 | 광원 뷰 1(절두체 밖 2) · 드로우 절두체 밖 3 · 제출 0 | **일치** — 그림도 하늘만 남음 |
| 3 점광 끔 | 광원 씬 1 · 제출 3 복귀 | **일치** |

②(기여도 선별·절두체 컬링)와 ③(드로우 풀·절두체 필터)이 수치로 확정됐다.

**과정에서 잡은 기존 엔진 버그 — 루트 오브젝트의 월드 캐시가 죽어 있었다.**
`Transform::UpdateWorldMatrix`의 부모-없음 분기가 `m_worldMatrix = m_localMatrix`
대입만 하고 분해 경로(`SetAndDecomposeMatrix`)를 건너뛰어,
`m_worldPosition`·`m_worldQuaternion` 캐시가 영영 초기값(원점·항등)이었다.
행렬만 읽는 소비자(메시 프록시)는 무사했고, 캐시를 읽는 소비자(광원 프록시의
위치·방향, `CameraComponent`의 시점 동기화)만 **루트 오브젝트에서만** 조용히
깨졌다 — 씬에서 저작된 오브젝트는 대부분 자식이라 드러나지 않았고, CLI로
만든 루트 광원·카메라가 즉시 걸렸다. 증상: 광원 프록시 덤프가 전부
pos(0,0,0)/dir(0,0,1). ①의 회귀가 아니다 — 옛 `ApplyLightData`도 같은
캐시를 읽었다(FT_Lights의 저작 태양 방향도 사실 반영된 적이 없었을 것).

★ **판별 방법이 남길 가치가 있다.** "컬링이 안 된다"에서 판정 버그와 낡은
데이터를 가른 것은 status의 **광원 프록시 덤프**(≤6개일 때 type·pos·r·i·dir
출력, 이번에 추가)와 절두체·구 교차의 **독립 재현 테스트**(20줄 단독 exe —
동일 행렬 구성으로 Intersects를 직접 확인, 수학은 정상으로 판명)였다.

**①의 세기 단일 곱 — 밝기 실측으로 확인.** 창 캡처 2회 비교(태양만 켠
정적 씬, `capture-window.ps1`). IBL 하늘광이 지배하는 씬이라 절대값이 아닌
**두 데이터 점의 일관성**으로 판정했다: 세기 1→2에서 ×1.07 → 태양 기여
S≈0.075×주변광으로 역산되고, 단일 곱 모델의 1→4 예측은 ×1.21 —
**실측 ×1.17~1.21과 일치**. 이중 곱 모델은 같은 1→2 점에서 1→4에 ×1.34+를
예측하므로 기각. (측정 주의: PIX `save-resource`는 두 뷰 중 어느 RT를 줄지
비결정적이라 밝기 비교에 쓰면 안 된다 — 실측으로 배웠다.)

### ③-b 옥트리 계통 제거 — 2026-08-09

③이 질의를 걷어내자 `CullingManager`가 쓰기 전용이 됐다 —
`MeshRenderer::Awake`가 `Register`로 채우고 아무도 읽지 않는다. 계통을
통째로 걷었다.

| 지운 것 | |
|---|---|
| `RenderEngine/CullingManager.{h,cpp}` | 싱글턴 + 옥트리 질의 |
| `Utility_Framework/Octree.{h,cpp}` · `Core.OctreeNode.h` | `CullingManager.h`가 유일한 include처였다 |
| `MeshRenderer`의 `Register`/`Unregister` | 쓰기 경로 |
| `Dx11Main`·`GameMain`의 `Initialize`(월드 5km 상자 + `OctreeConfig`), `Dx11Main`의 `Shutdown` | 진입점 배선 |
| `EngineBootstrap`의 `GetInstance`/`Destroy` | 수명 배선 |

★ **"나중에 쓸지 모르니 남긴다"를 하지 않은 이유.** 가속 구조가 다시
필요해지면 그때 붙일 자리는 여기가 아니다 — 게임플레이 컴포넌트가 자기를
등록하는 형태가 아니라, 보유층(`RenderScene`)이 프록시로 유지하는 형태여야
한다(§3의 경계 규약 1). 즉 되살릴 때도 이 코드를 그대로 쓰지 못한다.
남겨 두면 "쓰이는 것처럼 보이는 죽은 계통"만 남는다. 형태가 필요하면
git 이력에 있다.

- 검증: 컬링 켬/끔 픽셀 대조(같아야 한다) + 드로우 수 감소 관측
  (`dx12.live status`에 culled/total), 카메라를 돌려 절두체 밖 물체가
  빠지는지 확인.

### ④ 카메라 등록 정리 (고정 슬롯 폐지)

`CameraContainer` 고정 10슬롯 + `GetCamera(1)` 하드코딩을 등록 기반으로:
`CameraComponent`가 자기 카메라를 만들어 등록하고, 뷰 배정은 카메라
**세대 ID**(단조 증가)로 매칭해 포인터 ABA를 함께 닫는다. 표시 대상
선정(어느 카메라가 게임뷰인가)은 우선순위/명시 지정으로 —
`GetLastCamera()`의 "마지막 활성"이라는 모호함을 없앤다.

`kMaxLiveCameraViews`는 유지하되(표시 슬롯 메모리 = 뷰당 ~25MB) 상한
초과를 로그로 관측 가능하게 한다.

## 5. 왜 이 순서인가

★ ①을 하며 확인된 것: **프록시 타입 분리가 ①의 선결 조건이었다.** 광원을
"프록시"로 들이려면 프록시가 메시 전용 클래스가 아니어야 했고, 그래서 두
작업이 한 몸이 됐다. ②③이 딛고 설 자리(뷰가 소유하는 목록)도 이 계층 위에
선다 — `visibleLights`는 `LightRenderProxy` 목록이고 `visibleDraws`는
`PrimitiveRenderProxy` 목록이다.


①이 먼저인 이유: 가장 작고, ②가 딛고 설 단일 진실을 만든다. ②가 ③보다
먼저인 이유: 라이트는 이미 뷰별 재변환 루프가 있어 그 자리를 바꾸기만
하면 되지만, 드로우는 "수집 1회 + 뷰별 필터" 분리라는 구조 변경이 필요해
더 크다. ④는 어느 것과도 독립이지만, ②③이 뷰를 렌더링의 단위로 굳힌
뒤라야 "카메라 = 뷰의 원천"이라는 그림이 완성된 상태에서 정리할 수 있다.

## 6. 하지 않을 것

- **가속 구조 부활** — ③의 선형 AABB 검사가 실측으로 느려질 때까지 보류.
  지금 씬 규모(수백 프록시)에서 선형이 충분할 가능성이 높다. 필요해지면
  보유층(RenderScene)이 프록시로 유지하는 형태로 새로 짓는다 — 옛
  `CullingManager`는 ③-b에서 걷었다.
- **GPU 드리븐 컬링·인다이렉트 드로우** — CPU 컬링이 병목으로 실측되기
  전에는 안 간다.
- **그림자 뷰의 일반화** — Unreal은 그림자 캐스케이드도 뷰다. 맞는
  방향이지만 우리 그림자 패스는 컨텍스트에서 직접 캐스케이드를 계산하는
  현행 구조로 충분하다. 캐스터가 여럿 필요해질 때 재검토.
- **방향광 다중 그림자** — `EnhancedShadowPass`의 기존 판단(둘 이상 쓰는
  씬이 나왔을 때) 유지.
- **Unity식 C# 노출 API** — CullingResults를 스크립트에 노출하는 것은
  이 계획의 범위 밖. 지금은 렌더러 내부 구조의 문제다.
