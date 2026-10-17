# Scene 오버레이: 배치·입력 소유·GPU 표기

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 배치와 수명

`SceneViewportOverlay`는 Scene 창의 툴바, 방향 기즈모, 보기 옵션, 통계를 그린다. 캔버스의 content를 배치 정본으로 삼고 image 크기나 창 title 높이로 별도 원점을 만들지 않는다. 소스 텍스처가 아직 없더라도 도구는 받은 content 안에 있어야 한다. 위젯을 배치한 뒤 이전 cursor를 복원하고 ImGui 1.92 layout item을 마무리하는 `Dummy(0,0)`를 호출한다.

방향 기즈모는 vendored ImViewGuizmo를 사용한다. 출처·변경 경계는 [PROVENANCE](../../ThirdParty/ImViewGuizmo/PROVENANCE.md)를 따른다. 현재 orbit pivot과 camera pose를 연결하며 연속 회전 및 축 snap animation은 `EditorCameraRig::SetPose`로 전달한다. 팝업·활성 item·window focus를 확인하는 wrapper가 필요하다. draw-list 위젯 자체가 ImGui item hit test를 대신 제공한다고 가정하지 않는다.

## 입력을 가로채는 규칙

`blocksPointer`와 `blocksShortcuts`는 다른 신호다. 툴바 위, popup, 방향 기즈모 사용 중에는 pointer를 차단한다. 툴바/popup에서 시작한 left/right press는 버튼을 놓을 때까지 소유권을 유지해 드래그 중 아래 canvas의 picking·RMB 카메라 이동으로 새지 않게 한다. pointer가 툴바 경계를 벗어났다는 이유만으로 즉시 양도하지 않는다.

Q/W/E/R는 작업 모드 선택, T는 현재 transform 모드의 snap 토글이다. popup, text input, 방향 기즈모 사용이 없고 Scene window가 focus되며 Ctrl과 RMB가 눌리지 않았을 때만 처리한다. 같은 키의 문자 입력·카메라 이동과 경쟁시키지 않는다. SceneView는 이 신호와 canvas 범위를 함께 보고 카메라·picking·변형 입력을 결정한다.

## 통계는 현재 표시 결과에 묶는다

Scene 업데이트 빈도는 display promotion count 차이를 시간으로 나눈 값이다. 전체 게임 FPS나 OS Present 빈도라고 부르지 않는다. 통계는 실제로 획득한 texture와 함께 반환된 `displayed.frame`을 사용한다. 텍스처와 metadata를 따로 조회해 서로 다른 frame의 값을 조합하지 않는다.

GPU debug snapshot cache는 약 0.25초 간격으로 갱신한다. GPU sample이 editor view에 속하고 frame/submission ID가 유효하며 현재 표시 frame보다 미래가 아니고 pass timing이 있으며 temporal provenance가 실제 표시 view·extent와 맞을 때만 수치를 표시한다. 이 값은 **sampled pass sum**이지 GPU 전체 frame time이 아니다. render/display extent, real frame ID, 현재 표시 frame과의 차이를 publications 단위 age로 함께 적는다. 조건이 안 맞으면 unavailable이다.

통계 background/text는 캔버스 clip 안에 그리는 비대화형 오버레이다. 별도 FPS 표시는 showFps가 켜지고 statistics가 꺼져 있을 때만 나온다. Scene skybox 표시 옵션과 전역 environment background/IBL 생성 정책을 구분해야 한다. 한 view의 숨김을 전체 환경 자산 재생성으로 설명하지 않는다.

## 수용해야 할 실제 조작

작은/넓은 canvas, 원점 이동, DPI/user scale, source 준비 중 도구, popup 열린 상태의 Q/W/E/R/T, 툴바에서 시작해 canvas로 나가는 left/right drag, 방향 snap 중 focus 상실을 확인한다. 렌더링 표면 runner는 일부 상태 토글과 capture를 자동화하지만 실제 포인터 소유권 전체를 대신하지 않는다. GPU 기록을 멈춘 상태·다른 view의 최신 sample·resize 뒤 stale sample에서는 unavailable 또는 명시된 과거 sample age가 정확해야 한다.

이번 개정에서는 구현과 회귀 입력을 읽었으며 새 UI 캡처나 GPU 측정은 없다. 과거 버튼 배치 캡처를 현재 스케일·프레임 provenance의 증거로 쓰지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/SceneViewportOverlay.h](../../Editor/EngineGUIWindow/SceneViewportOverlay.h)
- [Editor/EngineGUIWindow/SceneViewportOverlay.cpp](../../Editor/EngineGUIWindow/SceneViewportOverlay.cpp)
- [Editor/EngineGUIWindow/SceneViewWindow.cpp](../../Editor/EngineGUIWindow/SceneViewWindow.cpp)
- [Editor/EngineGUIWindow/EditorViewportCanvas.h](../../Editor/EngineGUIWindow/EditorViewportCanvas.h)
- [Editor/EngineGUIWindow/EditorCameraRig.cpp](../../Editor/EngineGUIWindow/EditorCameraRig.cpp)
- [ThirdParty/ImViewGuizmo/PROVENANCE.md](../../ThirdParty/ImViewGuizmo/PROVENANCE.md)
- [Tools/regression/verify-scene-viewport-canvas-contract.ps1](../../Tools/regression/verify-scene-viewport-canvas-contract.ps1)
- [Tools/regression/verify-editor-rendering-surfaces.ps1](../../Tools/regression/verify-editor-rendering-surfaces.ps1)
