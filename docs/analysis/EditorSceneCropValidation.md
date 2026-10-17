# Scene 캔버스 좌표와 crop 호환 규칙

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 Scene은 fill이다

파일 이름의 crop은 뷰포트가 전체 창 크기의 렌더 타깃을 잘라 보여 주던 시기를 가리킨다. 현재 Scene은 캔버스 extent와 render scale에 맞춘 타깃을 `fill`로 펼친다. Game은 출력 종횡비를 보존하는 `letterbox`다. `crop`은 공유 계산 함수의 지원 정책으로 남아 있지만 현재 Scene 기본 정책을 설명하는 이름이 아니다. 배율을 0.5로 낮춘 Scene에 crop을 강제하면 이미지를 절반 크기로 놓고 가장자리를 비우는 잘못된 동작이 생긴다.

## 한 좌표 정본

`LayoutViewportCanvas(fit, contentMin, contentSize, sourcePixels, framebufferScale)`가 content/image/clip과 UV를 계산한다. 모두 같은 프레임의 화면 좌표 min/max이며 extent는 max-min이다. content 원점은 창 프레임이나 제목표시줄이 아니라 실제 content region이다. 소비자가 제목표시줄 높이를 더하거나 별도의 viewport 사각형을 다시 만들지 않는다.

- fill: image 크기는 content 크기다. 이 캔버스를 위해 생산된 소스를 전체에 편다
- crop: image 크기는 sourcePixels/framebufferScale이며 중앙 정렬한다. 확대하지 않고 넘치는 부분을 자른다
- letterbox: 소스 종횡비를 지키는 최대 크기로 중앙 정렬한다

clip은 image와 content의 교집합이고 UV는 전체 image를 기준으로 환산한다. `ClipContains`는 오른쪽/아래 경계를 제외하는 반열린 범위다. 유한 양수의 content, source, framebuffer scale을 요구한다. 소스가 없더라도 유효한 content는 채우며 image/clip/UV가 없는 상태는 valid=false다. 준비 배경·도구 배치·카메라 입력이 source 준비와 함께 사라지지 않아야 한다.

## 소비자 연결

Scene의 AddImage는 clip 사각형과 그 부분의 UV를 사용한다. picking은 같은 image에서 SourceUV를 얻어 NDC로 바꾸며 ImGuizmo도 image 사각형을 받는다. ImGuizmo가 내부에서 비교차 clip을 밀 수 있어 이후 draw command의 clip을 RAII scope로 캔버스 가시 영역에 다시 제한한다. 기즈모가 Hierarchy/Inspector/탭을 덮지 않는 것은 좌표 정렬과 별개의 clip 계약이다.

카메라 이동은 아직 표시 이미지가 없을 때 content를 사용해 계속 입력을 받을 수 있다. 반면 선택/기즈모 편집은 valid canvas, 실제 texture, ready 결과, 현재 render Scene epoch가 맞아야 한다. 렌더러가 준비 중이라는 이유로 다음 카메라 snapshot을 만들 입력까지 막지 않는다.

## 검증 방법과 제한

`verify-scene-viewport-canvas-contract.ps1`은 좌표식·title 보정·입력 호출 지점의 소스 검사와 실제 창 이동 후 상대 좌표 비교를 함께 가진다. `verify-editor-viewport-extent.ps1`은 scale off/auto/fixed, resize 반복, 실제 canvas/target extent 수렴과 표시 상태를 검사한다. 둘 다 이 개정에서는 실행하지 않았다.

필요한 UI 수용은 창 원점 이동, DPI 변화, 작은 패널, 0.5 scale, source 준비 중 배경, 가장자리 picking과 화면 밖 gizmo다. Scene fill 상태에서 content와 image가 우연히 같아지는 사례만으로 crop/letterbox 공통 계산을 입증하지 않는다. 캡처 이미지는 해당 프레임의 정책과 입력 치수를 함께 기록해야 재사용할 수 있다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorViewportCanvas.h](../../Editor/EngineGUIWindow/EditorViewportCanvas.h)
- [Editor/EngineGUIWindow/SceneViewWindow.cpp](../../Editor/EngineGUIWindow/SceneViewWindow.cpp)
- [Editor/EngineGUIWindow/GameViewWindow.cpp](../../Editor/EngineGUIWindow/GameViewWindow.cpp)
- [Editor/EngineGUIWindow/ViewportHostWindow.cpp](../../Editor/EngineGUIWindow/ViewportHostWindow.cpp)
- [Tools/regression/verify-scene-viewport-canvas-contract.ps1](../../Tools/regression/verify-scene-viewport-canvas-contract.ps1)
- [Tools/regression/verify-editor-viewport-extent.ps1](../../Tools/regression/verify-editor-viewport-extent.ps1)
