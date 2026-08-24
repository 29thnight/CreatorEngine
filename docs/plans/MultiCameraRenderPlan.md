# 카메라별 표시 슬롯 도입 계획 (씬뷰/게임뷰 동시 표시)

> 인수인계 메모. 2026-08-07 작성. 착수 전 이 문서를 먼저 읽는다.

## 0. 2026-08-24 카메라 아키텍처 이행 결과

이 문서의 고정 `CameraContainer` 슬롯, `Camera*` 기반 뷰 항등성,
`CameraManagement->GetLastCamera()` 전제는 더 이상 현재 구조가 아니다. 아래
본문은 다중 표시 슬롯을 처음 도입한 역사와 렌더 회귀 근거로만 유지한다.

현재 정본은 다음과 같다.

- `CameraComponent`가 직렬화되는 `Camera` 값을 직접 소유한다. 각 `Scene`이
  `CameraSystem`을 소유하고 `Scene::Cameras().GetPrimaryCamera()`가 `m_isPrimary`를
  우선하며, 없으면 가장 작은 component instance ID를 선택한다. 프로세스 전역
  카메라 registry는 없다.
- Editor 카메라와 이동 상태는 Editor 전용 `EditorCameraRig`가 함께 소유한다.
  게임 카메라 registry나 RenderCore 슬롯에 등록하지 않는다.
- Editor/Player Host가 `EnhancedLiveViewRequest`에 `EnhancedLiveViewKey`,
  `FrameCameraSnapshot`, `EnhancedLiveDisplayTarget`, `EnhancedLiveViewFlags`를 값으로
  밀봉한다. RenderCore는 `Camera*`를 받거나 역참조하지 않는다.
- 표시 대상(Editor/Game)과 기능(SceneOverlay/ScreenSpaceUI/CanvasPreview)은 카메라
  속성이 아니라 Host의 뷰 정책이다. backend의 실제 슬롯은 내부 구현일 뿐이다.
- 고정 카메라 인덱스 저장소였던 `RenderPassData`와 `CameraContainer`는 삭제했다.
  프록시는 `RenderScene`, 뷰 입력과 시간축 상태는 Enhanced RenderView가 소유한다.
- AI·Foliage는 프레임 시점의 `BoundingFrustum` 값을 받고, CLR/콘솔/렌더 테스트도
  활성 씬의 `CameraComponent` 또는 값 스냅샷만 사용한다.

검증: v145 x64 Debug에서 `ScriptBinder`, `RenderEngine`, `RenderTests`,
`CreatorEditor`, `Player` 빌드/링크를 통과했다. Reflection golden은 77/77
직렬화·실패 0·diff 0이다. Editor `camera.editor match`와 구형
`FT_Primitives.creator` 30프레임 전환은 primary snapshot을 출력했고,
presentation/render queue가 모두 balanced인 정상 shutdown을 확인했다.

## 1. 왜 하는가 — 증상과 원인

**증상 1.** 에디터 기본 실행에서 게임뷰가 비어 있다.
**증상 2.** 재생을 누르면 씬뷰가 검게 되고, 환경에 따라 두 뷰가 번갈아 깜빡인다.

두 증상의 원인은 하나다. 라이브 러너가 **한 프레임에 한 카메라만** 그린다.

```cpp
// EnhancedSceneRendererLive.cpp — GetLiveDisplaySrv
if (nullptr == camera || camera != state.boundCamera) return nullptr;
```

그런데 두 뷰는 서로 다른 카메라를 넘긴다.

| 창 | 넘기는 카메라 | 코드 |
|---|---|---|
| Scene | 에디터 카메라 | `SceneViewWindow.cpp`의 `GetLiveDisplayImTextureId(cam)` |
| Game | `CameraManagement->GetLastCamera()` | `GameViewWindow.cpp` |

그리고 `App.cpp`는 재생 중이 아니면 **항상 에디터 카메라**를 바인딩한다. 그래서
재생 전 게임뷰는 원리상 절대 그려질 수 없고(증상 1), 재생 중에는 `boundCamera`가
게임 카메라로 넘어가 씬뷰가 죽는다(증상 2). `GetLastCamera()` 반환이 프레임 사이에
흔들리면 두 뷰가 번갈아 그림을 받아 깜빡임으로 보인다.

## 2. 결정된 방침

- **카메라별 표시 슬롯을 두고, 필요한 카메라를 모두 렌더한다.** 각 뷰가 자기 최신
  프레임을 계속 표시하므로 깜빡임이 사라지고, 재생 전에도 게임뷰가 나온다.
  대가는 GPU 비용(뷰 2개면 씬을 2번 그린다). 실측 GPU가 0.45ms 수준이라 여유가 있다.
