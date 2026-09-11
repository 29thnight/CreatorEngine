# W2 승계 결정표 — 확정본

- 작성: 2026-09-11
- 대상: `docs/plans/EditorWorkspaceRedesignPlan.md` §7.1 의 초기 판정표
- 범위: `Editor/ImGuiHelper/` 의 기존 자산과 W2 가 세울 네 family 의 관계

계획서가 W2 의 **첫 산출물**로 지목한 표다. 관계를 정하지 않고 구현을 시작하면 같은 역할의
위젯이 두 벌 남는다. 초기 판정 다섯 중 **셋이 실측과 어긋났다.**

판정의 근거는 소비자 수다. 계획서가 경고한 대로 파일 이름 부분 문자열로 세지 않았다 —
헤더를 읽어 **실제로 내보내는 심볼**을 확인하고, 그 이름에 단어 경계를 붙여 정의처 밖의
호출만 셌다. 훑은 소스 950개.

첫 자동 추출은 숫자가 무의미했다. 헤더 **안에서 부르는** ImGui 함수(`Button`, `PushID`,
`Checkbox` …)까지 "내보내는 심볼" 로 잡아 `CustomCollapsingHeader.h` 의 소비자가 476건으로
나왔다. 다섯 파일이 합쳐 476줄이라 읽는 편이 빨랐고 정확했다.

---

## 1. 확정표

| family | 기존 자산 | 계획서 초기 판정 | **확정** |
|---|---|---|---|
| `EditorSectionHeader` | `CustomCollapsingHeader.h` | 승계 | **승계** (유지) |
| `EditorPropertyRow` | `TableAPIHelper.h` | 부분 승계 | **승계** — label column 규약의 정본이 이미 여기 있다 |
| `EditorPropertyRow` | `HorizontalLayout.h` | 부분 승계 | **범위 밖으로 정정** — 소비자가 노드 에디터뿐 |
| `EditorAxisField3` | RectTransform 의 축 필드 | 승격 | **신설로 정정** — 승격할 구현이 없다 |
| `EditorModeButton` | `ToggleUI.h` | 판정 필요 | **은퇴** — 소비자 0 |
| `EditorModeButton` | `widgets.{h,cpp}` | 판정 필요 | **범위 밖으로 정정** — `ax::Widgets::Icon` 이다 |
| — | `drawing.{h,cpp}` · `BlueprintBuilder` · `NodeEditor` | 범위 밖 | **범위 밖** (유지) |

---

## 2. 근거

### 2.1 `CustomCollapsingHeader.h` → `EditorSectionHeader` 승계 (유지)

`ImGui::DrawCollapsingHeaderWithButton` 오버로드 둘(`bool* pChecked` 유무)을 내보낸다.
소비자 **3건**.

| 소비자 | 건수 |
|---|---:|
| `EngineGUIWindow/InspectorWindow.cpp` | 2 |
| `EngineGUIWindow/ImGuiDrawHelperRectTransformComponent.cpp` | 1 |

`pChecked` 를 받는 오버로드가 계획서 §7.1 의 "component header, enable toggle, fold,
context menu" 중 enable toggle 과 context menu 버튼을 이미 든다. 토큰화해서 개명하면 된다.
138줄.

#### 이관 결과 — 착지 (2026-09-11)

`Editor/ImGuiHelper/EditorSectionHeader.{h,cpp}` 로 옮기고 `CustomCollapsingHeader.h` 를
지웠다. 소비자 3 을 모두 옮겼고, 자산 참조는 0 이다.

승계하며 **죽은 코드 셋**이 드러났다. 개명만으로는 안 되는 것들이다.

