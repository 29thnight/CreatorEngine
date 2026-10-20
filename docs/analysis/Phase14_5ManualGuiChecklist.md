# PHASE 14.5 조건 ② — GUI 수동 조작 체크리스트

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- UI는 현행 Canvas/UIManager·Component와 값 기반 렌더 준비 경계를 소비한다. GC 객체와 렌더 스냅샷의 수명을 분리한다.
- scene-local 입력 routing·layout dirty·UIDrawSnapshot·전역 UIManager 퇴역은 UI 계획의 실제 소비자/제품 수용으로 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp:345](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp#L345) — `std::is_sorted(std::begin(kSeeds), std::end(kSeeds),`
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp:350](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp#L350) — `std::adjacent_find(std::begin(kSeeds), std::end(kSeeds),`
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp:357](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp#L357) — `const auto found = std::lower_bound(`
- [ScriptCore/Animator.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L14) — `public sealed class Animator : NativeComponent`
- [ScriptCore/Animator.cs:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L18) — `public void SetBool(string name, bool value) => Native.AnimatorSetBool(OwnerHandle, name, value);`
- [ScriptCore/Animator.cs:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L19) — `public void SetFloat(string name, float value) => Native.AnimatorSetFloat(OwnerHandle, name, value);`
- [Engine/SceneRuntime/Animator.h:49](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L49) — `struct [[reflgen::reflect]] BoneTransformConstraint final`
- [Engine/SceneRuntime/Animator.h:136](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L136) — `void CreateController(std::string name);`
- [Engine/SceneRuntime/Animator.h:137](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L137) — `std::shared_ptr<AnimationController> CreateController_UI();`
- [Editor/EngineGUIWindow/InspectorWindow.h:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/InspectorWindow.h#L11) — `class Entity;`
- [Editor/EngineGUIWindow/InspectorWindow.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/InspectorWindow.h#L19) — `void DrawAddComponent(Entity* entity);`
- [Editor/EngineGUIWindow/InspectorWindow.h:48](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/InspectorWindow.h#L48) — `void ImGuiDrawHelperTransformComponent(Entity* gameObject);`
- [Editor/EngineGUIWindow/HierarchyWindow.h:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.h#L6) — `class Entity;`
- [Editor/EngineGUIWindow/HierarchyWindow.h:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.h#L7) — `class HierarchyWindow`
- [Editor/EngineGUIWindow/HierarchyWindow.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyWindow.h#L9) — `public:`
- [Editor/EngineGUIWindow/SceneViewWindow.h:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.h#L38) — `const std::vector<gc::trace_ref<Entity>>& slots);`
- [Editor/EngineGUIWindow/SceneViewWindow.h:42](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.h#L42) — `const Ray& ray, const std::vector<gc::trace_ref<Entity>>& slots);`
- [Editor/EngineGUIWindow/SceneViewWindow.h:71](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/SceneViewWindow.h#L71) — `Ray CreateRayFromCamera(const FrameCameraSnapshot& camera, const ImVec2& mousePos);`
- [Editor/EngineEntry/EditorObjectOperations.h:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorObjectOperations.h#L33) — `CommandCore::CommandResult Rename(EntityHandle target, const std::string& name);`
- [Editor/EngineEntry/EditorObjectOperations.h:82](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorObjectOperations.h#L82) — `CommandCore::CommandResult NavigateSelection(Scene* scene, int direction);`
- [Editor/EngineEntry/EditorObjectOperations.h:85](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorObjectOperations.h#L85) — `CommandCore::CommandResult MaterialMode(const std::vector<own::shared_owner<Material>>& materials, MaterialRenderingMode mode);`
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.h:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.h#L13) — `struct DescriptorSeed`
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.h:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.h#L65) — `const DescriptorSeed* FindDescriptorSeed(std::string_view canonical);`
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.h:67](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.h#L67) — `std::size_t DescriptorSeedCount() noexcept;`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

[종결 검토](Phase14_5Closure.md)의 재정의가 남긴 마지막 조건이다. 자동 게이트는 **HTTP 경로로** 공통
편집 API 를 때려 실제 상태 복원을 확인했다. 그것이 **위젯 조작을 대신했다고 주장하지 않는다** — 같은
서비스라도 GUI 는 자기 입력 확정·선택 상태·다중 선택 묶음을 거쳐 들어가고, 그 구간은 자동 게이트 밖이다.

## 대상을 어떻게 정했나

`CommandDescriptorSeeds.cpp` 의 `undoable = true` 를 정본으로 삼는다. **24 개**다.

[종결 검토](Phase14_5Closure.md)와 [CommandSurfaceTable.md](CommandSurfaceTable.md) 는 **21 개**라고 적는다.
그 수는 2026-09-06 당시 옳았다 — **틀린 것이 아니라 그 뒤에 늘었다.** `object.icon` · `object.lock` ·
`scene.navigate` 셋을 2026-09-13 의 `4737ec60`(PHASE 21 에디터 워크스페이스 재설계)이 더했고, 도표의
마지막 갱신은 그 전날인 `0ec60573`(09-12)이다. 도표에는 Undo 열이 빈 것이 아니라 **행 자체가 없다**.
같은 이유로 도표는 제품 명령 28 개를 모른다(`editor.*` 16 · `play.*` 5 · 위 셋 · `scene.populate` ·
`script.create*` · `dx12.validation`).

★ **이 수는 고정이 아니다.** 다른 페이즈가 공통 편집 API 를 쓰면 14.5 의 Undo 표면이 함께 늘어난다.
확인에 착수할 때 seed 표에서 **다시 뽑아** 이 표와 맞대라 — 줄 수가 다르면 그 사이에 늘어난 것이다.
이 체크리스트는 도표가 아니라 seed 표에서 유도했다. 도표 갱신은 별도 과제다.

## 판정 규칙

항목마다 넷을 본다. 하나라도 어긋나면 그 줄은 실패이며, 실패를 지우지 말고 관측값을 적는다.

1. **반영** — GUI 조작이 실제 상태를 바꾼다(되읽어서 확인한다. "명령이 성공했다" 로 그치지 않는다).
2. **Undo** — 한 번에 조작 **전** 상태로 돌아간다. 여러 객체를 한 번에 바꾼 조작은 **Undo 한 항목**이다.
3. **Redo** — 다시 조작 후 상태가 된다.
4. **무변경·오입력** — 값이 그대로이거나 잘못된 입력이면 **Undo 항목이 늘지 않는다**.

`undo` 자신은 3·4 를 스택 깊이로 본다. 프리팹 정의 저장·씬 저장은 영속 자산 서비스라 이 표에 없다
(계획서 §편집 HTTP 계약). 프리팹 **소환**은 씬 편집이므로 있다.

## 확인 전 준비

- Debug 또는 Release Editor 를 띄우고 빈 씬에서 시작한다. 재생 중이 아닌 **편집 모드**다.
- 각 항목은 독립으로 본다. 앞 항목의 Undo 스택이 남아 있으면 4 번 판정이 흐려진다.
- 같은 조작을 CLI 로도 한 번 해 보면 "공통 경로" 주장이 함께 확인된다(선택).

## A. GUI 호출처가 소스에서 확인된 18 개

`EditorObjectOperations::<API>` 를 GUI 파일이 실제로 부르는 것을 확인한 항목이다.
경로는 **호출 관계**이지 화면 위치가 아니다 — 패널 안 어디를 누르는지는 확인하며 채운다.

| # | 명령 | 공통 API | GUI 패널 | 조작 · 기대 | 반영 | Undo | Redo | 무변경 |
|---|---|---|---|---|:-:|:-:|:-:|:-:|
| 1 | `component.add` | `AddComponent` | InspectorWindow | 오브젝트에 컴포넌트를 붙인다 | ☐ | ☐ | ☐ | ☐ |
| 2 | `component.remove` | `RemoveComponent` | InspectorWindow | Remove an optional component with Undo | ☐ | ☐ | ☐ | ☐ |
| 3 | `object.create` | `Create` | HierarchyWindow | 빈 오브젝트를 만든다(Empty/Light/Camera/Mesh) | ☐ | ☐ | ☐ | ☐ |
| 4 | `object.delete` | `Delete` | HierarchyWindow | Delete an object subtree with Undo | ☐ | ☐ | ☐ | ☐ |
| 5 | `object.icon` | `SetIcon` | InspectorWindow | Set entity image preset with Undo | ☐ | ☐ | ☐ | ☐ |
| 6 | `object.lock` | `SetEditLocked` | InspectorWindow | Lock or unlock entity authoring with Undo | ☐ | ☐ | ☐ | ☐ |
| 7 | `object.parent` | `Parent` | HierarchyWindow | 오브젝트의 부모를 바꾼다(-는 씬 루트로 올린다) | ☐ | ☐ | ☐ | ☐ |
| 8 | `object.rename` | `Rename` | InspectorWindow | Rename through the shared editor undo transaction | ☐ | ☐ | ☐ | ☐ |
| 9 | `object.transform` | `Transform` | InspectorWindow | 변환을 지정한다(회전은 도) | ☐ | ☐ | ☐ | ☐ |
| 10 | `prefab.instantiate` | `InstantiatePrefab` | SceneViewWindow | 프리팹을 씬에 소환한다 | ☐ | ☐ | ☐ | ☐ |
| 11 | `render.matmode` | `MaterialMode` | ImGuiDrawHelperMeshRenderer | 오브젝트 재질의 렌더링 모드를 바꾼다 | ☐ | ☐ | ☐ | ☐ |
| 12 | `scene.navigate` | `NavigateSelection` | InspectorWindow | Navigate entity selection history | ☐ | ☐ | ☐ | ☐ |
| 13 | `scene.select` | `Select` | HierarchyWindow, SceneViewWindow | 오브젝트를 에디터 선택으로 지정한다 | ☐ | ☐ | ☐ | ☐ |
| 14 | `ui.anchor` | `CapturePropertyEdit,CommitPropertyEdits,Properties,PropertyEdit` | SceneViewWindow | 앵커를 직접 지정한다 | ☐ | ☐ | ☐ | ☐ |
| 15 | `ui.pos` | `CapturePropertyEdit,CommitPropertyEdits,Properties,PropertyEdit` | SceneViewWindow | UI anchored position을 편집한다 | ☐ | ☐ | ☐ | ☐ |
| 16 | `ui.screenpos` | `CapturePropertyEdit,CommitPropertyEdits,Properties,PropertyEdit` | SceneViewWindow | UI 화면 위치를 편집한다 | ☐ | ☐ | ☐ | ☐ |
| 17 | `ui.size` | `CapturePropertyEdit,CommitPropertyEdits,Properties,PropertyEdit` | SceneViewWindow | UI 크기를 편집한다 | ☐ | ☐ | ☐ | ☐ |
| 18 | `undo` | `UndoRedo` | HierarchyWindow | 에디터의 Ctrl+Z / Ctrl+Y와 같은 호출 | ☐ | ☐ | ☐ | ☐ |

## B. GUI 호출처를 찾지 못한 6 개 — 먼저 볼 것

아래는 "GUI 에 기능이 없다" 는 뜻이 **아니다**. 셋 중 하나다 —
① GUI 가 공통 API 가 아닌 **다른 경로**로 같은 편집을 한다(그렇다면 LC6 의 공통 경로 주장에 구멍이 있다),
② 호출이 메뉴·명령 디스패치를 거쳐 정적 grep 에 안 잡힌다, ③ 애초에 GUI 표면이 없다.
**어느 쪽인지 확인하는 것이 이 절의 일이다.** ②·③ 이면 그 사실을 적고 넘어가며, ① 이면 결함이다.

| # | 명령 | 인자 | 동작 | 판정(①/②/③) | 반영 | Undo | Redo | 무변경 |
|---|---|---|---|---|:-:|:-:|:-:|:-:|
| 1 | `animator.param` | `<오브젝트> <파라미터> <bool\|float\|int\|trigger>` | Animator 파라미터를 저작한다 | | ☐ | ☐ | ☐ | ☐ |
| 2 | `model.place` | `<이름>` | 임포트한 모델을 활성 씬에 배치한다 | | ☐ | ☐ | ☐ | ☐ |
| 3 | `object.duplicate` | `<오브젝트> [새 이름]` | 오브젝트를 복제한다(에디터 Ctrl+D와 같은 원시 함수) | | ☐ | ☐ | ☐ | ☐ |
| 4 | `object.property` | `<오브젝트> <컴포넌트> <필드> <값>` | 리플렉션으로 프로퍼티를 설정한다 | | ☐ | ☐ | ☐ | ☐ |
| 5 | `tag.add` | `<name>` | Add and persist a project tag with Undo | | ☐ | ☐ | ☐ | ☐ |
| 6 | `tag.remove` | `<name>` | Remove and persist a project tag with Undo | | ☐ | ☐ | ☐ | ☐ |

## 기록

확인이 끝나면 결과를 이 문서에 그대로 남기고 [종결 검토](Phase14_5Closure.md)의 조건 ② 를 닫는다.
**통과한 것만 적지 않는다** — 실패와 미확인도 같은 표에 남긴다. 이 저장소는 "검사 0 건·빈 집합을
성공으로 접지 않는다" 를 관례로 삼는다.

</details>
