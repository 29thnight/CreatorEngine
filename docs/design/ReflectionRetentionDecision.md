# 엔진 전용 리플렉션 — 존치 결정

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 현행 서술 정본은 reflgen::schema_of/type_descriptor다. 저작 문서와 cooked archive는 공유 서술의 서로 다른 소비 정책이다.
- 모델/AssetSet 신원은 구현된 UUIDv8/CEMF 계약을 따른다. 다른 저작 객체의 신원까지 일괄 치환하거나 과거 UUIDv4 이주 단계를 재개하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/SceneRuntime/SceneManager.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L28) — `class SceneManager : public Singleton<SceneManager>`
- [Engine/SceneRuntime/SceneManager.h:50](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L50) — `void Editor();`
- [Engine/SceneRuntime/SceneManager.h:188](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L188) — `bool IsEditorSceneLoaded() const { return m_sessionPolicy.RestoresAuthoring() && m_isSimulationSessionActive; }`
- [Engine/SceneRuntime/InputManager.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/InputManager.h#L15) — `class InputManager : public Singleton<InputManager>`
- [Engine/SceneRuntime/InputManager.h:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/InputManager.h#L17) — `friend class Singleton<InputManager>;`
- [Engine/SceneRuntime/InputManager.h:148](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/InputManager.h#L148) — `inline static auto InputManagement = InputManager::GetInstance();`
- [Engine/RenderEngine/Interfaces/Navigation.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/Navigation.h#L7) — `enum class Direction`
- [Engine/RenderEngine/Interfaces/Navigation.h:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/Navigation.h#L15) — `enum class ClipDirection : std::uint8_t`
- [Engine/RenderEngine/Interfaces/Navigation.h:86](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/Navigation.h#L86) — `enum class TextAlignment : std::uint8_t`
- [Editor/EngineGUIWindow/ReflectionImGuiHelper.h:107](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/ReflectionImGuiHelper.h#L107) — `case MethodInputKind::Int: { int result{}; method.invoke(instance, arguments, &result); return "Result: " + std::to_string(result); }`
- [Editor/EngineGUIWindow/ReflectionImGuiHelper.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/ReflectionImGuiHelper.h#L108) — `case MethodInputKind::Float: { float result{}; method.invoke(instance, arguments, &result); return "Result: " + std::to_string(result); }`
- [Editor/EngineGUIWindow/ReflectionImGuiHelper.h:126](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/ReflectionImGuiHelper.h#L126) — `if (nullptr == address) throw std::invalid_argument("method '" + std::string(method.name()) + "': argument is not set");`
- [Engine/SceneRuntime/Prefab.cpp:326](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.cpp#L326) — `const reflgen::type_descriptor* type = Meta::Find(obj->GetTypeID());`
- [Engine/SceneRuntime/Prefab.cpp:330](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.cpp#L330) — `Meta::SerializeInto(nonConst, *type, outNode);`
- [Engine/SceneRuntime/Prefab.cpp:354](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Prefab.cpp#L354) — `const reflgen::type_descriptor* type = Meta::Find(obj->GetTypeID());`
- [Engine/SceneRuntime/ActionMap.cpp:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ActionMap.cpp#L15) — `InputAction* ActionMap::AddAction()`
- [Engine/SceneRuntime/ActionMap.cpp:23](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ActionMap.cpp#L23) — `finalName = baseName + std::to_string(uniqueIndex);`
- [Engine/SceneRuntime/ActionMap.cpp:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ActionMap.cpp#L34) — `void ActionMap::AddButtonAction(std::string name, size_t _playerindex, InputType _inputType, size_t _key, KeyState _state, std::function<void()> _action)`
- [Engine/SceneRuntime/SceneManager.cpp:217](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L217) — `if (componentType && Meta::TypeIDOf(*componentType) == type_guid(BoneComponent))`
- [Engine/SceneRuntime/SceneManager.cpp:269](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L269) — `Meta::Typed::ReadScalar(positionNode, value);`
- [Engine/SceneRuntime/SceneManager.cpp:958](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L958) — `Meta::SerializeInto(gameObject, Meta::TypeOf<Entity>(),`
- [Engine/SceneRuntime/PrefabUtility.cpp:97](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrefabUtility.cpp#L97) — `Meta::SerializeDocument(&obj, Meta::TypeOf<Entity>());`
- [Engine/SceneRuntime/PrefabUtility.cpp:180](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrefabUtility.cpp#L180) — `if (const reflgen::type_descriptor* type = Meta::FindTypeByInstance(comp.get()))`
- [Engine/SceneRuntime/PrefabUtility.cpp:214](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrefabUtility.cpp#L214) — `Meta::DeserializePrefab(comp, *type, node, overriddenNames);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

작성: 2026-08-07 · 근거: 소스 실측 (`Meta::*` 호출 지점 · `*.generated.h` 분포 · 헤더툴 pre-build 배선)

## 결론

**존치.** 폐지·대체 논의는 종결한다. 다만 아래 §4의 재배치 3건은 부채로 남기고, 실행은
DX12 라이브 파이프라인 작업 이후로 미룬다.

**후속(2026-09-30)**: 존치한 것은 리플렉션의 **역할**이고, 표현은 그 뒤 두 번 바뀌었다 — PHASE 18이 매크로를
`reflect()` 레시피로, 2026-09 reflgen 전환이 레시피를 헤더 속성(`[[reflgen::reflect]]`)과 생성된 서술로
옮겼다. 아래 존치 근거 셋(자산 포맷·핫리로드 상태 보존·인스펙터 자동 생성)은 reflgen 전환에서도 그대로
지켰다 — 저장 포맷은 바이트 동일이다. 현재 설계는 [ReflectionDesign.md](ReflectionDesign.md)에 있다.

## 1. 대상 정의

"엔진 전용 리플렉션"은 `Reflect()` 갈래(`ReflectionField` / MetaGenerator / `*.generated.h`)를
가리킨다. 사용자 스크립트의 `ScriptReflect()` 갈래(ScriptReflectionHeaderTool)는 별개 체계이나
`Meta::Type` 표현과 직렬화 경로를 공유하므로 함께 검토했다.

## 2. 실측 규모

| 항목 | 규모 |
|---|---|
| 코어 헤더 | 11개 · 약 2,900줄 (`Utility_Framework/Reflection*.h`) |
| 생성 헤더 | 155개 · 2,424줄 (평균 16줄, 얇은 스텁) |
| 헤더툴 | 3개 바이너리 — MetaGenerator · ScriptReflectionHeaderTool · ScriptReflectionAndFactoryClear |
| pre-build 배선 | `Utility_Framework` · `ScriptBinder` (MetaGenerator), `Dynamic_CPP` (스크립트 툴 2종) |

리플렉션 보유 타입 분포: `Dynamic_CPP/Assets/Script` 71 · `ScriptBinder` 58 ·
`RenderEngine`(+`Interfaces`) 25 · `EngineEntry` 1.

## 3. 존치 근거

폐지 시 대체 비용이 비현실적이다. 세 가지가 결정적이다.

1. **자산 포맷이 리플렉션 스키마에 묶여 있다.** `.yaml` 씬·프리팹이 `Meta::Serialize`가 생성한
   필드 구조 그대로다. 걷어내면 기존 씬 자산 전부가 마이그레이션 대상이 된다.
   - 근거: `SceneManager.cpp`(Serialize/Deserialize 8건) · `Prefab.cpp` · `PrefabUtility.cpp` ·
     `ComponentFactory.cpp`(31건)
2. **핫리로드 상태 보존이 이 경로 하나뿐이다.** 스크립트 DLL 교체 시 컴포넌트 상태를
   Serialize → 재로드 → Deserialize 왕복으로 살린다.
   - 근거: `HotLoadSystem.cpp` (`ScriptReflect()` 8건 + `Meta::*` 9건)
3. **인스펙터 UI가 자동 생성이다.** 폐지하면 컴포넌트 154종에 수동 ImGui 코드를 써야 한다.
   - 근거: `InspectorWindow.cpp`(`Meta::Find` 9건 · `Meta::DrawObject`) · `ImGuiDrawHelper*.cpp`

~~부수적으로 문자열→메서드 디스패치도 리플렉션에 의존한다:
`AnimationEventBridge.cpp:74,106` · `ActionMap.cpp:337` (`Meta::InvokeMethodByMetaName`).~~
**정정(2026-08-16)**: 위 부수 근거는 스테일이었다 — CoreCLR 레거시 은퇴(9-4)로 두
호출처가 소멸해 `InvokeMethodByMetaName`은 호출 0건이 됐고, PHASE 18 CT2에서
삭제했다. 존치 결론 자체는 본문 1~3 근거로 여전히 유효하다
([ReflectionSystemAnalysis.md](../analysis/ReflectionSystemAnalysis.md) F-7).

**대체재 부재**: C++26 정적 리플렉션은 현 MSVC 툴체인에 없고, RTTR 등 서드파티는 등록 코드를
다시 손으로 작성해야 하므로 교체 이득이 없다.
**정정(2026-09-30)**: "대체재 부재"는 등록 코드를 손으로 쓰는 라이브러리만 본 판단이었다. 헤더를 읽어
서술을 생성하는 reflgen(libclang 생성기, C++26 native reflection과 같은 반영 범위)으로 옮겼다 — 손으로 쓰는
등록 코드가 없고 자산 포맷을 바꾸지 않았다.

## 4. 잔여 부채 (기록만 — 실행 보류)

### R1. `ReflectionImGuiHelper.h` 소속 정정 — 우선순위 높음, 위험 낮음

924줄짜리 **에디터 전용** 코드가 최하위 코어(`Utility_Framework`)에 있으면서 상위 레이어를
역참조한다 — 8~12행에서 `SceneManager.h` · `InputManager.h`를 include.
실 소비자는 `EngineGUIWindow`의 6개 파일뿐(`InspectorWindow.cpp`,
`ImGuiDrawHelper{PlayerInput,ModuleBehavior,MeshRenderer,Animator}.cpp`, `AssetBundleWindow.cpp`).

→ `EngineGUIWindow`로 이관. PHASE 5-3(에디터 UI 분리)과 한 몸이므로 그때 함께 처리한다.

### R2. `ComponentFactory` 수동 분기 — 리플렉션 가치 미회수

리플렉션 레지스트리가 있는데도 컴포넌트 타입마다 `Meta::Deserialize` 분기를 손으로 나열한다(31건).
컴포넌트를 추가할 때마다 팩토리를 함께 고쳐야 하는 구조다.

→ 레지스트리 기반 디스패치로 전환. 역직렬화 경로 전체를 건드리므로 씬 로드 회귀 검증
(`Tools/regression`) 필수.

### R3. 헤더툴 3종 / 이중 갈래 통합 — ~~위험도 최상, 별도 슬라이스~~ 해소(2026-08-16)

~~`Reflect()` / `ScriptReflect()` 이중 갈래와 헤더툴 3개는 통합 여지가 있으나 핫리로드 경계와
얽혀 있다. 단독 슬라이스로 떼어내기 전에는 착수하지 않는다.~~
**정정**: "통합"이 아니라 "삭제"였다 — ModuleBehavior 은퇴(9-4)로 `ScriptReflect()`
갈래의 베이스 virtual 선언·호출처·빌드 배선이 전부 소멸한 상태였음이 실측됐고,
PHASE 18 CT2가 매크로 갈래와 고아 바이너리 2종(ScriptReflectionHeaderTool ·
ScriptReflectionAndFactoryClear)을 제거했다. 남은 헤더툴은 MetaGenerator 1종이며
그 은퇴는 [ReflectionRedesignPlan.md](../plans/archive/ReflectionRedesignPlan.md) CT7이 승계한다.

## 5. 연계

- R1은 [Phase5CouplingPlan.md](../plans/archive/Phase5CouplingPlan.md) §3-C4 / 5-3(에디터 UI 분리)에 귀속.
- 헤더 이동 시 MetaGenerator 스캔 경로 확인 필수 — 같은 문서 §3-B의 `Navigation.h` 항목과 동일 함정.
  **갱신(2026-09-30)**: MetaGenerator는 은퇴했고 지금은 reflgen이 프로젝트의 `ClInclude` 를 읽는다. 반영
  모듈(`RenderEngine`·`SceneRuntime`·`Editor`) 밖으로 헤더를 옮기면 서술이 다른 프로젝트에 주입되지 않고
  등록되지도 않는 같은 계열의 함정이 남는다([ReflectionDesign.md](ReflectionDesign.md) §1.2).

</details>