| 원본의 것 | 실상 |
|---|---|
| `ImGuiCol_HeaderHovered` 분기 | `bool hovered = false;` 선언 **직후** 그 값으로 배경색을 골랐고, 진짜 hover 는 20여 줄 **뒤**에 대입했다. hover 색이 한 번도 그려지지 않았다. |
| `bool held` | `IsItemActive()` 로 읽었지만 맨 `ItemAdd` 는 ActiveId 를 세우지 않는다. 읽히지 않은 것이 아니라 **참이 될 수 없었다.** |
| `ImGuiTreeNodeFlags flags` 인자 | 세 호출자가 모두 `ImGuiTreeNodeFlags_DefaultOpen` 을 넘기는데 본문이 한 번도 보지 않았다. |

고친 방식은 순서를 바꾼 것이다. 자리를 먼저 잡고(`ItemAdd` → `ButtonBehavior`) 상태를 읽은
**뒤에** 배경을 그린다. 클릭 영역이 양옆 컨트롤을 비켜서 잡히므로 버튼·체크박스의 클릭을
빼앗지 않는다.

**색은 Header 칸을 쓰지 않는다.** `EditorTheme.cpp` 가 `ImGuiCol_HeaderHovered` 와
`ImGuiCol_HeaderActive` 를 **같은 값**(`Selection`)으로 설정한다. 그래서 원본 구조대로 hover 를
살려도 눈에 보이는 차이가 없다 — 닫힌 머리줄이 hover 되면 열린 것과 똑같이 보인다. 토큰을
직접 읽어 넷을 가른다.

| 상태 | 토큰 | 값 |
|---|---|---|
| 닫힘 | `Panel` | `0x343434` |
| 열림 | `PanelRaised` | `0x484848` |
| hover | `Selection` | `0x525252` |
| 누름 | `Primary` | `0x2E70EA` |

넷 다 불투명이다. 처음에는 hover 를 `Selection` 45% 로 썼다가 버렸다 — 알파는 깔린 색에 따라
결과가 달라진다. 계산해 보니 `Panel`(`0x34`) 위에서 0x41 이 되어 열림(`0x48`)과 6/255 밖에
벌어지지 않았고, 같은 머리줄이 Inspector(`ChildBg`=`Panel`)와 창 바닥(`Canvas`)에서 서로 다른
색이 되는 문제도 있었다. 누름을 `Primary` 로 둔 것은 테마가 `ImGuiCol_ButtonActive` 에 쓰는
강조색과 같아서다.

**이 표는 단정이다, 주석이 아니다.** 머리줄은 열림/닫힘을 배경색으로만 알린다. 누가
`Panel` 과 `PanelRaised` 를 같은 값으로 만들면 상태 표시가 통째로 사라지는데 빌드도 다른
검사도 붉어지지 않는다. 원본이 hover 를 잃은 원인 자체가 테마가 `HeaderHovered` 와
`HeaderActive` 를 같은 값으로 둔 것이었으니 같은 충돌이 재발할 수 있다. 그래서 구현이 고르는
값을 `section_header_surface_hex` 로 내보내고, `EditorThemeSelfTest` 가 넷의 기대 hex 와 여섯
쌍의 상이함을 단정한다(`verify-editor-theme.ps1` 이 도는 세트에 있다).

**변이로 이빨을 쟀다.**

| 변이 | 결과 |
|---|---|
| 제품에서 `Hovered` 토큰을 `Selection` → `PanelRaised` (열림과 충돌) | 빌드 exit 0, 게이트 **붉음** — 193 중 2 실패. 붉은 것은 `section header: hovered` 와 `section header: states differ` 둘뿐이고 다른 검사는 하나도 건드리지 않았다. |
| 위 변이를 유지한 채 검사의 기대 hex 만 새 값으로 완화 | 빌드 exit 0, 게이트 **붉음** — 193 중 1 실패, `states differ` 단독. 상이성 단정 혼자서도 잡는다. |

