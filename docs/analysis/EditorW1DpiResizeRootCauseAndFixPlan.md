# PHASE 21 W1 — DPI·리사이즈 표시 오류 원인 분석과 해결안

2026-09-12, HEAD `3efbb23f`와 기존 W1 변경이 포함된 작업 트리 기준.
[정본 W1](../plans/EditorWorkspaceRedesignPlan.md) ·
[실제 재현·복원 기록](EditorW1InteractiveValidation.md).

**후속 수정(2026-09-12):** 사용자 요청으로 DX12를 먼저 수정했다.
전체 패스 재초기화를 크기 자원 교체로 바꾼 뒤 Debug/Release의 실제 DPI 왕복과 각 10회 resize가 통과했다.
수정 원인·계측값·검증 범위는 [EditorW1Dx12ResizeValidation.md](EditorW1Dx12ResizeValidation.md)에 둔다.
아래 본문은 수정 전 분석 기록이며, Vulkan은 미수정으로 남는다.
후속 사용자 결정으로 Vulkan 대응은 우선 보류하고 **W1은 DX12 기준 완료**로 정리했다.
아래 Vulkan 원인 가설과 수정·재검증 항목은 재개 시 이어갈 보류 기록으로 유지한다.

## 결론과 확정 범위

두 오류의 공통 조사 대상은 **창 크기 변경을 렌더·표시 자원에 전달하는 과정**이다.
현재 코드에서 수정할 결함은 확인했지만, 당시 로그에는 표시 ID의 실패 이유와 실제 swapchain 크기가 없어
각 현상의 최종 원인을 하나로 확정할 수는 없다. 아래에서 소스상 결함과 재현 원인 가설을 구분한다.
이번 변경은 분석·해결 계획 문서화이며 제품 코드 수정, 새 빌드, 재실행 검증은 수행하지 않았다. W1은 `progress`다.

| 문제 | 확인된 동작·결함 | 재현 원인 판단 | 해결 방향 |
|---|---|---|---|
| DX12: DPI 축소 뒤 Scene이 검게 남다가 복구 | resize 시 기존 표시 결과를 무효화하고 새 GPU 완료 결과를 기다린다. Scene의 표시 ID가 0이면 준비 배경을 그린다. | 관찰된 검은 배경과 일치하는 경로다. 다만 당시 ID/ready/key·단계별 시간이 없어 지연을 만든 분기는 미확정이다. | 크기 전달 동기화, 표시 중단 이유·시간 계측, 첫 새 결과의 게시/소비 보장, 필요 시 마지막 완료 영상의 안전한 유지 |
| Vulkan: 모니터 경계에서 축소 후 `DEVICE_LOST` | 실제 swapchain extent와 요청 크기를 별도로 사용한다. acquire 실패 뒤 열린 내부 프레임을 되돌리지 않는 경로가 있다. fence wait의 device loss가 지속 오류 상태로 전파되지 않는다. | **실제 이미지보다 큰 render area를 제출했을 가능성**을 우선 검증한다. 당시 extent/VUID가 없어 장치 손실의 직접 원인으로 확정하지 않는다. | 실제 extent로 렌더링, 재생성/프레임 취소 상태 처리, device loss 최초 오류 보존 및 제출 중단 |

공통으로 `ScreenResizeBus`의 크기 필드를 표시 스레드가 쓰고 게임 스레드가 잠금 없이 읽는 결함도 확인했다.
이는 수정 대상이지만, 두 증상을 모두 설명하는 단일 원인으로 판정하지 않는다.
Material Symbols 글리프 누락이나 이전 `GlyphExcludeRanges` assertion이 다시 발생했다는 증거는 없다.

## 1. 공통 문제 — 서로 다른 시점의 크기와 동기화되지 않은 공유 값

현재 호출 경로는 다음과 같다.

1. 창 소유 스레드의 `EditorWindowChrome::HandleWindowMessage`가 `WM_DPICHANGED`의 제안 RECT를 즉시 적용한다.
2. `WM_SIZE`는 `EditorMain::InvokeResizeFlag()`의 atomic flag로 전달된다.
3. 표시 스레드는 `HandleWindowResize()`에서 client 크기를 읽고 `ScreenResizeBus::BroadcastResize()`를 호출한다.
4. 게임 스레드는 `BuildLiveFramePacket()`에서 버스의 `GetWidth()`와 `GetHeight()`를 따로 읽어 렌더 스레드에 보낸다.
5. ImGui host는 별도로 매 표시 프레임 `GetClientRect()`를 읽어 셸의 `Resize()`를 호출한다.

