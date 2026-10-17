# Editor 입력 컨트롤·메뉴 간격 보정 (2026-09-12)

## 원인과 수정

| 증상 | 원인 | 수정 |
|---|---|---|
| 체크박스·드롭다운·float 입력이 큼 | 글꼴 16px에 상하 padding 4px가 더해져 컨트롤 높이 24px. 사용자/모니터 배율이 각각 150%이면 약 54 physical px | 상하 padding 2px, 컨트롤 높이 20 logical px. 일반 행 간격 8→4px, 본문 글꼴 16px 유지 |
| 탭 핸들 크기 유지 | ImGui 탭과 입력칸이 FramePadding을 공유해 함께 축소됨 | TabStyleScope로 NewFrame의 floating dock, DockSpace, 창 Begin, Animator의 명시적 탭에 기존 4px padding 적용. 본문은 2px로 복원하며 탭 높이는 기존 24px 유지 |
| 컴포넌트 활성 체크박스가 제목보다 큼 | ImGui Checkbox가 GetFrameHeight() 전체를 사각형 크기로 사용 | 해당 체크박스에만 padding.y=0을 적용해 16px. 예약 칸도 16px, 제목과 간격 6px로 조정 |
| Add Component 문구가 잘림 | 배율과 문구 길이에 관계없는 고정 물리 폭 180px | CalcTextSize+실제 padding으로 최소 폭 산정. 내용 영역 안에서 가운데 배치. 부족하면 Add/Component 두 줄 표시 |
| 메뉴 항목이 붙음 | 제목 줄에서 사용하는 ItemSpacing.y=0이 열린 팝업까지 상속됨 | 메뉴 간격 x=10/y=6px, 팝업 WindowPadding=8×6px. 제목 행 높이 20px 유지 |

컴포넌트 헤더의 접기 버튼은 체크박스 사각형 위에서 반응하지 않도록 분리했다.
ImGui Checkbox와 기존 Component::SetEnabled 경로를 계속 사용한다.
메뉴는 표준 BeginMenu/MenuItem, 입력값 편집은 기존 ImGui 및 Inspector 숫자 위젯을 사용한다.

## 검증

증거 디렉터리: `Artifacts/phase21-control-density/`.

- Debug x64 `CreatorEditor.vcxproj /t:Build` 통과. 기존 `/DELAYLOAD:vulkan-1.dll` LNK4229 경고는 남는다.
  도중 커서 모듈의 변경된 호출부와 기존 라이브러리가 달라 LNK2019가 발생했고,
  해당 모듈을 포함한 재빌드로 통과했다. 이 작업에서 커서 소스는 수정하지 않았다.
- 최종 실행 파일의 `editor.selftest`: **916 checks, 0 failures**. `editor.theme`의
  geometry/theme mapping, DPI, Inter/Material Symbols 검사 모두 통과.
- 실제 DX12 화면(사용자 150%, OS DPI 150%)에서 Scene/Game, Hierarchy, Inspector,
  Content Browser 탭이 기존 높이를 유지하고 본문 입력칸은 축소된 것을 확인했다.
  기본 16px 글꼴은 그대로이며 배율을 적용한 탭은 54 physical px,
  입력칸은 ImGui padding 절삭을 포함해 44 physical px이다.
- Main Camera의 Transform float3, Camera float4/float1, Tag/Layer 콤보와 체크박스 표시 확인.
  float2 전용 화면 및 모든 전용 드로어를 전수 조작한 결과는 아니다.
- LightComponent 체크박스 끄기/켜기와 헤더 접기를 각각 조작했다.
  체크박스 클릭으로 본문이 함께 접히지 않았고, 마지막에 활성 상태를 복원했다.
- Add Component 전체 문구 표시와 기존 컴포넌트 선택 팝업 열기 확인.
  창을 2880×1665 → 850×1245 → 720×1245 → 2880×1665로 변경하면서,
  마지막 좁은 폭에서 Add/Component 두 줄 표시를 확인했다. 컴포넌트 추가/씬 저장은 하지 않았다.
- Edit 드롭다운의 행 간격과 팝업 가장자리 여백을 실제 화면에서 확인했다.
- `verify-editor-theme.ps1 -Backends dx12`: **3회 기동, 64 checks 통과**.
  사용자 배율 100→150→100% 왕복이며 실제 OS DPI는 150%였다.
  물리 모니터 간 이동 검증은 포함하지 않는다.
- 검증용 Editor 종료 후 EngineSettings.asset와 imgui.ini를 시작 전 바이트로 복원했다.
  변경 파일 `git diff --check` 및 대시보드 JavaScript 구문 검사 통과.

최종 탭/숫자 입력 화면: [tabs-restored-camera.png](../../Artifacts/phase21-control-density/tabs-restored-camera.png).
좁은 패널: [add-component-wrapped.png](../../Artifacts/phase21-control-density/add-component-wrapped.png).
메뉴 간격: [edit-menu.png](../../Artifacts/phase21-control-density/edit-menu.png).
메뉴/LightComponent 동작 캡처는 탭 높이 복원 전 중간 빌드의 증거이며,
최종 빌드의 탭과 입력칸 크기는 `tabs-restored-camera.png`를 기준으로 한다.

Vulkan은 사용자 결정에 따라 보류한다. 이 작업은 W2-I 전체나 비동기 썸네일 W7의 완료를 의미하지 않는다.
