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

### ② visibleLights — 뷰별 라이트 목록 (상한 통일)

`CaptureFromCamera`의 라이트 재변환 자리를 "뷰 프러스텀 × 라이트 영향
반경" 교차 검사로 바꾸고, 결과를 거리·세기 기반으로 정렬해 뷰가 소유한다.
패스 상한들은 이 목록의 앞 N개를 받는 것으로 통일 — 드롭이 일어나면
**뷰 단위로 한 번, 관측 가능하게**(`dx12.live status`에 dropped 수) 일어난다.

- 방향광은 항상 포함(프러스텀 교차 무의미).
- 그림자 캐스터 선정("첫 방향광")도 이 자리로 옮긴다 — 뷰가 정하는 것이
  맞다(뷰마다 캐스케이드가 이미 다르다).

### ③ visibleDraws — 뷰별 프러스텀 컬링 (수집 1회 + 뷰별 필터)

MultiCameraRenderPlan §14 후속 과제("draws 수집 중복")와 한 몸이다:

```
[프레임당 1회]  프록시 스냅샷 → 공통 드로우 풀 (변환·재질 해석)
[뷰마다]        프러스텀 × 프록시 AABB → visibleDraws (+ 투명 정렬)
```

프록시에 월드 AABB가 필요하다 — `CullMeshData`가 쓰던 바운즈 소스를
프록시 갱신 경로로 옮긴다. 이 슬라이스가 끝나면 `Scene::CullMeshData`의
카메라별 전수 순회와 `PushCullData` 계통(소비자 0)을 **통째로 삭제**한다.
게임 스레드에서 매 프레임 나가던 리스트 값 복사 + 스레드풀 태스크가
사라지는 것이 부수 이익이다.

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

- **옥트리 부활** — `CullingManagers`의 옥트리는 ③의 선형 AABB 검사가
  실측으로 느려질 때까지 보류. 지금 씬 규모(수백 프록시)에서 선형이 충분할
  가능성이 높고, 가속 구조는 보유층(RenderScene)에 넣는 것이 자리다.
  ③ 완료 시점에 소비자 없는 `CullingManager` 계통도 삭제 후보로 재평가.
- **GPU 드리븐 컬링·인다이렉트 드로우** — CPU 컬링이 병목으로 실측되기
  전에는 안 간다.
- **그림자 뷰의 일반화** — Unreal은 그림자 캐스케이드도 뷰다. 맞는
  방향이지만 우리 그림자 패스는 컨텍스트에서 직접 캐스케이드를 계산하는
  현행 구조로 충분하다. 캐스터가 여럿 필요해질 때 재검토.
- **방향광 다중 그림자** — `EnhancedShadowPass`의 기존 판단(둘 이상 쓰는
  씬이 나왔을 때) 유지.
- **Unity식 C# 노출 API** — CullingResults를 스크립트에 노출하는 것은
  이 계획의 범위 밖. 지금은 렌더러 내부 구조의 문제다.