소스 근거:

- `Editor/EngineGUIWindow/EditorWindowChrome.cpp:70`: DPI 변경의 제안 RECT 적용.
- `Editor/EngineEntry/EditorMain.cpp:342`, `:380`, `:492`: 표시 스레드에서 resize 처리.
- `Engine/RenderEngine/RHI/ScreenSizedResource.h:163`, `:189`, `:217`: 일반 `uint32_t` 크기 필드의 무잠금 읽기/쓰기.
  `m_mutex`는 구독 목록을 보호하며 크기 값을 보호하지 않는다.
- `Editor/EngineEntry/App.cpp:311` → `Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:4419`:
  게임 스레드가 두 크기를 개별 조회한다.
- `Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp:110`: 셸의 별도 크기 조회.

따라서 코드상 데이터 경쟁이 있고, width/height가 하나의 관측값이라는 보장도 없다.
atomic resize flag는 요청 전달을 보호하지만 이후의 공유 크기 읽기/쓰기를 모두 보호하지는 않는다.
또한 버스 값을 동기화해도 Windows가 그 뒤 크기를 바꿀 수 있으므로 **실제 GPU 이미지 크기의 별도 확인은 필요하다**.

해결안:

- 우선 `ScreenResizeBus`에 `{width, height, generation}`을 한 번에 읽고 쓰는 snapshot API를 둔다.
  짧은 mutex 구간에서 값 전체를 복사하고 콜백은 잠금 밖에서 호출한다. 두 필드만 각각 atomic으로 바꾸는 것으로 끝내지 않는다.
- 프레임 패킷은 snapshot을 한 번만 읽는다. 같은 크기의 중복 이벤트는 generation을 올리지 않고,
  전환 중 여러 요청은 안전한 프레임 경계에서 최신 유효 크기로 합친다.
- 진단에는 창에서 관측한 크기, 렌더 프레임의 크기, 셸이 요청받은 크기, GPU가 실제 만든 크기를 구분한다.
  모두 같은 값이라고 가정해 한 필드로 덮어쓰지 않는다.
- `window.resize`는 현재 게임 스레드의 CLI Pump에서 실행된다(`App.cpp:264`).
  HTTP 작업 스레드가 직접 resize한다는 가설은 배제한다.
  `AdjustWindowRectEx` 사용과 요청/실제 크기 차이는 계측하되, `clamped=true` 자체를 GPU 오류로 취급하지 않는다.
  명령 결과에는 최종 DPI와 적용된 크기 generation도 함께 기록한다.

## 2. DX12 — 기존 표시 결과를 비운 뒤 새 결과로 복구하는 경로

### 재현 사실

- 독립 Debug 실행 두 번에서 OS DPI 150→100% 뒤 Scene만 검게 남았다. 본문·아이콘·도구 표시는 계속됐다.
- 두 번째 실행에서는 추가 클릭 없이 이후 캡처에서 복구됐다. 클릭을 복구의 필수 조건으로 보지 않는다.
- 검은 영역의 색은 Scene 준비 배경과 일치한다. 다만 캡처만으로 특정 코드 분기의 실행을 증명할 수는 없다.
- `debug-black-live-status.json`은 pipeline ready와 프레임 누적 진행을 보고했다.
  Scene의 표시 ID, 대상별 completed frame, 공유 텍스처 개방 성공은 보고하지 않았다.
  관측 간 정확한 복구 지연 시간도 측정하지 않았다.

### 소스에서 확인한 경로

1. 렌더 프레임 크기가 기존 pipeline과 달라지면 `TickLive()`가 `dx12.Resize()` 후
   `TeardownPipeline(true, true)`와 `BuildPipeline()`을 수행한다
   (`EnhancedSceneRenderer.cpp:5018`).
