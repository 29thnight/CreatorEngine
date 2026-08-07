# 카메라별 표시 슬롯 도입 계획 (씬뷰/게임뷰 동시 표시)

> 인수인계 메모. 2026-08-07 작성. 착수 전 이 문서를 먼저 읽는다.

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

`RenderEngine/RHI/DX12/EnhancedSceneRendererLive.cpp`

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

> **주의(기존 결함).** 포그가 받는 색에 forward 결과는 들어 있지 않다.
> forward는 자기 타깃을 새로 만들어 거기 그리는데(`m_output`) 라이브 배선이
> 그것을 아무도 소비하지 않는다 — **투명 물체가 화면에 안 나온다.** 포그
> 이전부터 있던 문제이고 별도 과제다. 포그를 forward 뒤에 두는 것은 그것이
> 고쳐지면 자연히 맞기 위해서다.

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
