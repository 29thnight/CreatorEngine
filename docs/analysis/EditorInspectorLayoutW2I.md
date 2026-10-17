# W2-I0 — 인스펙터 배치 실측과 공통 계약

- 작성: 2026-09-12
- 2026-09-13 상태 정정: I0는 완료. 아래는 착수 당시 실측과 추가 범위 제안이며 §5의 9일안은 현재 채택 견적이 아니다. 정본은 [최신 완료·잔여](../plans/EditorWorkspaceRedesignPlan.md#phase21-current-status)의 W2-I 6일·I0 완료 공수 0.5일을 따른다. 현재 외관은 사용자 승인으로 고정했다.
- 대상: `docs/plans/EditorWorkspaceRedesignPlan.md` 의 [W2-I](../plans/EditorWorkspaceRedesignPlan.md#w2-inspector-layout)
- 범위: W2-I0 이 요구한 "content 폭·공통 열·줄 전환과 표시 정책 정의", 그리고 계획서가
  I0 으로 미룬 **전용 드로어 전수 이관량**과 **개별 활성 변경 진입점**의 재계수

계획서는 W2-I 의 근거를 "현재 소스 분석" 이라고 적고, 실제 폭별 캡처·빌드·런타임 결과로
표기하지 말라고 못 박았다. 이 글은 그 경계를 지킨다 — 아래 계수는 소스에서 센 것이고,
Transform 중복과 라벨 배치 두 양식은 **실제 에디터 화면으로도** 확인했다. 어느 쪽인지 항목마다
적었다. 폭별·배율별 검증은 W2-I5 의 일이며 여기서 하지 않았다.

---

## 1. 인스펙터 렌더 경로 전수

`InspectorWindow::RenderImGui` 가 선택된 엔티티를 그리는 경로는 셋이다.

| 경로 | 자리 | 라벨 배치 |
|---|---|---|
| 기본 렌더 | `InspectorWindow.cpp:1718-1726` — 기본 정보 + 공간 컴포넌트 | 라벨 **왼쪽** (`Text` + `SameLine`) |
| 전용 드로어 | 컴포넌트 순회의 `type_guid` 분기 11개 | 드로어마다 제각각 |
| 일반 리플렉션 | `Meta::DrawObject` → `ReflectionTypedDraw.h` | 라벨 **오른쪽** (ImGui 기본) |

**라벨이 두 방향으로 갈린다는 것은 화면으로 확인했다.** 같은 인스펙터 안에서 전용 드로어의
`Position`·`Rotation`·`Scale` 은 라벨이 왼쪽에 서고, 그 아래 일반 리플렉션의 `m_name`·`position`·
`rotation`·`scale`·`m_parentID`·`m_nearPlane` 은 라벨이 오른쪽에 선다. 값 열도 맞지 않는다.

### 1.1 전용 드로어의 실제 분량

계획서가 "전용 드로어 전수 이관량은 I0 에서 재계수한다" 고 한 항목이다.

| 드로어 | 줄 |
|---|---:|
| `ImGuiDrawHelperSoundComponent` | 836 |
| `ImGuiDrawHelperGameObjectBaseInfo` | 206 |
| `ImGuiDrawHelperVolume` | 201 |
| `DrawManagedScripts` | 169 |
| `ImGuiDrawHelperTransformComponent` | 162 |
| `ImGuiDrawHelperImageComponent` | 152 |
| `ImGuiDrawHelperDecal` | 87 |
| `ImGuiDrawHelperBT` | 65 |
| `ImGuiDrawHelperSpriteRenderer` | 28 |
| `ImGuiDrawHelperFSM` | 22 |
| `ImGuiDrawHelperCanvas` | 13 |
| **`InspectorWindow.cpp` 안 소계** | **1,941** |
| `ImGuiDrawHelperMeshRenderer.cpp` | 477 |
| `ImGuiDrawHelperTerrainComponent.cpp` | 371 |
| `ImGuiDrawHelperRectTransformComponent.cpp` | 328 |
| `ImGuiDrawHelperAnimator.cpp` | 88 |
| `ImGuiDrawHelperPlayerInput.cpp` | 39 |
| **바깥 파일 소계** | **1,303** |
| `DrawYamlNodeEditor.cpp` (자산 Import Settings) | 143 |
| `ReflectionTypedDraw.h` (일반 경로 정본) | 499 |
| `ReflectionImGuiHelper.h` | 232 |

**한 드로어가 전체의 4분의 1이다.** `SoundComponent` 836줄이 전용 드로어 3,244줄 중 26%이고,
계획서가 "Sound 의 Bus/Params·Spatial 같은 허용된 하위 묶음" 을 따로 언급한 이유가 이 분량이다.
W2-I4 의 초기 추정 1.5일은 이 한 드로어를 상수로 보고 세운 값이 아니다 — §5 에서 다시 잰다.

### 1.2 죽어 있는 분기 하나

`InspectorWindow.cpp:1908` 이 `dynamic_cast<ICustomEditor*>` 로 사용자 정의 인스펙터를 찾고
`OnInspectorGUI()` 를 부른다. **그 인터페이스를 구현하는 클래스가 저장소에 0 개다.**
`ICustomEditor` 라는 이름이 나오는 파일은 인터페이스 정의(`ICustomEditor.h`, 9줄), 이 호출부,
프로젝트 파일 둘, 계획 문서 둘뿐이다. 상속하는 타입이 없으므로 `dynamic_cast` 는 언제나
`nullptr` 이고 실행은 늘 `else` 의 `Meta::DrawObject` 로 간다.

이관 계획에서 이 분기를 "전용 드로어 경로 하나" 로 세면 안 된다. 지금은 이관할 소비자가 없다.
W2-I3 에서 이 분기의 처리(유지·제거·실제 소비자 도입)를 명시적으로 정한다.

---

## 2. Transform 이 두 번 그려진다

계획서가 "Transform 이 다시 일반 리플렉션으로 내려갈 수 있다" 고 적은 항목이다. **가능성이
아니라 현재 동작이다.** 소스와 화면 양쪽으로 확인했다.

### 2.1 소스

- `Entity` 의 `Transform` 은 값 멤버가 아니라 **`m_components` 안에 있다.** S1-b 에서 저장소가
  옮겨졌고 `m_pTransformComponent` 는 그 슬롯을 가리키는 캐시다(`Entity.h:197-199`).
- 기본 렌더는 RectTransform 이 있으면 RectTransform 드로어를, 없으면 Transform 드로어를
  부른다(`InspectorWindow.cpp:1719-1726`).
- 그 뒤 컴포넌트 순회는 **`RectTransformComponent` 만** 건너뛴다(`InspectorWindow.cpp:1756`).
- `Transform` 은 리플렉션 등록 목록에 있다(`RegisterReflectManual.h` 의 `X(Transform)`).
  따라서 `Meta::Find` 가 성공하고 `Meta::DrawObject` 가 돈다.

### 2.2 화면

일반 엔티티(Main Camera)를 선택하면 `Transform` 헤더가 **두 개** 나온다. 위는 전용 드로어라
체크박스가 없고 축 badge 가 붙은 Position·Rotation·Scale 을 그린다. 아래는 일반 리플렉션이라
체크박스가 있고 `m_name`·`position`(4값)·`rotation`(4값 quaternion)·`scale`(4값)·`m_parentID` 를
그린다. 즉 **회전이 quaternion 원시값으로 한 번 더 편집 가능하다.**

### 2.3 엔티티 종류별 현재 동작

공간 컴포넌트 조합은 `Entity::AttachSpatialComponent`(`Entity.cpp:60-77`)가 정한다.

| 종류 | Transform | RectTransform | 지금 인스펙터가 하는 일 |
|---|---|---|---|
| 일반 · Empty | 있음 | 없음 | Transform 드로어 1 + 일반 리플렉션 1 → **헤더 2개** |
| UI | **없음** | 있음 | RectTransform 드로어 1, 순회에서 제외 → 1개 (정상) |
| Canvas | 있음 | 있음 | RectTransform 드로어 1 + Transform 일반 리플렉션 1 → **Transform 의 전용 편집이 없다** |

Canvas 가 둘 다 갖는 것은 의도된 예외다. rect 는 자식 레이아웃의 기준이고 Transform 은 월드
공간 배치용이며, `CanvasRenderMode::WorldSpace` 경로가 그 월드 행렬을 읽는다(`Entity.cpp` 의
`AttachSpatialComponent` 주석). 그런데 지금 인스펙터는 `else` 분기 때문에 Canvas 에서 Transform
드로어를 **부르지 않는다.** 계획서의 "Canvas 에서는 각각 한 번 표시한다" 는 이 자리를 고치라는
뜻이다.

---

## 3. 개별 활성·제거 진입점 재계수

계획서: "개별 조작과 엔티티 전이를 구분해 UI·개별 활성 변경 진입점에 같은 정책을 적용한다."
그 진입점을 셌다.

| 진입점 | 자리 | 대상 | 판정 |
|---|---|---|---|
| 인스펙터 컴포넌트 체크박스 | `InspectorWindow.cpp:1791` | **임의 컴포넌트** | 공간 컴포넌트 정책이 필요한 **유일한 개별 조작 진입점** |
| 엔티티 활성 전이 | `Entity.cpp:554` (`Entity::SetEnabled` → 각 컴포넌트) | 모든 컴포넌트·자식 | **막으면 안 된다** — 계획서가 명시적으로 금지 |
| C# 스크립트 활성 | `ClrHost.cpp:458` (`Api_Script_SetEnabled`) | `ScriptComponent` 한정 | 대상이 고정이라 무관 |
| C# 엔티티 활성 | `ClrHost.cpp:429` (`Api_Entity_SetEnabled`) | 엔티티 전체 | 엔티티 전이라 무관 |
| CLI | `ConsoleCommandSystem.cpp:2262·3612·3618` | MeshRenderer·Animator 한정 | 대상이 고정이라 무관 |
| 역직렬화·생성 시 강제 활성 | `CameraComponent.h:18` · `LightComponent.h:38` · `MeshRenderer.cpp:516` · `FoliageComponent.cpp:428` · `ModelSceneInstantiation.cpp` 3곳 | 각자 고정 | 공간 컴포넌트를 건드리지 않음 |

**정책을 적용할 자리는 하나다.** 나머지는 대상 타입이 이미 고정돼 있어 공간 컴포넌트에 닿지
않는다. 계획서가 경고한 "공용 `SetEnabled(false)` 를 무조건 거부하는 구현"은 `Entity.cpp:554`
를 막는 구현이고, 그 줄은 엔티티 전이의 전파 경로다.

**제거 쪽은 이미 서 있다.** `EditorObjectOperations.cpp:497` 이
`dynamic_cast<::Transform*> || dynamic_cast<RectTransformComponent*>` 로 공간 컴포넌트 제거를
거부한다. W2-I1 은 이 거부를 재구현하지 않고 그대로 쓴다.

---

## 4. 폭 계산과 전환 조건 — 정본 계약

I0 의 완료 기준은 "폭 계산·정책이 한 정본을 소비하고 전환 조건이 고정됨" 이다. 아래가 그
고정이다. 구현은 `EditorPropertyRow` 의 배치 책임을 넓혀 담고, 새 입력 체계나 매크로 선언을
만들지 않는다(계획서 계약 1).

### 4.1 폭의 출처는 하나다

가용 폭은 **`ImGui::GetContentRegionAvail().x`** 하나만 읽는다. 지금 `InspectorWindow.cpp` 는
`GetWindowSize()` 를 한 번, `GetContentRegionAvail()` 을 두 번 쓴다. 둘은 스크롤바 폭만큼 다르고,
`Add Component` 버튼이 `GetWindowSize().x` 로 중앙을 잡아 스크롤바가 선 폭에서 오른쪽으로 밀린다.
화면에서 그 버튼의 라벨이 잘리는 것으로 보인다.

창 전체 폭이나 GPU 타깃 크기를 배치 기준으로 쓰지 않는다.

### 4.2 열 구성

```
[ 라벨 열 ][ gap ][ 값 열 ..................... ][ gap ][ 보조 버튼 예약 ]
```

| 양 | 정의 |
|---|---|
| `label_max` | `ThemePixels(160)` — 라벨 열의 **상한**. 남는 폭은 값 열이 가져간다 |
| `label_ratio` | 가용 폭의 `0.40` — 상한에 닿기 전까지의 비율 |
| `label_min` | `CalcTextSize("MMMMM...").x` — 말줄임 뒤 최소 판독 폭 |
| `value_min` | `CalcTextSize("-0000.000").x + FramePadding.x * 2` — 대표 최악 문자열 |
| `aux_reserve` | 보조 버튼이 있는 줄만 `GetFrameHeight()` × 버튼 수 |
| `gap` | `ThemePixels(EditorThemeTokens::ItemGapX)` |

`value_min` 을 **현재 값이 아니라 고정 대표 문자열**로 재는 것이 핵심이다. 계획서가 "현재 숫자
값이나 매 프레임 라벨 최대값 변화로 열과 모드가 흔들리지 않게 한다" 고 한 자리다. 같은 이유로
`label_max` 도 실제 라벨 길이의 최대값을 매 프레임 다시 재어 정하지 않는다.

치수는 전부 W1 의 `ThemePixels` 를 **한 번만** 통과한다. 폰트에서 재는 값(`CalcTextSize`)은
이미 배율이 반영돼 있으므로 다시 곱하지 않는다.

### 4.3 전환 조건

세 모드를 둔다. 조건은 가용 폭 `W` 에 대해 판정한다.

| 모드 | 조건 | 배치 |
|---|---|---|
| `Inline` | `W - label_col - gap - aux ≥ value_min` | 라벨 왼쪽, 값 오른쪽 (기본) |
| `Stacked` | 위가 거짓 | 라벨 한 줄, 값 다음 줄 |
| 축 `AxisStacked` | 축 슬롯 하나가 `badge_w + value_min` 미만 | X·Y·Z 를 세로로 |

축 판정은 값 열을 셋으로 나눈 뒤에 한다. 축 슬롯 = `(value_col - gap × 2) / 3`.

**완충 폭을 둔다.** 전환한 모드에서 되돌아오려면 임계값보다 `ThemePixels(ItemGapX)` 만큼 더
넓어져야 한다. 경계에서 한 픽셀 왕복이 모드를 진동시키면 열 위치가 매 프레임 바뀐다.

**편집 중에는 전환을 보류한다.** `ImGui::IsAnyItemActive()` 가 참인 동안 직전 모드를 유지하고,
편집이 끝난 프레임에 다시 판정한다. 드래그 중에 값 칸이 다른 줄로 이동하면 드래그가 끊긴다.

### 4.4 ID 는 배치를 모른다

ID 씨앗은 엔티티·컴포넌트 인스턴스·필드·축의 안정 신원이다. 열 위치, 표시 라벨, 모드
(`Inline`/`Stacked`/`AxisStacked`)는 ID 에 들어가지 않는다. 이미 선 규약이 둘 있다 —
`EditorPropertyRow` 의 필드 ID 는 열 인덱스로 고정한 표이고 `EditorAxisField3` 의 것은 축마다
고정이다. 모드 전환으로 이 값이 달라지지 않는다.

### 4.5 넘침 처리

| 종류 | 처리 |
|---|---|
| 긴 라벨 | 말줄임 + 전체 이름 tooltip. 지금은 `m_nearPlane` 이 그냥 잘린다 |
| 설명·읽기 전용 경로 | 줄바꿈 |
| 편집 문자열 | 표준 입력칸의 탐색·스크롤 유지. 보조 버튼이 밖으로 밀리지 않게 폭을 먼저 예약 |
| 세로 | 스크롤. 높이가 남아도 행 간격을 늘려 채우지 않는다 |

---

## 4.6 배치만으로는 리플렉션 경로가 전용 드로어를 대신하지 못한다

**이 절은 첫 판 이후에 더했다.** §4 가 폭과 열 전환만 정해 두었는데, 그것만으로는
"리플렉션이 그리는 컴포넌트를 볼 만하게 만들려면 결국 컴포넌트마다 전용 창구를 하나씩
만들어야 하지 않느냐" 는 물음에 답이 되지 않는다. 실제로 저장소가 걸어온 길이 그쪽이고,
그래서 전용 드로어가 3,244줄 쌓였다. 아래가 그 물음에 대한 답이다.

### 4.6.1 갈리는 것은 테마가 아니다

색과 치수는 `ImGuiStyle` 이 전역으로 입힌다. 리플렉션 경로도 이미 같은 테마를 쓰고 있고,
화면에서 두 경로의 색·프레임 모양은 같다. 전용 드로어가 생겨난 이유는 테마가 아니라 셋이다.

| 축 | 지금 상태 | 전용 드로어 없이 풀 수 있나 |
|---|---|---|
| **배치** — 라벨 방향, 열 정렬, 폭 | 기본 렌더는 라벨 왼쪽, 리플렉션은 오른쪽 | 풀 수 있다. §4 의 공통 계층을 리플렉션 경로가 소비하면 된다 |
| **의미** — 표시 이름, 범위, 숨김 | 아래 실측대로 **거의 비어 있다** | 풀 수 있다. 단 §4 의 범위 밖이라 이 절에서 정한다 |
| **편집 의미** — 쿼터니언↔오일러, 앵커 프리셋, 에셋 참조 선택 | 전용 드로어가 든다 | **풀 수 없다.** 여기는 전용으로 남는다 |

### 4.6.2 의미 축의 실측

`MetaSchema.h` 에 멤버 속성 셋이 있다 — `display_name_attr`, `range_attr<V>`, `units_attr`.
`ReflectionTypedDraw.h:170-180` 이 앞의 둘을 소비한다. 그런데 선언 쪽이 비어 있다.

| 속성 | 선언 | 소비 | 판정 |
|---|---:|---|---|
| `display_name_attr` | **0** | 있음 | 소비만 있고 생산이 0. 라벨은 언제나 원시 필드명이다 |
| `range_attr<float>` | 3 | 있음 | 셋 다 `BoxColliderComponent.h` 한 파일 |
| `units_attr` | **0** | **없음** | 양쪽 다 0. 죽은 속성 |

숨김·읽기 전용 속성은 **아예 없다.** 인스펙터에서 필드를 거르는 규칙은
`ReflectionTypedDraw.h:159` 의 `std::strcmp(name, "m_isEnabled") == 0` 하나뿐이고 문자열
비교다. 그래서 `m_name`·`m_parentID` 같은 내부 필드가 그대로 화면에 나온다 — 실제로 나온다.

전체 규모는 **필드 416개 · 타입 77개**이고 그중 **181개가 `m_` 접두**다.

### 4.6.3 그래서 이렇게 푼다

**컴포넌트마다 전용 창구를 만들지 않는다.** 그 길은 컴포넌트를 하나 더할 때마다 드로어를
하나 더하게 만들고, 이미 그렇게 쌓인 것이 §1.1 의 3,244줄이다. 대신 축마다 다르게 푼다.

**① 배치는 공통 계층이 든다.** 계획서 계약 1 의 "배치와 값 편집을 분리한다" 가 이것이다.
`ReflectionTypedDraw.h` 의 각 타입 분기는 값 편집만 남기고, 라벨 열·값 열·줄 전환은 §4 의
계층에서 받는다. 이 하나로 리플렉션이 그리는 **모든** 컴포넌트가 기본 렌더와 같은 열에 선다.
전용 드로어를 하나도 더 만들지 않고 얻는다.

**② 이름은 규칙으로 유도하고 속성은 예외에만 붙인다.** 416개 필드에 손으로 `displayName` 을
붙이는 길은 택하지 않는다. 분량도 분량이지만 붙이지 않은 필드가 조용히 원시 이름으로 나오는
것을 막을 수단이 없다 — 지금 선언이 0건인 이유가 그것이다. 드로어가 식별자에서 표시 이름을
유도한다.

```
m_nearPlane  → "Near Plane"
m_isPrimary  → "Is Primary"
position     → "Position"
```

규칙은 `display_label(identifier)` 한 자리에 두고, `m_` 접두 제거 · camelCase 분리 ·
첫 글자 대문자 · 연속 대문자 보존(`m_fov` → "FOV" 는 예외 표로)만 한다. 유도가 틀리는 자리에
`meta::displayName` 을 붙인다 — 그때는 속성이 **예외 표기**라 안 붙은 필드가 기본값으로 잘
나온다. 규칙 자체는 검사가 대표 입력 몇 개로 단정한다.

**③ 숨김 속성을 더한다.** `m_isEnabled` 문자열 비교를 속성으로 옮기고, 내부 전용 필드에 붙인다.
**기본은 "보임" 이다.** 반대로 두면 새 필드가 인스펙터에서 조용히 사라지고, 그 사라짐은 빌드도
검사도 붉게 만들지 않는다. 이 저장소가 이미 겪은 실패 양식이다.

**④ 전용 드로어는 편집 의미가 진짜 다른 것만 남긴다.** 쿼터니언을 오일러로 보여 주는 것,
앵커 프리셋 팝업, 에셋 참조 선택이 그것이다. 이들도 헤더와 배치는 공통 계층이 들고 본문만
그린다(계획서 W2-I1 의 "공통 헤더 / 전용 본문 분리"). 이름이 읽히고 배치가 맞는다고 해서
쿼터니언 4칸이 좋은 편집기가 되지는 않으므로, 이 축은 줄이는 것이 목표가 아니다.

### 4.6.4 이 절이 계획서를 넘는 부분

계획서 W2-I 의 여섯 계약은 **배치** 축만 다룬다. 의미 축(표시 이름·숨김·범위)은 계획서에
없고, 그것이 비어 있다는 사실도 적혀 있지 않다. W2-I3 의 "일반 리플렉션·중첩 필드·공유
드로어 이관 1일" 은 배치 이관만 센 값이다.

의미 축을 같은 슬라이스에 넣으면 I3 은 1일이 아니다. §5 에서 다시 잰다.

## 5. 공수 재계수

계획서는 I0 에서 재계수하고 "초기 추정이 달라지면 본문과 대시보드의 공수를 함께 갱신한다" 고
정했다. 계수 결과 **둘을 조정하고 넷은 유지한다.**

| 단계 | 계획서 | 재계수 | 근거 |
|---|---:|---:|---|
| W2-I0 | 0.5일 | 0.5일 (소진) | 이 문서 |
| W2-I1 | 1일 | 1일 | 중복은 한 자리(`:1756` 의 제외 조건)에서 갈린다. 제거 거부는 이미 서 있다 |
| W2-I2 | 1일 | 1일 | 대상 셋, 그중 RectTransform 의 표는 이미 `EditorPropertyRow` 위에 있다 |
| W2-I3 | 1일 | **2.5일** | 배치 이관은 한 파일(499줄)이라 1일이다. §4.6 의 의미 축(표시 이름 유도·숨김 속성·죽은 `units_attr` 처리)이 416필드·77타입에 걸쳐 +1.5일 |
| W2-I4 | 1.5일 | **2.5일** | 전용 드로어 3,244줄 중 `SoundComponent` 한 드로어가 836줄이고 고정 폭·중첩 묶음이 가장 많다 |
| W2-I5 | 1일 | **1.5일** | 검증 행렬이 폭 4단 × 배율 4단 × 엔티티 4종이고, 변이 주입 항목이 넷이다 |
| **합** | **6일** | **9일** | |

I4 의 증가분이 대부분 `SoundComponent` 한 드로어다. 이 드로어만 별도 슬라이스로 떼는 선택지도
있으나, 계획서가 "일반 리플렉션만 바꾸고 전용 드로어를 남겨 두면 전체 이관 완료로 세지
않는다" 고 했으므로 I4 안에 둔다.

I3 의 증가분은 성질이 다르다 — 계획서에 **없던 축**이다(§4.6.4). 배치만 이관하고 의미 축을
남기면 리플렉션이 그리는 컴포넌트는 열은 맞되 이름이 `m_nearPlane` 인 채로 남는다. 그 상태를
"공통 규칙을 사용한다" 로 판정하면, 남은 차이를 메우려고 컴포넌트마다 전용 드로어를 만드는
길이 다시 열린다. 그래서 같은 슬라이스에 둔다.

**의미 축을 W2-I 밖의 별도 슬라이스로 빼는 선택지도 있다.** 그 경우 W2-I 는 6일 + I4·I5 조정분
으로 7.5일이 되고, 의미 축 1.5일이 별도로 선다. 어느 쪽이든 총량은 같다.

---

## 6. 착수 순서와 걸림돌

I1 부터는 `InspectorWindow.cpp`·`ReflectionTypedDraw.h`·`ImGuiDrawHelper*.cpp` 를 고쳐야 한다.
이 글을 쓰는 시점에 **다른 세션이 그 파일들을 미커밋으로 들고 있다**(아이콘을 Material Symbols
로 바꾸는 작업, 작업 트리에 64개 파일). 같은 파일을 두 세션이 동시에 고치면 한쪽이 사라진다.
I0 은 새 파일 하나라 충돌하지 않지만, I1 착수는 그 세션이 커밋한 뒤로 미룬다.