2. teardown/build는 `InvalidateDisplayResultsLocked()`로 `ready=false`, 표시 key 0을 게시한다
   (`:957`, `:1444`, `:1625`). 새 표시 슬롯과 크기 의존 패스를 다시 만든다.
3. 후속 틱에서 슬롯의 GPU fence 완료를 확인하고 `PublishDisplayResultLocked()`로 표시 결과를 게시한다
   (`:4934`, `:981`). 게시에는 현재 대상의 active/key 일치 조건도 있다.
4. UI의 `GetLiveDisplayImTextureId(Editor)`는 sink, enabled, active, ready, key를 확인하고
   DX12 adapter를 통해 공유 텍스처를 연다(`:5257`). ID가 0이면
   `SceneViewWindow.cpp:260`에서 `(0.08, 0.08, 0.09)` 준비 배경을 그린다.

**표시 공백이 생길 수 있는 구조는 확인했다. 길어진 공백의 원인은 아직 미확정이다.**
현재 자료만으로 재구축 소요, 크기 재변경, GPU 완료 대기, 게시 조건 불일치, UI의 결과 소비 중 어느 단계가
지연됐는지 나눌 수 없다. pipeline ready나 누적 렌더 횟수가 정상이라는 이유로 이 경로를 정상 처리해서는 안 된다.

공유 텍스처 개방도 구분해서 봐야 한다. adapter는 token을 못 찾으면 0을 반환하지만,
`ImGuiDx12Shell::OpenSharedTexture()`의 `OpenSharedHandle` 실패는 fallback texture ID를 반환한다
(`EnhancedSceneRendererLiveDX12Adapter.cpp:308`, `ImGuiDx12Shell.cpp:372`).
따라서 개방 실패가 모두 같은 검은 준비 배경으로 나타난다고 설명하면 부정확하다.

### 해결안

1. 대상별로 `resizeGeneration`, view key, active/ready, 제출·GPU 완료·표시 게시·UI 소비 frame ID,
   표시 ID가 없는 이유, 공유 핸들 개방 결과와 HRESULT를 수집한다.
   teardown/build 시작·완료와 첫 새 영상 소비 시점을 같은 시계로 기록한다.
2. 공통 크기 snapshot 수정 뒤 재현해 반복 재구축인지, 완료 결과가 게시되지 않는지 먼저 확정한다.
   새 세대 결과의 게시·UI 소비가 지연된다면 해당 단계만 수정한다.
3. resize 전환 중 마지막 완료 영상을 유지할 경우, 이전 세대의 완료된 표시 텍스처를 명시적으로 보유하고
   새 세대의 GPU 완료 영상이 열릴 때 한 번에 교체한다. 단순히 `ready=false` 처리를 삭제하지 않는다.
   기존 adapter는 active token만 열므로 이전 결과의 보유·조회 계약도 함께 수정해야 한다.
4. 이전 영상은 생산 측과 표시 측 GPU 사용이 모두 끝난 뒤 해제한다. 같은 슬롯에 다시 쓰는 것과 표시가 겹치지 않도록 한다.
   씬 교체·backend 종료·device loss에는 이전 영상을 정상 프레임처럼 유지하지 않는다.
5. 재구축 비용이 실제 병목으로 확인되면 해당 패스의 크기 의존 리소스만 줄여 재생성한다.
   현재도 backend/PSO·자산 캐시는 유지하는 경로이므로, 디바이스 전체 재부팅을 원인으로 단정하지 않는다.

표시 수명 보호용 락을 제거하거나, GPU 완료를 기다리지 않은 텍스처를 먼저 게시하는 방식은 해결안에 포함하지 않는다.
W4의 viewport별 렌더 해상도 분리는 이번 수정의 선행 조건으로 만들지 않는다.

## 3. Vulkan — 실제 extent 불일치 가능성과 실패 상태 처리 결함

### 재현 사실

1번 모니터 100%, 2번 모니터 150% 경계에 창이 걸친 상태에서 `window.resize 960 400`을 실행했다.
실제 client는 639×296, 관측 DPI는 1.0이 됐다. `release-vulkan.out:1057`의 resize 결과 바로 다음 행에서
`Vulkan BeginFrame 실패: 펜스 대기 실패 — VK_ERROR_DEVICE_LOST`가 기록됐다.
이후 프레임 미개방 오류와 함께 반복됐으며 device-loss 문자열을 포함한 로그가 275,198행 쌓였다.

