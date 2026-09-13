# Editor 제목 표시줄 정리 검증 (2026-09-12)

## 구현 범위

- 사용자 최종 결정: 재생 컨트롤은 제목 표시줄의 최소화 버튼 앞에 한 박스로 표시한다.
- 상단 순서: 엔진 아이콘, File/Edit/Settings/Tools/Window/Help, 가운데 제목, 재생 박스, 창 버튼 셋.
- 제목 행 높이 24 → 20 logical px, 글꼴 16 → 12px. 별도 24px 재생 행을 제거해 상단 예약 높이는 48 → 20px가 된다. 본문/도크 탭/하단 상태 행 배율은 유지한다.
- 재생 박스 56×16px, 엔진 아이콘 14px. 모두 사용자 배율×모니터 DPI를 한 번 적용한다.
- 아이콘은 `Editor/EngineEntry/Academy_4Q.ico`를 `Tools/icons/Export-EngineIcon.ps1`로 PNG 변환한 동일 artwork다. EditorAssetPresentation이 적재·해제하고 EditorImGuiTexture가 업로드한다.
- `BeginMainMenuBar`의 중복 sidebar 진입, 별도 `RenderToolBar`를 제거했다. 메뉴 행과 도크 호스트의 테두리 선을 없앴다.
- 좁은 창은 메뉴를 접고 충돌하는 제목을 숨긴다. Play/Stop 및 Pause/Resume은 Scene/Game 탭 선택과 무관하게 노출한다.
- 재생 버튼은 SceneManager API를 사용하며 Pause는 편집 상태에서 비활성이다. caption hit 영역은 재생 박스 전에 끝난다. 기존 Win32 최대화/복원·크기 조절 메시지 경로를 유지한다.

## 검증

증거 디렉터리: `Artifacts/phase21-titlebar/`.

- 첫 x64 Debug 빌드 성공 (`build-debug.log`).
- `editor.selftest`: 915 checks, 0 failures (`selftest.json`). 제목 행 배치 100개 단정을 추가했다: 배율 1/1.25/1.5/2.25/3 × 논리 폭 320/480/800/1280/1920.
- DX12 실제 UI, 사용자 배율 150%·OS DPI 150%: File 메뉴 열기, Play, Game 탭에서 Pause/Resume, Stop 확인. 각 단계의 `play.state`를 `idle/playing/paused/resumed/stopped.json`에 기록했다.
- 재생 상태 전이는 `(false,false) → (true,false) → (true,true) → (true,false) → (false,false)`였으며 모두 pending=false, entities=3.
- 실제 최대화 및 복원 버튼 클릭 성공 (`maximized.png`).
- 제품 `window.resize`로 960/720 physical px 폭을 적용해 메뉴 폭과 접힘을 확인했다. 접힌 메뉴의 File 하위 항목도 열었다 (`resized-small.json`, `resized-compact.json`, `compact-menu.png`).
- 마우스 드래그 방식의 크기 변경은 자동화 입력으로 크기 변화가 관측되지 않아 성공으로 세지 않았다. 메뉴/Play/최대화 클릭과 제품 resize API 검증을 구분한다.

- 최종 메뉴 간격 보정·호스트 테두리 제거 이후 x64 Debug 빌드 성공 (`build-final.log`). 기존 `LNK4229 /DELAYLOAD:vulkan-1.dll` 경고 1건이 있으며 컴파일/링크 오류는 없다.
- 최종 DX12 배율 회귀: `verify-editor-theme.ps1 -Backends dx12 -Work Artifacts/phase21-titlebar/theme-gate` — 사용자 배율 100→150→100%, 3회 시작/64 checks PASS. 각 실행의 `editor.selftest` 915 checks/0 failures. OS DPI는 150% 관측이며 실제 모니터 간 이동 검증은 아니다.
- 게이트가 설정과 imgui.ini를 바이트 단위로 복원한 것을 확인했다.
- 최종 빌드 실제 화면 `final-titlebar.png`, 접힘 경계 `final-compact.png` 확인. 편집 상태 Pause 클릭 후 `(gameStart=false, paused=false)` 유지 (`disabled-pause.json`). 최종 닫기 버튼 클릭으로 검증 프로세스가 종료되었고 stderr=0 bytes, 설정/ini 원본 복원을 다시 확인했다.

## 경계

Vulkan은 사용자 결정에 따라 보류했다. 이 변경은 W4/W5의 ViewportHost나 Play 입력 소유권 재설계를 완료하지 않는다.