- **PIE 중 씬뷰는 활성 씬(=플레이 중인 씬)을 보여준다**(유니티 방식). 씬은 하나,
  카메라만 둘이다. 이 방침 덕분에 씬 분리와 카메라 분리가 직교한다.

### 걸림돌이 아닌 것 (확인 완료)

밀봉이 렌더 타깃 크기를 카메라가 아니라 `DirectX11::DeviceStates->g_Viewport`에서
가져온다(`CaptureFromCamera` 안). 두 뷰가 같은 해상도를 쓰므로 **카메라마다
파이프라인을 따로 세울 필요가 없다.** 슬롯 집합만 나누면 된다.

## 3. 현재 구조

`RenderEngine/Render/Scene/EnhancedSceneRendererLive.cpp`

```cpp
struct LivePipeline
{
    ...
    DisplaySlot      slots[3];        // 표시 1 + 인플라이트 최대 2
    int              displaySlot{-1}; // DX11이 표시 중인 슬롯
    std::vector<int> pendingQueue;    // 제출 순서의 인플라이트 슬롯들
};

struct LiveState
{
    const Camera* boundCamera{nullptr};   // 지금 표시 중인 그림의 원천 카메라
    ...
};
```

`TickLive`는 "밀봉 1회 → 렌더 1회" 구조다.

## 4. 바꿀 지점 (아홉 곳, 한꺼번에)

중간 상태가 성립하지 않는다. 일부만 고치면 빌드가 깨진다.

| # | 위치 | 할 일 |
|---|---|---|
| 1 | `LivePipeline` 멤버 (`slots`/`displaySlot`/`pendingQueue`) | `CameraView` 배열로 교체 |
| 2 | `BuildPipeline`의 공유 텍스처 생성 루프 | 뷰마다 × 슬롯마다 생성 |
| 3 | `TeardownPipeline`의 슬롯 정리·묘지 이관 | 뷰마다 순회 |
| 4 | `RenderOnce(int slotIndex, ...)` | `RenderOnce(CameraView&, int slotIndex, ...)` |
| 5 | 인플라이트 승격 루프 (`pendingQueue` 소비) | 뷰마다 |
| 6 | 렌더 슬롯 선택 (`for i < 3`) | 뷰마다 |
| 7 | `GetLiveDisplaySrv(camera)` | 뷰 검색 후 그 뷰의 `displaySlot` |
| 8 | `GetLiveDisplayImTextureId(camera)` | 위와 같음 |
| 9 | `TickLive` 시그니처 + `EngineEntry/App.cpp` 호출부 | 카메라 목록을 받도록 |

## 5. 설계 상세

### CameraView

```cpp
// 카메라 하나가 쓰는 표시 슬롯 묶음.
struct CameraView
{
    const Camera*    camera{ nullptr };
    DisplaySlot      slots[3];
    int              displaySlot{ -1 };
    std::vector<int> pendingQueue;
};
static constexpr int kMaxCameraViews = 2;   // 씬뷰 + 게임뷰
CameraView views[kMaxCameraViews];
```

카메라 → 뷰 배정은 `camera` 포인터로 찾고, 없으면 빈 슬롯에 배정한다. 카메라가
사라지면(씬 전환 등) 해당 뷰를 비우고 슬롯을 묘지로 보낸다.

### TickLive 재구성

프레임당 한 번만 도는 것과 카메라마다 도는 것을 갈라야 한다.

```
[프레임당 1회]
  ProxyCommandQueue->Execute()
  renderScene->EraseRenderPassData() / Update() / OnProxyDestroy()
  PublishDebugSnapshot() / ApplyAndPublishTuning()

[카메라마다]
  뷰 배정
  인플라이트 승격 (뷰의 pendingQueue)
  CaptureFromCamera(camera)     ← cameraSnapshot이 카메라별로 달라진다
  렌더 슬롯 선택 → RenderOnce(view, slot)
```

`boundCamera`는 제거하고 `view.camera`가 그 역할을 한다.

### App.cpp 호출부

재생 여부와 무관하게 둘 다 넘긴다. 그래야 재생 전에도 게임뷰가 나온다.

```cpp
Camera* cameras[2];
uint32_t count = 0;
if (Camera* editor = EnhancedSceneRenderer::GetEditorCamera()) cameras[count++] = editor;
if (auto game = CameraManagement->GetLastCamera();
    game && (0 == count || game.get() != cameras[0]))
{
    cameras[count++] = game.get();
}
EnhancedSceneRenderer::TickLive(dt, cameras, count, SceneManagers->IsSceneLoading());
```

## 6. 주의할 점

- **`frameContext`가 `draws`/`lights`의 주소를 든다.** 카메라마다 `CaptureFromCamera`를
  다시 부르면 그 벡터가 재구성되므로, 뷰1 렌더 제출이 끝난 뒤 뷰2를 밀봉하는
  순차 흐름을 지켜야 한다. 밀봉을 몰아서 하고 렌더를 몰아서 하면 안 된다.