최초 오류는 ImGui Vulkan 셸의 `VulkanDeviceResources::WaitForFenceValue()` → `vkWaitSemaphores()`에서
관측됐다(`VulkanDeviceResources.cpp:1226`). **이 함수는 장치 손실을 발견한 지점이지 GPU를 손상시킨 명령의 증거가 아니다.**
Release 실행은 셸의 validation 활성화 기본값이 false다(`ImGuiVulkanShell.cpp:259`).
따라서 원인 VUID가 없다는 사실도 GPU 사용 규약이 지켜졌다는 증거가 아니다.

### A. 최우선 원인 가설 — 요청 크기로 실제 이미지 범위를 넘겨 그리기

- `CreateSwapChainInternal()`은 surface의 `caps.currentExtent`가 지정되어 있으면 요청 크기 대신 그 값으로
  이미지를 만든다(`VulkanDeviceResources.cpp:1402`). 하지만 그 선택된 extent를 표시용 크기로 저장하지 않는다.
- `ResizeSwapChain()`은 재생성 후 `m_width/m_height`에 **요청값**을 넣는다(`:1503`).
  셸도 `impl.width/height`에 **요청값**을 넣는다(`ImGuiVulkanShell.cpp:368`). 초기 생성 경로도 같은 점검 대상이다.
- `RenderAndPresent()`는 그 셸 값을 `VkRenderingInfo::renderArea.extent`에 사용한다(`:456`).

예를 들어 client 크기를 읽은 뒤 DPI 전환이 추가로 일어나 surface가 더 작아지면,
이미지는 작은 실제 크기로 생성되지만 render area는 이전의 큰 요청값일 수 있다.
이 경우 attachment 범위를 넘기는 Vulkan 사용 규약 위반이다.
VUID `VkRenderingInfo-pNext-06079/06080`은 render area가 attachment의 너비·높이 안에 있어야 한다고 규정한다.
[Khronos VkRenderingInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderingInfo.html)

**코드의 크기 전달 결함은 확정, 이번 device loss가 이 경로에서 발생했는지는 미확정**이다.
639×296은 명령이 보고한 client 크기이며 당시 GPU 이미지 크기를 측정한 값이 아니다.
재현 시 요청 extent, surface extent, 생성 extent, render area를 한 기록에 묶고 위 VUID를 확인해야 한다.

수정은 다음과 같이 한다.

- 실제 생성 extent를 `VulkanDeviceResources`가 보유·반환하고 셸의 render area와 백버퍼 관련 계산에 사용한다.
- 요청한 창 크기와 실제 extent는 별도 필드로 둔다. 두 값이 다르다는 이유만으로 매 프레임 같은 재생성을 반복하지 않는다.
- surface가 자유 extent를 허용하는 경우 min/max 범위로 제한하고, 0 크기·최소화는 제출을 건너뛰는 상태로 처리한다.
- ImGui draw data의 표시 크기도 별도로 변할 수 있으므로 실제 attachment보다 큰 영역을 제출하지 않도록 확인한다.
  같은 프레임의 크기가 맞지 않으면 해당 프레임을 안전하게 취소하고 최신 크기로 다시 시작한다.

### B. 확인된 추가 결함 — 재생성과 실패 상태 전환이 불완전함