치수 둘도 매직 넘버였다. 버튼 너비 `24.0f` 는 `GetFrameHeight()` 로, 아래 여백 `3.0f` 는
`EditorThemeTokens::CompactGap` 으로 바꿨다. 너비를 `ThemePixels(ControlHeight)` 로 재지
않은 이유가 있다 — ImGui 1.92 의 `FontScaleMain`/`FontScaleDpi` 는 폰트만 키우고 `ImGuiStyle`
의 여백은 그대로 둔다(`imgui.h:2536` 주석). 토큰을 직접 배율하면 DPI 2 에서 이 버튼만 홀로
커진다. `GetFrameHeight()` 는 같은 줄의 `ImGui::Button` 과 같은 자를 쓴다.

오버로드 둘은 하나로 합쳤다. 차이가 체크박스뿐이라 `enabled` 가 비면 그리지 않는다. 출력도
구조체 하나로 돌려준다 — 원본은 "눌렸을 때만 true 를 써 넣는" 출력 인자라 호출자가 매 프레임
스스로 초기화해야 했다. 옛 규약에 기대는 소비자가 하나 있어(`InspectorWindow.cpp` 의
`static bool isOpen`) 그 자리는 `menu_clicked` 일 때만 쓰는 것으로 옮겼다.

### 2.2 `TableAPIHelper.h` → `EditorPropertyRow` 승계 (초기 판정 "부분 승계" 보다 강함)

이 파일은 이름이 가리키는 범용 표 헬퍼가 아니다. 내보내는 다섯이 전부 RectTransform 의
vec2·앵커 행이다.

| 심볼 | 소비자 |
|---|---|
| `DrawVec2Row` | `ImGuiDrawHelperRectTransformComponent.cpp` 3 |
| `DrawVec2RowAbs` | 같은 파일 2 |
| `DrawAnchorIconButton` | 같은 파일 1 |
| `DrawAnchorIconVisual` | 같은 파일 1 |
| `VecEq` | 같은 파일 2 |

소비자가 한 파일뿐이다. 그리고 `DrawVec2Row` 의 몸통이 곧 `EditorPropertyRow` 의 규약이다 —
0번 열이 라벨(`AlignTextToFramePadding` + `PushTextWrapPos`), 그 뒤가 축마다 한 열,
`SetNextItemWidth(-FLT_MIN)` 로 셀 가용폭 전부. **label column 규약의 정본이 이미 여기 있다.**
"부분 승계" 가 아니라 승계다.

이 파일은 CP949 였다. 계획서 §1.10 이 "편집 대상에 비-UTF8 파일이 있으면 인코딩을 먼저
정리한 뒤 내용을 고친다" 고 정했으므로 인코딩만 바꾸는 커밋을 먼저 넣었다(`63e87c78`,
Editor 트리의 CP949 소스 여덟을 함께). 내용 이관은 그 위에 선다.

#### 이관 결과 — 착지 (2026-09-11)

`Editor/ImGuiHelper/EditorPropertyRow.{h,cpp}` 로 옮기고 `TableAPIHelper.h` 를 지웠다.

승계하며 셋을 바꿨다.

| 원본의 것 | 바꾼 것 |
|---|---|
| X·Y 두 열이 몸통에 박힘 | 열 개수를 인자로. `math::vector2` 가 `float x, y;` 연속 멤버라 원본도 사실상 float 배열을 쓰고 있었고, 열 수만 올리면 vec3·vec4 가 같은 규약에 든다 |
| 오버로드 둘 (`DrawVec2Row` / `DrawVec2RowAbs`) | 하나로. 차이가 `min`/`max` 뿐이었고 Abs 는 `0,0` 을 넘겨 제한을 껐다. 기본값을 `0,0` 으로 두면 같은 뜻이다 |
| 필드 ID `"##x"`·`"##y"` | 열 인덱스로 고정한 표. 세 번째 열을 더할 때 이름을 새로 짓지 않아도 되고, 매 프레임 문자열을 만들지 않는다 |

