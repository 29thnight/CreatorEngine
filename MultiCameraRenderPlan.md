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