| 위치 | 현재 동작과 문제 | 수정 |
|---|---|---|
| `VulkanDeviceResources::BeginFrame:668–710` | 내부 `m_frameOpen=true`와 upload recording을 만든 뒤 acquire 실패 시 그대로 false를 반환한다. 셸은 자기 `frameOpen`을 열지 않아 둘의 상태가 달라진다. | acquire 결과를 분류하고, 실패한 recording/context·업로드 예약을 정확히 되돌린다. 이미지 미획득과 획득 후 취소를 구분한다. |
| `BeginFrame:707`, `Present:1534` | `OUT_OF_DATE`를 일반 실패로 전달하며 `SUBOPTIMAL`은 성공으로만 처리한다. API 결과에서 재생성을 요청하는 명시적 경로가 없다. | `Ready / RecreateNeeded / Suspended / DeviceLost` 등의 명시적 결과를 셸까지 전달한다. OUT_OF_DATE는 안전한 재생성으로, SUBOPTIMAL은 프레임 종료 뒤 재생성 판단으로 연결한다. |
| `WaitForFenceValue:1231` | device loss에 오류 문자열만 반환한다. submit/present/drain에 있는 `MarkUnrecoverableDeviceError` 처리가 이 경로에는 없다. | 최초 device loss를 공통 terminal 상태에 기록하고 이후 GPU 제출·일반 resize 재시도를 중단한다. |
| `ImGuiVulkanShell::Resize:359–364` | 이전 view를 없앤 뒤 재생성이 실패해도 셸이 active 상태로 남는다. | 완성된 swapchain/view/extent를 함께 반영하고 실패 시 rendering을 시작하지 않는다. 일반 재생성 실패와 device loss를 구분한다. |
| `ImGuiVulkanShell::NewFrame:389`, `ImGuiHost::EndFrame:137` | BeginFrame 실패 뒤에도 UI가 계속 끝나며 프레임 미개방 오류를 매번 출력한다. | 프레임 시작 결과를 host가 받도록 하고 미개방 프레임의 렌더를 건너뛴다. 최초 오류와 발생 횟수를 보존하고 중복 로그를 제한한다. |

Vulkan은 창 이벤트뿐 아니라 acquire/present 결과에서도 swapchain 재생성이 필요해질 수 있다.
[Khronos swapchain 재생성 안내](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/04_Swap_chain_recreation.html)

기존 `DrainForLifecycle()`에는 제출 스레드를 경유한 `vkDeviceWaitIdle`이 있고,
표시 semaphore도 swapchain image별로 관리한다. 따라서 현재 원인을 단순히 “GPU 대기가 없음” 또는
“모든 semaphore를 프레임 번호로 잘못 재사용함”이라고 설명하지 않는다.
정상 장치에서의 취소는 기존 `AbortFrame()` 계약을 검토해 재사용하되,
장치가 이미 손실된 상태에서 acquire 반환을 위해 추가 submit/present를 시도하지 않는다.
완전한 device 재생성·자동 복구는 별도 범위이며 이번 우선 목표는 원인 제거와 명확한 실패 전환이다.

### C. 직접 원인으로 확정하지 않은 항목

- ImGui Vulkan backend의 동적 폰트 업로드는 별도 queue submit/wait를 한다.
  그러나 셸의 `Present()`는 제출 스레드 완료를 기다리며 씬 렌더러는 별도의 device resources를 소유한다.
  두 스레드가 있다는 사실만으로 같은 VkQueue의 동시 접근이라고 단정할 수 없다.
  extent 수정 뒤에도 재현되면 device/queue 식별자, 업로드·submit·present 순서와 validation을 확인한다.
- 폰트/DPI 값이 정상이라고 GPU upload를 모두 배제하지도 않는다. 글리프 최초 생성과 이미 생성된 글리프 사용을 분리해 시험한다.
- OS DPI만 바꾼 Vulkan 구간은 정상 관찰됐고, 손실 직전 직접 동작은 경계에서의 창 축소였다.
  DPI 변경, 창 resize, 모니터 이동을 분리하지 않은 테스트로 원인을 확정하지 않는다.

## 4. 구현 순서와 통과 기준

| 순서 | 범위 | 완료 증거 |
|---|---|---|
| R0 | 양쪽 backend의 표시 계측, 최초 실패 보존, 제품 서비스에서 호출 가능한 읽기 전용 진단 추가 | 각 target의 첫 새 영상, 마지막 GPU 완료·present, 크기 세대, 실패 이유를 한 타임라인으로 읽을 수 있음 |
| R1 | `ScreenResizeBus` 크기 snapshot과 생산/표시 경계 수정 | 동시 크기 변경에도 width/height/generation이 같은 묶음, 0 크기 안전 처리, 중복 크기 재구축 없음 |
| R2 | Vulkan 실제 extent 전달, acquire/present 재생성·취소 처리, terminal device-loss 상태 | 범위 초과 제출 0, 오류 주입 시 프레임 상태/자원 예약 복원, device loss 뒤 추가 제출·로그 폭주 0 |
| R3 | DX12 계측 결과에 따른 지연 원인 수정과 표시 전환 보강 | 새 세대 GPU 완료 후 늦어도 두 번의 정상 표시 프레임 안에 새 결과를 소비, 불필요한 준비 배경 없음, 이전 세대 자원 안전 해제 |
| R4 | 동일 실행 파일로 실제 DPI·경계 resize 재검증 | 아래 행렬 통과, 화면·로그·세대별 frame 진행 증거와 원복 기록 확보 |

