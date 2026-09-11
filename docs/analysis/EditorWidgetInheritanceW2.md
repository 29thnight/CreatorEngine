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

이 파일은 CP949 다. 계획서 §1.10 이 "편집 대상에 비-UTF8 파일이 있으면 인코딩을 먼저 정리한
뒤 내용을 고친다" 고 정했으므로, 이관 슬라이스의 첫 커밋은 **인코딩만** 바꾸는 것이어야 한다.

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
**한 번도 부르지 않는다.** 계획서 W2 가 이미 목록에 둔 `ImGuiContext.h` 의 죽은
`imgui_impl_dx11.h` include 와 같은 종류다. 같은 슬라이스에서 함께 걷는다.

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

## 4. 이 표가 답하지 않는 것

`EditorPropertyRow` 의 "mixed/disabled/error 상태"(계획서 §7.1)를 지금 저장소에서 쓰는 자리는
세지 않았다. 상태 matrix 는 W2 의 별도 항목이고, 승계 여부를 가르는 데는 필요하지 않아서
범위에서 뺐다. 구현 착수 전에 따로 세야 한다.