**파일이 통째로 사라진 이유.** 행 함수 둘을 걷어내고 남는 것은 `NearEq`·`VecEq`·
`DrawAnchorIconButton`·`DrawAnchorIconVisual` 넷인데 전부 RectTransform 앵커 전용이고
소비자도 그 한 파일이다. 범용 헤더가 아닌 것을 범용 이름으로 남겨 둘 이유가 없어
소비자 파일의 익명 이름공간으로 내렸다. 그러면서 §2.7 이 센 죽은 include 둘
(`InspectorWindow.cpp`, `ImGuiDrawHelperTerrainComponent.cpp`)도 함께 없어졌다.

**내리면서 결함 하나가 드러났다 — 뒤집힌 Y 축.** 같은 그림을 그리는 함수가 두 벌이었고
세로 축이 서로 **반대**였다.

| 함수 | 세로 축 | 판정 |
|---|---|---|
| `DrawAnchorIconButton` | `ImLerp(Min.y, Max.y, ny)` — ny=0 이 화면 위 | 맞다 |
| `DrawAnchorIconVisual` | `ImLerp(Max.y, Min.y, ny)` — ny=0 이 화면 아래 | **틀렸다** |

앵커 표는 y-down 이다 — `TopLeft={0,0}`, `BottomLeft={0,1}`
(`RectTransformComponent.h:16`). 즉 인스펙터의 "현재 프리셋" 버튼은 고른 것과 위아래가
뒤집힌 그림을 보이고 있었다. 팝업 안의 3x3 은 버튼 쪽 함수를 쓰므로 멀쩡했고, 그래서
눈에 띄기 어려웠다 — 고른 칸과 보이는 그림을 나란히 볼 일이 없다. 고친 방식은 그림
함수를 하나로 만든 것이다. 축이 한 자리에만 있으면 두 벌이 갈라질 수 없다.

**상태는 표준 위젯의 것을 쓴다.** 이 줄은 배경을 그리지 않으므로 hover·active 는
`DragFloat` 자신이 든다. disabled 는 넣지 않았다 — 다섯 소비자 중 쓰는 자리가 없어
한 번도 돌지 않는 경로가 된다. §4 가 "disabled 만 지금 뚫을 값이 있다" 고 한 것은
저장소 전체를 센 것이고, 이 줄의 소비자로 좁히면 공집합이다.

### 2.3 `HorizontalLayout.h` → 범위 밖 (정정)

`BeginHorizontal`·`EndHorizontal`·`BeginVertical`·`EndVertical`·`Spring` 을 내보낸다.
소비자는 **`Editor/ImGuiHelper/BlueprintBuilder.cpp` 하나뿐**이다(각 4건, `Spring` 13건).
`LayoutState` 는 소비자 0.

BlueprintBuilder 는 노드 에디터 전용이고 계획서가 이미 범위 밖으로 둔 것이다. 이 헤더가
Inspector 에 닿은 적이 없으므로 `EditorPropertyRow` 의 입력이 아니다. 초기 판정이 이름
(`HorizontalLayout` → "행 배치")에서 추론한 것으로 보인다.

### 2.4 `EditorAxisField3` → 신설 (정정)

계획서는 "창 안에 흩어진 구현을 정본으로 끌어올린다" 고 적었다. **끌어올릴 구현이 없다.**

X/Y/Z 색 badge 는 저장소에 한 자리도 없다. vec3 를 그리는 자리는 넷이고 전부 맨
`DragFloat3` 에 `ImGui::Text` 라벨이다.

| 자리 | 내용 |
|---|---|
| `InspectorWindow.cpp:528` | `DragFloat3("##Position", …)` |
| `InspectorWindow.cpp:567` | `DragFloat3("##Rotation", …)` |
| `InspectorWindow.cpp:613` | `DragFloat3("##Scale", …)` |
| `ReflectionTypedDraw.h:304` | `DragFloat3(label, &v.x)` — 리플렉션 경로의 정본 |

계획서가 지목한 `ImGuiDrawHelperRectTransformComponent.cpp` 의 축 필드는 **vec2** 이고
(§2.2 의 `DrawVec2Row`), vec3 가 아니다. 승격이 아니라 신설이며, 승계할 것이 있다면 축
**필드**가 아니라 §2.2 의 **행 규약**이다.