- **`draws` 수집은 카메라와 무관하다.** 지금은 `CaptureFromCamera`가 프록시 스냅샷을
  통째로 훑으므로 카메라 수만큼 중복된다. 1차 구현에서는 그대로 두고, 필요하면
  "draws 수집 1회 + cameraSnapshot만 카메라별"로 나눈다.
- **공유 텍스처가 3개에서 6개로 늘어난다.** 1920x1080 RGBA8 기준 뷰당 약 25MB.
- **`GameViewWindow`는 `camera->m_cameraIndex == 0`이면 "No Camera rendering"을 그린다.**
  에디터 카메라가 index 0이라, 씬에 게임 카메라가 등록되지 않으면 이 작업과 무관하게
  여전히 비어 보인다. `CameraComponent::Awake`가 `GetCamera(1)`을 가져가는 구조와
  함께 별도로 확인할 것.
- 슬롯 3개(표시 1 + 인플라이트 2)라는 기존 근거는 유지한다. 인플라이트 1개로는
  제출이 쉬는 틱이 38%였다는 실측이 코드 주석에 남아 있다.

## 7. 검증 방법

`Dynamic_CPP/Assets/Scenes/Test1.creator`에 Animator + SkinnedMesh(`Gunner_F_Mythic`)가 있다.

```
Academy_4Q.exe --script <scene.switch 한 줄이 담긴 파일>
```

확인할 것:
1. 재생 전 — 씬뷰와 게임뷰가 **둘 다** 그림을 보여준다
2. 재생 중 — 씬뷰가 검게 되지 않고, 두 뷰 모두 계속 갱신된다
3. 깜빡임이 없다
4. `Settings > Pipeline Setting`의 Frames/GPU 수치로 뷰가 둘 다 도는지 교차 확인

## 8. 관련 완료 작업

- `378db280` — 프록시 갱신이 `RenderPassData` 부재로 죽던 문제 수정
  (`Scene::CullMeshData`의 조기 `return`. 컬링·그림자 데이터도 함께 되살아남)
- `0213e5e5` — PIE를 사본 씬 방식에서 유니티식 백업/복원으로 변경
  (재생 중 에디터씬 모델이 겹쳐 보이던 문제 해결)

이 둘이 끝난 상태에서 시작한다. 씬은 하나로 정리됐으므로 이제 순수하게
"활성 씬 하나를 여러 카메라로 그린다" 문제만 남았다.

## 9. 구현 기록 (2026-08-07)

§4의 아홉 곳을 계획대로 반영해 완료했다. 검증 결과:

1. 재생 전 — 씬뷰(에디터 카메라)·게임뷰(Main Camera) **동시 표시 확인**(창 캡처)
2. 재생 중 — 씬뷰가 검게 되지 않고, 1초 간격 픽셀 대조로 **두 뷰 모두 갱신** 확인
3. 무인 스모크(Test1) — 렌더 프레임이 120틱 대기 동안 +241~244(틱당 ~2 제출
   = 두 뷰 가동의 교차 증거), 검증 레이어 오류 0, exit 0

### 계획과 달라진 점

- **총 인플라이트를 뷰 합산 2로 제한**했다. '인플라이트 2 = 링(kFrameCount=3)의
  안전 거리'는 제출 총량 기준 실측이라 뷰당 2씩 총 4를 들면 BeginFrame이
  얼로케이터 펜스에서 블로킹한다. 예산이 1만 남는 틱의 씬뷰 선점 편향은
  카메라 순회 시작점 라운드로빈(viewRotation)으로 상쇄했다.
- 카메라가 뷰에서 교체될 때 `displaySlot = -1`로 리셋한다(옛 카메라의 그림이
  새 카메라 창에 남지 않게). 인플라이트였던 옛 프레임이 한두 틱 승격될 수
  있으나 곧 새 프레임이 덮는다.

## 10. 잔상 회귀와 수정 (2026-08-07)

멀티카메라 도입 직후 씬 뷰에 잔상이 나타났다. **뷰가 둘이면 시간축 누적을
하는 패스도 뷰마다 있어야 한다**는 것을 §4의 아홉 곳이 놓쳤다.

### 원인

라이브 그래프에서 프레임을 넘겨 상태를 잇는 패스는 SSGI 하나뿐인데,
`LivePipeline`이 그 인스턴스를 하나만 들고 있었다. 시간축 상태가 전부
패스 멤버다 — `m_history[2]` · `m_historyDepth[2]` · `m_previousViewProjection`
· `m_historyIndex`.

히스토리 슬롯이 2칸인데 프레임당 두 번(카메라마다) 회전하므로 결과적으로
**각 카메라가 늘 상대 카메라의 히스토리를 읽는다**:

