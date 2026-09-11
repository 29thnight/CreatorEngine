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

### 2.7 곁가지 — 죽은 include 하나

`EngineGUIWindow/InspectorWindow.cpp:53` 이 `PinHelper.h` 를 들이는데 `DrawPinIcon` 을
**한 번도 부르지 않는다.** 계획서 W2 가 목록에 둔 `ImGuiContext.h` 의 죽은
`imgui_impl_dx11.h` include 와 같은 종류인데, 그쪽은 이미 없어졌다(§5). 남은 것은 이
하나이므로 이관 슬라이스에서 함께 걷는다.

같은 이유로 `EngineGUIWindow/MenuBarWindow.cpp:35` 의 `#include "ToggleUI.h"` 도 죽은
include 다 — `ToggleSwitch` 를 부르지 않는다. §2.5 의 은퇴는 이 한 줄을 걷는 것으로 끝난다.

---

## 3. 이 표가 W2 구현 순서에 주는 것

1. **`EditorSectionHeader`** — 승계. 기존 138줄을 토큰화·개명하고 소비자 3을 옮긴다.
2. **`EditorPropertyRow`** — 승계. `TableAPIHelper.h` 를 UTF-8 로 바꾸는 커밋이 먼저,
   그 다음 행 규약을 일반화한다(vec2 전용 → 열 개수 인자).
3. **`EditorAxisField3`** — 신설. §2.4 의 네 자리 중 `ReflectionTypedDraw.h:304` 를 대표
   지점으로 잡는다.
4. **`EditorModeButton`** — 신설. 착지 뒤 `ToggleUI.h` 를 지운다.

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

## 5. 계획서 항목 하나는 이미 끝나 있다

W2 목록의 "`ImGuiContext.h` 의 죽은 `imgui_impl_dx11.h` include 를 걷는다" 는 할 일이 없다.
`ImGuiContext.h` 가 저장소에 없고, `imgui_impl_dx11` 이라는 이름이 남은 곳은
`EngineEntry/EditorMain.h:39` 의 **주석 한 줄**뿐이다(무엇을 걷었는지 적어 둔 기록).

## 6. 이 표가 답하지 않는 것

`EditorSectionHeader` 와 `EditorPropertyRow` 의 토큰화는 `EditorTheme.h` 의
`EditorThemeTokens`(RowHeight·ControlRadius 등)를 읽어야 하는데, 이 글을 쓰는 시점에 그 파일은
다른 세션이 미커밋으로 들고 있다. 치수를 `ImGuiStyle` 에서만 뽑으면 두 번 고치게 되므로
구현은 그 커밋 뒤로 미뤘다.