이관 대상 넷 중 `ReflectionTypedDraw.h:304` 가 가장 넓다 — 리플렉션이 그리는 모든 vector3 가
그 한 줄을 지난다. 대표 지점을 고른다면 여기다.

#### 이관 결과 — 착지 (2026-09-11)

`Editor/ImGuiHelper/EditorAxisField3.{h,cpp}` 를 새로 세우고 네 자리를 모두 옮겼다 —
`ReflectionTypedDraw.h` 의 vector3 분기와 `InspectorWindow.cpp` 의 Position·Rotation·Scale.

**라벨은 ImGui 규약을 그대로 쓴다.** 라벨을 왼쪽으로 옮기는 규약을 새로 세우지 않았다.
가장 넓은 소비자가 `ReflectionTypedDraw.h` 이고 그곳의 다른 열두 타입이 모두 ImGui
기본(라벨 오른쪽)이라, vector3 만 왼쪽이 되면 리플렉션 인스펙터 안에서 그 줄만 어긋난다.
라벨을 앞에 두고 싶은 호출자는 지금처럼 `Text` + `SameLine` 뒤에 `##` 이름을 넘긴다.

**축 색은 의미 색을 쓰지 않는다.** 팔레트의 `Error`(빨강)·`Positive`(초록)·
`Primary`(파랑)가 크기도 색상도 딱 맞지만 그대로 쓰면 세 축이 곧 오류·성공·강조가 된다.
그러면 이 줄에 error 상태를 넣는 순간 X 축과 색이 겹쳐 상태 표시가 사라진다 —
`EditorSectionHeader` 가 hover 를 잃었던 것과 같은 충돌이다. 그래서 따로 두었다.

| 축 | 값 | 흰 글자 대비 |
|---|---|---|
| X | `0xC0392B` | 5.4:1 |
| Y | `0x4F7A28` | 5.1:1 |
| Z | `0x2D6FA8` | 5.3:1 |

대비를 재 둔 이유가 있다. 밝은 `Positive`(`0x5AEB5C`)를 그대로 쓰면 흰 글자 대비가
**2.1:1** 이라 badge 의 글자가 배경에 묻는다. 색만으로 축을 알리면 색을 구별하지 못하는
눈에는 세 칸이 같은 칸이 되므로 글자는 장식이 아니다. 그래서 이 표는 주석이 아니라
단정이다 — `EditorThemeSelfTest` 가 구현이 내보내는 두 색으로 대비를 다시 재고 3:1
미만이면 붉어진다.

**화면을 찍어 보고 하나를 적어 두었다.** badge 뒤의 입력칸은 손대기 전에 경계가 보이지
않는다. `EditorTheme.cpp` 가 `ImGuiCol_FrameBg` 를 `Canvas` 로 두는데 인스펙터 바탕도
`Canvas` 라 두 색이 같기 때문이다(hover 하면 `FrameBgHovered` 가 떠올라 드러난다).
이 위젯이 만든 성질이 아니라 테마가 두 칸을 같은 값으로 둔 결과이고, 이번에도 같은
충돌 양식이다. W1 이 아직 `progress` 라 여기서 값을 바꾸지 않고 기록만 했다.

### 2.5 `ToggleUI.h` → 은퇴 (소비자 0)

`ImGui::ToggleSwitch(const char*, bool)` 하나를 내보내고 **소비자가 0** 이다. 계획서는
"겹치면 승계, 아니면 신설 후 은퇴" 로 두었고 실측이 후자로 답했다.

은퇴 비용이 0 이다 — 끊을 소비자가 없다. 다만 저장소 규약대로 **소비자를 실제로 끊어 본 뒤**
지운다는 조건은 여기서 공집합이므로, 지우는 커밋이 곧 증명이다(빌드가 서면 소비자가 없었다).