| | 슬롯 인덱스 | 쓰기 | 읽기 |
|---|---|---|---|
| 카메라 A | 0 → 1 | 슬롯 1 | 슬롯 0 = B의 지난 프레임 |
| 카메라 B | 1 → 0 | 슬롯 0 | 슬롯 1 = A가 방금 쓴 것 |

재투영 행렬도 직전에 렌더한 다른 카메라의 것이 된다. 리졸브는 깊이 차이
(`< 0.01`)만 보고 히스토리를 받아들이는데 두 카메라가 같은 씬을 보므로
대부분 통과한다 → 다른 시점의 화면 공간 GI가 최대 32프레임 지수 평균으로
섞인다.

### 원인 규명 방법 (재사용 가능)

`camera.editor` 명령을 추가해 세 조건을 대조했다. 애니메이션은 셋 다 재생 중.

| 조건 | 결과 |
|---|---|
| 카메라 2 · 시점 다름 | 잔상 |
| 카메라 1 · 같은 근접 시점 | 깨끗 |
| 카메라 2 · 시점 통일(`camera.editor follow on`) | 깨끗 |

카메라 개수나 근접 거리 단독으로는 재현되지 않는다는 것이 요점이다 —
시점이 겹치면 공유 히스토리가 우연히 맞는 값이 되므로 깨끗해진다.

### 수정

`EnhancedSSGIPass`를 `CameraView` 멤버로 옮겨 뷰마다 하나씩 둔다.
PSO는 `psoManager`가 바이트코드 해시로 공유하므로 컴파일은 그대로 한 번이고,
추가 비용은 히스토리 텍스처뿐이다 — GI가 절반 해상도(1920x1080 → 960x540)라
`(RGBA16F 8B + R32F 4B) x 2슬롯`으로 뷰당 약 12MB.

튜닝 적용은 전 뷰에 걸고, 미러 되읽기는 `views[0]`을 대표로 쓴다.

검증: `dx12.ssgi` 통과(누적 8프레임 뒤 평균 8.00 — 시간축이 살아 있다),
`dx12.selftest` 4/4, 무인 스모크 검증 레이어 오류 0.

### ★ 다음에 같은 함정을 밟을 자리

**판단 기준: 프레임을 넘겨 상태를 잇는 패스인가.** 그렇다면 뷰마다 하나다.

- `EnhancedVolumetricFogPass` — **처리 완료**(11장). 배선하면서 처음부터
  뷰별로 넣었다.
- `EnhancedSSRPass` · `EnhancedSSSPass` — 아직 미배선. SSR은 시간축 상태가
  없어 한 인스턴스로 족하고, SSS도 마찬가지다. 배선 전에 멤버를 다시 볼 것.

## 11. 볼류메트릭 포그 배선 (2026-08-07)

10장의 교훈을 적용한 첫 사례다. 포그는 라이브 그래프에 없던 패스라
"옮긴다"가 아니라 **처음 배선하면서 뷰별로 넣었다**.