R0부터 빌드 가능한 단위로 나눠 진행한다. R2의 device-loss 중단 처리는 원인 재현 중 로그 폭주를 막기 위해
먼저 넣을 수 있지만, 오류를 조용히 숨기는 것으로 원인 수정 완료를 대신하지 않는다.
R3의 첫 새 GPU 완료까지 걸리는 시간은 R0에서 계측하고 정상 크기 전환 기준과 비교한다.
“두 표시 프레임” 조건은 GPU 완료 후 전달 지연의 상한이며, GPU 완료 자체의 무기한 지연을 허용하는 조건이 아니다.

최소 검증 행렬:

| 분리할 조건 | 조작 | 대상 |
|---|---|---|
| 사용자 배율만 변경 | 같은 모니터·창 크기에서 100→150→100% | DX12/Vulkan, 폰트 신규 생성·재사용 |
| OS DPI만 변경 | 같은 모니터에서 150→100→150%, 자연스럽게 발생한 client 변화도 기록 | DX12/Vulkan, 사용자 배율 100/150% |
| 창 크기만 변경 | 같은 DPI에서 확대·축소·연속 resize | DX12/Vulkan |
| 모니터 경계만 이동 | 100/150% 모니터 왕복, 추가 resize 없이 관찰 | DX12/Vulkan |
| 기존 실패 조건 | 경계에서 `window.resize 960 400`, 최종 client/DPI/extent 기록 | Vulkan 우선, DX12 대조 |
| 별도 실패 조건 | 최소화·복원, 0 extent, OUT_OF_DATE, 재생성 실패, device loss 주입 | 상태·수명 검사 후 양 backend 실제 화면 확인 |

- Debug validation 실행으로 원인 규약 위반을 찾고 Release에서 같은 실제 조작의 재발 여부를 확인한다.
  실제 경계 왕복은 수정 후 우선 10회 반복하고 UI 추가 클릭 없이 새 프레임이 진행되는지 기록한다.
- 작은 고정 씬에서 세대별 frame ID와 시각적 변화가 함께 보이는 증거를 남긴다.
  `Present` 성공 응답만으로 화면에 새 영상이 보였다고 판정하지 않는다.
- `editor.theme clean`은 폰트·geometry 검사 의미를 유지한다. R0에서 추가할 표시 건강 상태와 함께 판정한다.
  현재 제품 서비스에 없는 `render.livecheck`를 검증 절차의 전제로 삼지 않는다.
- device loss/프레임 실패/관련 validation 오류 0, Scene의 새 영상 표시 확인, 폰트 59역할 누락 0을 모두 충족해야 W1 완료로 바꾼다.
- Windows 배율·설정·ini를 원본으로 복원한다. W2-V/W2-B의 배치 수정과 W7 비동기 썸네일 구현은 별도 계획을 유지한다.

## 근거 파일

- `Artifacts/phase21-w1-final-visual/debug-dx12-dpi100-black-scene.png`
- `Artifacts/phase21-w1-final-visual/debug-repeat-user150-dpi100-black-scene.png`
- `Artifacts/phase21-w1-final-visual/debug-repeat-user150-dpi100-recovered.png`
- `Artifacts/phase21-w1-final-visual/debug-black-live-status.json`
- `Artifacts/phase21-w1-final-visual/release-vulkan-monitor2-transition.json`
- `Artifacts/phase21-w1-final-visual/release-vulkan-monitor-return-resize.json`
- `Artifacts/phase21-w1-final-visual/release-vulkan.out:1057`
- `Artifacts/phase21-w1-final-visual/render-log-review.json`

위 산출물은 기존 검증의 로컬 증거이며 이번 문서 작성 중 새로 실행해 얻은 결과가 아니다.