`ToggleSwitch` 의 시각(둥근 트랙 + 원형 손잡이)은 `EditorModeButton` 의 "flat icon/active
marker" 와 다른 물건이다. 승계하면 두 벌이 남는다.

### 2.6 `widgets.{h,cpp}` → 범위 밖 (정정)

`ax::Widgets::Icon(size, IconType, filled, color, innerColor)` 하나다. `ax::` 는 노드
에디터의 이름공간이고, 소비자는 `Editor/ImGuiHelper/PinHelper.h` 하나뿐이다(핀 아이콘).
`widgets.cpp` 는 `drawing.{h,cpp}` 의 `DrawIcon` 을 부른다 — 계획서가 이미 범위 밖으로 둔
쌍이다. `EditorModeButton` 의 입력이 아니다.

#### `EditorModeButton` 이관 결과 — 착지 (2026-09-11)

`Editor/ImGuiHelper/EditorModeButton.{h,cpp}` 를 세우고 툴바의 Play·Pause 둘을 옮긴 뒤
`ToggleUI.h` 를 지웠다. 빌드가 섰으므로 소비자가 없었다는 것이 그 자리에서 증명된다.

**원본 자리에 결함이 있었다.** `MenuBarWindow::RenderToolBar` 는 일시정지 중일 때
`ImGuiCol_Button`·`ButtonHovered`·`ButtonActive` **세 칸을 모두** `ButtonActive` 색으로
덮었다. 그러면 켜진 버튼은 마우스를 올려도 눌러도 색이 그대로다 — 켜짐과 눌림이 같은
축을 다투었기 때문이다. 여기서는 켜짐을 **아래 marker** 가 들고 배경은 hover/press 를
그대로 든다. 두 축이 갈라지므로 겹치지 않는다.