포그의 시간축 상태는 SSGI와 같은 모양이다 — 프록셀 격자
(`m_voxelTemp[2]` · `m_voxelFinal`, "프레임을 넘겨 살아야 하므로 transient가
아니라 패스가 든다"), `m_readIndex` 핑퐁, `m_previousViewProjection`.

### 자리

`forward.Declare` 뒤, `postChain` 앞. 톤맵 앞이라야 DX11과 같은 순서다.

투명(Forward+) 합성이 포그 앞에 오므로 투명에도 포그가 걸린다(12장).

### 기본 꺼짐

두 가지 이유다.

- **메모리.** 켤 때 Private가 1437 → 1564MB (**+127MB**, 실측). 격자만
  뷰당 42MB(160x90x128 RGBA16F x3)이고 합성 출력과 힙이 나머지다.
  그래서 `Initialize`를 처음 켜지는 프레임까지 미룬다(`fogReady`) —
  끈 채로는 증가가 없음을 실측으로 확인했다.
  **끄면 다시 놓는다**(`ReleaseFogResources`). 이것이 없으면 "끄면 안
  잡는다"가 '한 번도 안 켰을 때만' 성립한다 — 창에서 껐다 켜는 것이
  이 UI가 유도하는 바로 그 조작이라 리뷰에서 잡혔다. 해제는 GPU가 격자를
  읽는 중일 수 있어 완주를 기다린 뒤에 하고, 그 대기가 디버그 스냅샷을
  읽는 CE 스레드를 세우지 않도록 락 밖(TickLive)에서 한다.
- **그림.** 켜면 모든 씬의 그림이 달라진다. 저작이 고를 일이다.

무인 검증용으로 `CREATOR_DX12_FOG=1` 환경변수를 둔다(BuildPipeline의 후처리
환경변수와 같은 취지). 창에서 고른 값을 리사이즈가 되돌리면 안 되므로
`InitializeRuntime`에서 한 번만 읽는다.

### 입력 텍스처에서 걸린 것 둘

- **중립 구름 그림자를 반드시 물려야 한다.** 셰이더가 구름의 켬/끔을 보지
  않고 알파를 곱하므로(패스 헤더의 발견 ②), 안 물리면 가려짐이 0이 되어
  **포그가 통째로 사라진다.** 1x1 흰색을 물린다.
- **`textureCache`의 흰색 폴백을 그대로 쓰면 안 된다.** 그것은 재질에
  텍스처가 없을 때 GBuffer가 디스크립터로 직접 묶는 리소스인데, 그래프가
  상태를 옮기면 다음 프레임 GBuffer가 어긋난 상태로 읽는다 — 그래프는
  임포트한 리소스를 원래 상태로 되돌려 주지 않는다(`stateWriteback`은
  '어디로 남았는지'만 알려 준다). 포그 전용으로 따로 만든다.
- 끝 상태는 `ALL_SHADER_RESOURCE`로 맞춘다. `RGResourceState`에 PIXEL 전용
  값이 없어 `ShaderResource`가 곧 ALL인데, `textureCache`는 업로드를
  PIXEL로 끝내므로 그대로 임포트하면 배리어의 before가 실제와 어긋난다.
  넓히는 배리어는 **캐시 수명당 한 번만** 건다(`fogNoiseStateWidened`) —
  포그를 껐다 켤 때 또 걸면 이미 ALL인 리소스에 before=PIXEL로 나간다.

포그 자원 확보가 실패하면 **포그만 끄고 렌더러는 살린다.** `RenderOnce`가
false를 돌려주면 `TickLive`가 파이프라인을 헐고 러너를 꺼 버리는데, 기본이
꺼짐인 선택 기능 하나 때문에 화면 전체를 잃는 것은 대가가 맞지 않는다.

### 검증

- 포그 켬/끔 픽셀 대조 — 씬뷰 50.5% · 게임뷰 16.8% 변화(무음 실패가 아니고
  **두 뷰의 인스턴스가 각각 돈다**)
- 두 뷰 모두 잔상 없음(10장의 회귀가 재발하지 않는다)
- `dx12.fog` · `dx12.ssgi` · `dx12.selftest` 통과, 무인 스모크 검증 레이어
  오류 0(포그 켠 상태 포함)

## 12. 투명 렌더링 (2026-08-07)

"forward 결과를 아무도 소비하지 않는다"를 고치러 갔다가, **배선만 빠진 게
아니라 forward가 아직 투명 렌더러가 아니었다**는 것을 알게 됐다. Forward+
광원 컬링과 N·L 조명까지만 된 슬라이스였고, 소비자가 없어서 아무도 몰랐다.

### 빠져 있던 것 일곱

| # | 증상 | 고친 것 |
|---|---|---|
| ① | 재질 색·알파가 없다 | `baseColor`를 인스턴스 버퍼로 올려 두고 PS가 무시했다. 알파는 `1.0` 하드코딩. → `lighting * baseColor.rgb`, `alpha = baseColor.a` |
| ② | 블렌드가 꺼져 있다 | `blendEnable` 미설정(=false). → `true` |
| ③ | 깊이를 쓴다 | 앞의 투명이 뒤의 투명을 지운다. → `depthWriteMask = ZERO`(테스트는 LESS 유지) |
| ④ | 정렬이 없다 | 프록시 순회 순서 = 임의. → 밀봉 단계에서 뷰 깊이 백투프론트 |
| ⑤ | 합성이 없다 | 자기 타깃에 그리고 버렸다. → 라이팅 타깃에 직접 그린다 |
| ⑥ | 하늘 타일에 광원이 0 | 타일 깊이가 GBuffer(불투명) 기준인데 투명은 거기 없다. → 앞쪽을 열고 방향광은 항상 포함 |
| ⑦ | 방향광이 밑에서 비춘다 | `position`을 방향으로 읽었다. → `direction` 슬롯(Deferred와 같은 규약) |

⑤만 고쳤다면 투명이 '불투명한 단색 덩어리'로 나와 지금보다 나빴다.

### ⑥⑦을 잡은 방법 — 셰이더 프로브

증상은 "투명면의 위쪽 절반이 새까맣다"였고 경계가 물체 음영이 아니라
**지평선**을 따랐다. 두 단계로 갈랐다.

1. PS를 상수 마젠타로 바꿔 봤다 → 모양이 균일하게 칠해졌다. 기하·깊이·
   블렌드·알파는 정상이고 `lighting`만 변수라는 뜻.
2. PS를 `R=타일에 광원이 있는가 · G=조명이 0이 아닌가`로 바꿨다 →
   **R은 어디나 1인데 G만 갈렸다.** 컬링(⑥)은 이미 고쳐져 있었고 남은 것은
   조명식이었다. 거기서 `Contribute`가 `position`을 방향으로 읽는 것을 봤다.

증상만 보고 고쳤다면 ⑥에서 멈췄을 것이다 — ⑥을 고쳐도 그림이 그대로였고,
그 '변화 없음'이 다음 단서였다.

### 합성 자리

`m_inputs.lighting`이 유효하면 forward가 **그 타깃에 직접** 그린다(클리어
생략). 별도 타깃에 그려 두고 나중에 합성하려면 프리멀티플라이드 알파를
따로 관리해야 하고 겹친 투명면의 누적 알파가 어긋난다 — 실제 배경에 바로
블렌드하면 그 문제가 통째로 사라진다. 그래서 `SSGI.Output`에
`allowRenderTarget`을 더했다(전 화면 패스 하나와 HDR 타깃 하나를 아낀다).

`m_inputs.lighting`을 주지 않으면 예전대로 자기 타깃을 만들어 지운다 —
`dx12.forward`·`dx12.forwardshade`가 그 경로를 쓴다(배경이 없어야 픽셀
대조가 성립한다).

### 남은 차이

- ~~텍스처 샘플링이 없다.~~ → 13장에서 붙였다.
- 정렬은 오브젝트 원점 기준이라 서로 관통하는 투명면은 어긋날 수 있다.
  삼각형 단위 정렬이나 OIT가 필요한 영역이다.
- 근거리 타일 컬링이 모든 타일에서 사라졌다(⑥의 대가). 광원이 많은 야외
  씬에서 타일당 목록이 32에 닿을 수 있고, 그 수는
  `GetLastOverflowTileCount`가 알려 준다.

### 검증

투명 재질을 쓰는 씬이 **하나도 없었다**(전부 `m_renderingMode: 0`) — 그래서
아무도 눈치채지 못했다. `FT_Primitives`에서 7개를 알파 0.45 투명으로 바꾼
`FT_Transparent.creator`로 확인했다.

> `Dynamic_CPP/Assets/Scenes/`는 `.gitignore` 대상이라 그 씬은 커밋에
> 따라오지 않는다. 다시 만들려면 `FT_Primitives.creator`를 복사해 첫
> 번째(바닥 `Prim_Plane`)를 빼고 나머지 `m_renderingMode: 0`을 `1`로,
> 같은 재질 블록의 `m_baseColor`의 `a: 1`을 `a: 0.45`로 바꾸면 된다.

`dx12.forward`·`dx12.forwardshade`·`dx12.fog`·`dx12.ssgi`·`dx12.selftest`
통과, 무인 스모크 검증 레이어 오류 0.

## 13. 투명 재질 텍스처 (2026-08-07)

12장이 남긴 "재질 계수만 쓴다"를 마저 붙였다. 베이스 컬러 텍스처를
샘플링해 색과 알파를 거기서 받는다 — 유리·잎사귀처럼 알파가 텍스처에 든
재질은 계수만으로는 모양이 안 나온다.

### 배선

`t4`에 텍스처, `s0`에 샘플러를 놓고 셰이딩 루트 시그니처를 5 → 7
파라미터로 늘렸다(`t0~t3`은 구조화 버퍼라 루트 SRV 그대로). 텍스처는
루트 SRV로 꽂을 수 없다 — 루트 SRV는 버퍼 전용이라 `Texture2D`는
디스크립터 테이블을 요구한다.

샘플러는 GBuffer와 같은 설정(LINEAR + WRAP)이다. 다르면 같은 UV가 다른
텍셀을 집어 같은 재질이 불투명과 투명에서 갈린다.

운반은 `PrepareFrame`에서 한다(GBuffer와 같은 규약) — `GetOrUpload`가
업로드 링과 커맨드 리스트를 쓰므로 기록 중에 부르면 드로우 한가운데에
복사가 끼어든다.

### 흰색 폴백 대신 플래그를 쓴 이유

GBuffer는 텍스처 없는 슬롯에 흰색 폴백을 물려 셰이더의 분기를 없앤다.
이 패스는 그럴 수 없다 — **텍스처 캐시가 없는 자가 검증 경로에서도
돌아야 하는데**(`EnhancedForwardShadeTest`가 `frameContext.textureCache`를
설정하지 않는다) 그때는 만들 흰색조차 없다. 그래서 인스턴스마다
`hasBaseColorMap`을 실어 셰이더가 가르게 했다. 캐시가 없으면 플래그가 0이
되어 예전과 같은 값(계수만)이 나온다.

테이블은 텍스처가 없는 드로우에도 건다. 바인딩된 테이블의 디스크립터는
초기화돼 있어야 하기 때문이다 — 셰이더가 안 읽더라도 비워 두면 규약
위반이고, 널 리소스 SRV는 유효한 디스크립터다(읽으면 0).

### 리뷰에서 잡힌 것

- **`IsValid()`로 업로드 실패를 거르려 한 것이 죽은 코드였다.**
  `GetOrUpload`는 복구 가능한 실패(2D 아님·멀티샘플·업로드 실패)에서
  흰색 폴백을 돌려주므로 `IsValid()`는 늘 참이다. 그 자리에서 `continue`
  했더니 `textureError`가 통째로 사라져 **텍스처가 안 나오는데 로그도
  없는** 상태가 됐다. GBuffer처럼 무조건 전달하도록 고쳤다.
- `ShadeInstance`의 HLSL/C++ 보폭 일치를 `static_assert`로 고정했다.
  어긋나면 컴파일도 검증도 통과하고 GPU만 엉뚱한 필드를 읽는다.
- 디스크립터 링은 프레임당 4096개를 전 패스가 나눠 쓴다. 투명에서
  소진되면 **뒤에 오는 패스(UI·기즈모)가 예산 부족을 겪어 엉뚱한 자리에서
  증상이 난다** — 주석으로 남겼다.

### 남은 것

~~노멀·ORM·이미시브는 아직 샘플링하지 않는다.~~ → 14장에서 PBR과 함께 붙였다.

## 14. Forward+를 PBR로 (2026-08-07)

투명이 확산 N·L로만 조명받던 것을 Deferred와 같은 PBR로 올리고, 노멀·ORM·
이미시브 텍스처와 IBL 앰비언트를 붙였다. **목표는 불투명과의 패리티**다 —
같은 재질이 투명일 때만 다르게 보이는 것이 지금까지 반복된 결함이었다.

### 무엇을 맞췄나

| 항목 | 기준 |
|---|---|
| BRDF | GGX 분포 + 높이상관 Smith + Schlick 프레넬. Deferred의 세 함수를 그대로 옮겼다 |
| f0 · kd · alpha | `lerp(0.04, albedo, metallic)` · `(1-F)(1-metallic)` · `max(rough², 1e-3)` |
| ORM 채널 | GBuffer와 같이 `rough = orm.g × 계수`, `metallic = orm.b + 계수`(곱과 합이 다르다) |
| occlusion | **안 쓴다.** Deferred가 `.gb`만 읽으므로 여기서만 적용하면 두 경로가 갈린다 |
| 노멀맵 | GBuffer와 같은 그람-슈미트 + 저장된 종법선의 핸디드니스 |
| IBL | Deferred와 같은 split-sum. 라이브 배선이 같은 자원을 넘긴다 |

손으로 옮긴 식이라 **고칠 때는 두 곳을 함께 고쳐야 한다**(셰이더에 그렇게
적어 뒀다).

### 뒷면이 앞면을 덮던 문제

알파 1로 두고 불투명과 대조했더니 구 두 개에만 검은 얼룩이 있었다.

- 노멀맵을 꺼도 남았다 → 탄젠트 프레임이 아니다
- 프로브로 재질을 보니 금속성 0, IBL 켜짐 → 재질도 IBL도 아니다
- 범인은 **후면 컬링**이었다. GBuffer는 `CULL_MODE_NONE`인데도 멀쩡한데,
  깊이를 쓰기 때문에 앞면이 뒷면을 깊이 테스트로 막는다. 투명은 깊이를
  쓰지 않으므로(12장 ③) 그 보호가 없어, 닫힌 물체의 뒷면이 앞면 위에
  덧그려져 법선이 반대인 어두운 얼룩이 됐다.

`CULL_MODE_BACK`으로 바꿨다. **양면 투명(잎사귀·천)은 뒷면→앞면 2패스가
정석이지만 재질이 '양면인가'를 알려 줘야 성립하고, 엔진에 그 개념이 아직
없다**(`doubleSided` 검색 0건). 그래서 흔한 기본값(닫힌 물체)을 택했다 —
양면 재질이 생기면 그때 플래그와 2패스를 함께 넣을 것.

### 리뷰에서 잡힌 것

- **IBL 디스크립터 할당 실패 시 루트 파라미터 6이 안 걸린 채 그렸다.**
  `SetGraphicsRootSignature`가 직전에 모든 루트 인자를 무효화하므로,
  테이블을 안 걸고 드로우로 넘어가면 미초기화 루트 인자가 된다(하드웨어는
  쓰레기 디스크립터 주소를 읽는다). 같은 함수의 다른 링 할당들처럼
  실패 시 접도록 고쳤다.
- `ShadeParams`에도 `static_assert`를 붙였다(`ShadeInstance`만 있었다).

### 남은 차이 (알파 1 대조에서 확인)

투명이 불투명보다 약간 밝고 대비가 낮다. **Deferred는 SSGI와 캐스케이드
그림자를 받고 Forward+는 안 받기 때문**이고, 구조에서 오는 차이다.
그림자는 §15에서 닫았고, SSGI가 남는다.

디스크립터 링 소비가 드로우당 1 → 4, 프레임당 +3으로 늘었다. 4096을 전
패스가 나눠 쓰므로 투명이 많은 씬에서는 `overflows`를 볼 것.

### 후속 과제 (리뷰에서 확인, 이번 범위 밖)

- **카메라 포인터 ABA**: 파괴된 카메라와 같은 주소로 새 카메라가 할당되면
  뷰를 오인 계승한다(비교만 하고 역참조는 없어 크래시는 아님, 한두 틱 잔상).
  세대 ID 또는 파괴 훅으로 해결 가능.
- **draws 수집 중복**(§6에 기왕 기록): CaptureFromCamera가 카메라 수만큼
  프록시 스냅샷을 통째로 순회한다. "수집 1회 + cameraSnapshot만 카메라별"
  분리가 다음 단계.
- **게임뷰의 에디터 오버레이**: 그리드·기즈모 체인이 두 뷰 모두에 그려진다.
  게임 카메라 뷰에서는 빼는 분기가 필요할 수 있다(그래프 선언 분기 +
  GetOutput 핸들 신선도 주의).

## 15. 투명에 캐스케이드 그림자 (2026-08-07)

§14가 남긴 차이 둘 중 하나를 닫는다. Deferred가 이미 받는 그림자 자원을
Forward+에도 같은 형태로 넘긴다.

### 배선

- `EnhancedForwardPass::SetShadow(RGHandle, const EnhancedShadowData&)` —
  Deferred와 같은 서명. 라이브 렌더러가 `p.deferred.SetShadow` 바로 옆에서
  같은 값을 준다.
- 셰이더: `Texture2DArray gShadowMap : register(t11)`,
  `SamplerComparisonState gShadowSampler : register(s2)`,
  `SampleShadow`/`SampleShadowCascade`는 Deferred에서 그대로 옮겼다.
  방향광 분기에서만 `falloff`에 곱한다(Deferred와 같은 자리).
- 루트 시그니처: IBL 테이블(파라미터 6)을 t8~t10 → t8~t11로 넓혀
  '프레임 내내 같은 텍스처' 하나로 묶었다. 샘플러 범위는 2 → 3.
- `ShadeParams`가 112 → 368바이트. `static_assert`도 함께 고쳤다.

`m_shadowMap`이 무효면 그래프 사용 선언에서 빠지고 `gHasShadow=0`으로
돈다 — 자가 검증 경로(그림자 패스 없음)가 그대로 통과하는 이유다.

### 받는 쪽만이다

투명은 그림자 맵에 **그려지지 않는다**(`EnhancedShadowPass`가
`context.draws`만 래스터한다). 그래서 자기 그림자도, 남에게 드리우는
그림자도 없다. 유리에 필요한 것은 어차피 다른 계산이므로 그 자리에
불투명 캐스터를 밀어 넣는 것은 답이 아니다.

### 검증

`FT_Shadow`(불투명 바닥 + 캐스터 다섯)를 복사해 바닥만
`m_renderingMode: 1`로 바꾼 `FT_TransparentShadow`를 만들고, 태양을
25도로 낮춘 `*Low` 변형을 함께 썼다(그림자가 옆으로 길게 뻗어 보인다).
`Assets/Scenes/`는 gitignore라 재생성 절차를 여기 남긴다.

`p.forward.SetShadow`만 껐다 켠 A/B에서 게임 뷰 431880픽셀 중 **2289개
(0.53%)가 갈렸고**, 차이 이미지가 캐스터 실루엣과 오른쪽으로 뻗은 그림자
줄기 그대로였다. 불투명 바닥(Deferred)과 그늘 자리도 일치한다.

회귀: `dx12.forward` / `dx12.forwardshade` / `dx12.fog` / `dx12.ssgi` /
`dx12.selftest` / `dx12.shadowquality` 모두 통과.

### 검증 중에 드러난 것 (이번 범위 밖)

**라이브 경로에서 방향광 세기가 제곱으로 들어간다.**
`LightComponent::ApplyLightData`가 `light.m_color = m_color * m_intencity`로
세기를 색에 이미 곱해 넣는데, `EnhancedSceneRendererLive`가
`light.color.w = source.m_intencity`로 세기를 한 번 더 싣고 두 셰이더
(Deferred·Forward+)가 `color.rgb * color.a`를 한다. 세기 1.6이면 2.56배다.
직사광이 날아가 톤맵이 눌러 버리므로 **그림자가 옅은 색조로만 보인다** —
그림자가 안 걸린 것처럼 보였던 것이 이 때문이다. 고치면 모든 씬의 밝기가
바뀌므로 따로 판단할 것.
