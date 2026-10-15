# 실제 DPI·창 조작 수용 절차

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 자동 검사로 대신할 수 없는 축

`editor.theme`와 selftest는 현재 token/font/scale 및 합성 입력을 관측한다. Windows 설정에서 배율을 바꾸거나 DPI가 다른 모니터로 창을 옮기는 행위 자체를 재현하지 않는다. `window.resize`도 원하는 client 크기를 요청할 뿐 모니터 경계와 native hit-test 전부를 검사하지 않는다. 이 문서는 실제 OS 조작이 필요한 수용 항목과 증거 최소치를 정의한다.

현재 대상은 DX12 Editor다. 과거 보고서의 Vulkan Editor device loss는 당시 역사적 실패이며 지금 지원하지 않는 UI backend를 재선택하는 절차로 남기지 않는다. Vulkan의 별도 수용은 native probe/Player 담당 경계에서 수행한다.

## 준비와 소유권

사용할 configuration/commit/executable, GPU·driver·모니터 rect/DPI, workspace/preset, user scale, render scale을 기록한다. 실측을 위해 Windows 설정·프로젝트 설정·작업공간을 바꿔야 한다면 그 조작 권한을 먼저 확보한다. 기존 Editor와 사용자 문서는 건드리지 않고 검사 소유 process/출력 폴더를 식별한다. 가능하면 workspace override를 사용하고, 변경하는 파일은 존재 여부와 바이트/해시를 보존한다.

검사 종료와 실패 경로 모두 Windows 배율 및 변경 파일을 원래 값으로 복원하고 실제 바이트/상태를 확인한다. process가 종료됐다는 사실은 설정이 복구됐다는 증거가 아니다. 실행 결과·로그·스크린샷에 환경 복원 결과를 함께 묶는다.

## 분리해서 조작할 행렬

1. 같은 모니터와 DPI에서 user scale만 왕복한다. 메뉴/탭/Inspector/Hierarchy/tooltip, 한글·아이콘, 입력 hit area와 원래 크기 복귀를 확인한다
2. user scale 고정 상태에서 OS DPI를 바꾸거나 DPI가 다른 모니터로 창을 이동한다. suggested RECT 적용, actual client, font scale, title minimum과 canvas extent를 확인한다
3. 모니터 경계에 창을 둔 상태에서 축소·확대한다. 요청 크기와 실제 크기 차이를 기록하고 clamp 자체를 GPU 실패로 판정하지 않는다
4. native 상단/corner drag resize, maximize/restore, minimize/restore, title drag 및 메뉴 클릭을 시험한다
5. render scale off/auto/fixed와 Scene/GamePreview를 나누어 새 generation의 이미지가 소비되는지 확인한다

각 단계는 UI frame 진행, 렌더 제출/완료, 실제 소비 texture/frame/scene epoch/resize generation을 연결한다. 화면에 준비 배경이 나타나면 unavailable 이유와 구간 길이를 남긴다. 렌더러 ready 또는 프레임 누계만으로 통과시키지 않는다. 다른 input 없이 복구된 경우 “클릭이 복구 원인”이라고 추정하지 않는다.

## 성공·실패 기록

통과에는 font/style 누적 drift 없음, 잘못된 좌표·hit area 없음, 정상 표시 전환, 장치 손실/반복 frame 실패 없음, orderly shutdown 및 복원 증거가 필요하다. 실패 시 최초 오류와 직전 조작, 모든 extent/generation을 보존한다. 수십만 줄의 반복 오류 수만 남기는 것보다 최초 실패 문맥이 중요하다.

옛 실행의 `debug-*-black-scene.png`, `render-log-review.json`, `restore-proof.json` 같은 이름은 재현 자료의 위치 안내였고 이번 checkout의 새 산출물이 아니다. 그 실행에서는 DX12 장시간 공백과 별도 Vulkan 손실이 보고되었고 뒤의 DX12 수정 결과는 [후속 검사 문서](EditorW1Dx12ResizeValidation.md)에서 역사 값으로 구분했다. 이번 개정에서는 실제 OS 설정 변경·창 조작·빌드·프로세스 실행을 하지 않았다.

## 남겨 둔 요구

단일 화면 캡처의 배치 정상, CPU font 검사 성공, 종료 코드 하나를 종합 수용으로 대체하지 않는다. 단일 DPI 기계에서 실제 다중 DPI 항목을 실행하지 못했다면 미실행으로 남긴다. 현재 지원 범위와 사용 환경에서 반복 가능한 자료를 확보한 뒤에만 상태를 갱신한다.

## 구현 근거

- [Editor/EngineGUIWindow/EditorTheme.cpp](../../Editor/EngineGUIWindow/EditorTheme.cpp)
- [Editor/EngineGUIWindow/EditorWindowChrome.cpp](../../Editor/EngineGUIWindow/EditorWindowChrome.cpp)
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp](../../Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp)
- [Editor/EngineGUIWindow/ViewportHostWindow.cpp](../../Editor/EngineGUIWindow/ViewportHostWindow.cpp)
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Tools/regression/verify-editor-theme.ps1](../../Tools/regression/verify-editor-theme.ps1)
- [Tools/regression/verify-editor-viewport-extent.ps1](../../Tools/regression/verify-editor-viewport-extent.ps1)