marker 두께는 활성 탭과 같은 토큰(`TabActiveMarker`)을 쓴다. 같은 뜻("이것이 지금 켜진
것")을 에디터 안에서 두 가지 두께로 그리면 규칙이 보이지 않는다.

**disabled 는 여기서 실제로 돈다.** 재생 중이 아닐 때 일시정지 버튼이 그 상태다.
`BeginDisabled` 로 감싸지 않고 위젯이 직접 든 이유는 그것이 전체 알파를 내려 marker 까지
흐리기 때문이다 — 켜져 있는데 손댈 수 없는 상태에서 켜짐 표시를 잃을 이유가 없다.

### 2.7 곁가지 — 죽은 include 하나

`EngineGUIWindow/InspectorWindow.cpp:53` 이 `PinHelper.h` 를 들이는데 `DrawPinIcon` 을
**한 번도 부르지 않는다.** 계획서 W2 가 목록에 둔 `ImGuiContext.h` 의 죽은
`imgui_impl_dx11.h` include 와 같은 종류인데, 그쪽은 이미 없어졌다(§5). 남은 것은 이
하나이므로 이관 슬라이스에서 함께 걷는다.

같은 이유로 `EngineGUIWindow/MenuBarWindow.cpp:35` 의 `#include "ToggleUI.h"` 도 죽은
include 다 — `ToggleSwitch` 를 부르지 않는다. §2.5 의 은퇴는 이 한 줄을 걷는 것으로 끝난다.

**정정 (2026-09-11 이관 중 발견).** 이 절의 목록이 하나 모자랐다. 위 표는 **심볼 호출**을 센
것이라 `#include` 만 있고 부르지 않는 파일을 놓쳤다. `EngineGUIWindow/ImGuiDrawHelperTerrainComponent.cpp:9`
가 `CustomCollapsingHeader.h` 를 들이면서 `DrawCollapsingHeaderWithButton` 을 한 번도 부르지
않았다. 즉 이 자산의 include 는 4 이고 소비자는 3 이었다. `EditorSectionHeader` 이관에서
그 한 줄을 함께 걷었다. 호출 건수와 include 건수는 다른 자다.

---

## 3. 이 표가 W2 구현 순서에 주는 것

1. ~~**`EditorSectionHeader`** — 승계. 기존 138줄을 토큰화·개명하고 소비자 3을 옮긴다.~~
   **착지했다 (2026-09-11, §2.1 의 "이관 결과").** 죽은 코드 셋을 함께 고쳤고 색은 Header 칸
   대신 토큰을 직접 읽는다.
2. ~~**`EditorPropertyRow`** — 승계. 행 규약을 일반화한다(vec2 전용 → 열 개수 인자).~~
   **착지했다 (2026-09-11, §2.2 의 "이관 결과").** `TableAPIHelper.h` 가 통째로 사라졌고
   앵커 아이콘의 뒤집힌 Y 축을 함께 고쳤다.
3. ~~**`EditorAxisField3`** — 신설.~~ **착지했다 (2026-09-11, §2.4 의 "이관 결과").**
   네 자리를 모두 옮겼고 축 색은 의미 색과 구별되게 따로 두었다.
4. ~~**`EditorModeButton`** — 신설. 착지 뒤 `ToggleUI.h` 를 지운다.~~
   **착지했다 (2026-09-11, §2.6 뒤의 "이관 결과").** 켜짐을 marker 로 옮겨 원본이 잃었던
   hover/press 를 되살렸고 `ToggleUI.h` 를 지웠다.

은퇴 대상은 `ToggleUI.h` 하나다. `HorizontalLayout.h`·`widgets.{h,cpp}` 는 은퇴가 아니라
**범위 밖**이다 — 노드 에디터가 쓰고 있으므로 건드리지 않는다.

## 4. 상태 matrix — 실측 (2026-09-11 추가)

계획서 §7.1 은 `EditorPropertyRow` 에 "mixed/disabled/error 상태" 를, W2 본문은
"hover/active/focus/nav/disabled/mixed/error 상태 matrix 를 고정한다" 를 요구한다. 그 일곱이
지금 저장소에서 **실제로 쓰이는 자리**를 셌다(`Editor/`, `ImGuiHelper/`·`RenderTests/` 제외,
주석 제거 후).

| 상태 | 표현 | 건수 | 판정 |
|---|---|---:|---|
| hover | `IsItemHovered` | 15 | 실재 |
| active | `IsItemActive` | 1 | 거의 없음 |
| focus | `IsItemFocused` | **0** | **소비자 0** |
| nav | `ImGuiCol_Nav*` | 3 | 전부 테마·탐침. 위젯 코드는 묻지 않는다 |
| disabled | `BeginDisabled` 6 + `TextDisabled` 14 | 20 | 실재. 다만 **기구가 둘** |
| mixed | `CheckboxFlags` (tristate) | **0** | **소비자 0** |
| error | 빨강 `PushStyleColor(ImGuiCol_Text)` | **2** | 사실상 없음 |

읽는 법 셋.

**① `mixed` 와 `focus` 는 소비자가 0 이다.** 계획서가 요구한 상태 중 둘이 저장소에 한 자리도
없다. 이 저장소는 "계획서가 지목한 대상이 이미 죽어 있을 수 있다" 로 한 번 데었다. 쓰는 데가
없는 상태를 위젯 API 에 미리 뚫으면 첫 소비자가 생길 때 모양이 맞을 확률이 낮다.
**두 상태는 첫 소비자가 생길 때 뚫는다.**

**② `error` 는 2건뿐이고 그나마 위젯 상태가 아니다.** `TextColored` 35건은 렌더 디버그와
리소스 카운터의 **데이터 색칠**이지 오류 표시가 아니다(`EnhancedRenderDebugWindow.cpp` 20 ·
`ResourceCounterWindow.cpp` 12). 문자열에 error/failed/invalid 가 든 661건은 거의 전부 CLI
명령의 메시지다. `EditorPropertyRow` 의 error 상태도 소비자가 생길 때 뚫는다.

**③ `disabled` 만 지금 뚫을 값이 있고, 기구가 둘로 갈려 있다.** `BeginDisabled/EndDisabled`
6건은 상호작용을 막고, `TextDisabled` 14건은 **색만** 바꾼다. 후자는 비활성이 아니라 "덜
중요함" 을 뜻하는 자리가 섞여 있을 수 있다. `EditorPropertyRow` 가 `disabled` 를 하나로
받으려면 그 14건을 먼저 두 뜻으로 갈라야 한다 — 그것은 이관 슬라이스의 일이다.

정리하면 **처음 구현에 넣을 상태는 hover·active·disabled 셋**이고, focus·nav·mixed·error 는
소비자가 생길 때 더한다. 그 판단의 근거가 위 표다.

## 4.1 검사가 붙은 자리와 그 이빨 (2026-09-11 추가)

네 family 가 모두 착지하면서 `EditorThemeSelfTest` 의 검사가 **193 → 237** 로 늘었다.
늘어난 44 는 세 위젯이 고르는 값을 읽어 단정한다.

| 위젯 | 단정 |
|---|---|
| `EditorPropertyRow` | 필드 ID 가 `##` 로 숨겨질 것, 서로 다를 것, 범위 밖은 `nullptr` 일 것 |
| `EditorAxisField3` | 축 색 셋의 기대 hex, 셋이 서로 다를 것, **의미 색 셋과 다를 것**, 흰 글자 대비 3:1 이상일 것 |
| `EditorModeButton` | 표면 셋의 기대 hex, 서로 다를 것, **marker 가 표면 셋 어느 것과도 다를 것**, 꺼진 글자색이 다를 것 |

**변이로 이빨을 쟀다.** 셋을 한 번에 넣고 어느 검사가 붉어지는지 이름으로 확인했다.

| 변이 | 붉어진 검사 |
|---|---|
| 필드 ID 표에서 `"##f2"` → `"##f1"` (중복) | `property row: field ids differ` |
| X 축 색 → `Positive`(`0x5AEB5C`) | `axis x: badge color` · `axis x: badge is not a meaning color` · `axis x: badge letter stays readable` |
| marker → `Selection` (Held 와 충돌) | `mode button: marker stands out from every surface` |

237 중 **5 실패**이고, 붉은 다섯이 모두 그 변이가 깨뜨린 계약의 이름이다. 나머지 232 는
하나도 건드리지 않았다. 대비 단정이 세 번째 줄에서 혼자 붉어진 것이 특히 값이 있다 —
색을 바꿔도 대비가 충분하면 그 검사는 조용하고, 실제로 묻는 색을 골랐을 때만 운다.

되돌린 뒤 Debug·Release 빌드가 서고 `verify-editor-theme.ps1` 이 DX12·Vulkan 6기동
103검사로 통과한다. 이웃 게이트 넷(obsolete surface · declaration wiring · workspace ·
icon resources)도 초록이다.

## 5. 계획서 항목 하나는 이미 끝나 있다

W2 목록의 "`ImGuiContext.h` 의 죽은 `imgui_impl_dx11.h` include 를 걷는다" 는 할 일이 없다.
`ImGuiContext.h` 가 저장소에 없고, `imgui_impl_dx11` 이라는 이름이 남은 곳은
`EngineEntry/EditorMain.h:39` 의 **주석 한 줄**뿐이다(무엇을 걷었는지 적어 둔 기록).

## 6. 이 표가 답하지 않는 것

`EditorSectionHeader` 와 `EditorPropertyRow` 의 토큰화는 `EditorTheme.h` 의
`EditorThemeTokens`(RowHeight·ControlRadius 등)를 읽어야 하는데, 이 글을 쓰는 시점에 그 파일은
다른 세션이 미커밋으로 들고 있다. 치수를 `ImGuiStyle` 에서만 뽑으면 두 번 고치게 되므로
구현은 그 커밋 뒤로 미뤘다.
