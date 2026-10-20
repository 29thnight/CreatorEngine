# Hierarchy order and drag placement — 2026-10-08

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- SceneManager는 GCCE domain과 Scene root를 소유한다. Scene→Entity→Component의 trace 참조와 논리적 제거·핸들 무효화를 분리한다.
- 일반 new/unique_ptr 기반 Scene 그래프를 새 이행 목표로 사용하지 않는다. 자산·SDK·GPU 수명은 독립 owner/완료 계약을 따른다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-hierarchy-order.ps1:3](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-hierarchy-order.ps1#L3) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-hierarchy-order.ps1:31](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-hierarchy-order.ps1#L31) — `$p = Start-Process -FilePath $exe -WindowStyle Hidden -WorkingDirectory (Split-Path $exe) -PassThru `
- [Tools/regression/verify-hierarchy-order.ps1:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-hierarchy-order.ps1#L33) — `-RedirectStandardOutput (Join-Path $out 'stdout.log') -RedirectStandardError (Join-Path $out 'stderr.log')`
- [Editor/EngineGUIWindow/HierarchyFlatten.h:56](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyFlatten.h#L56) — `class Scene;`
- [Editor/EngineGUIWindow/HierarchyFlatten.h:61](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyFlatten.h#L61) — `enum class hierarchy_row_kind : std::uint8_t`
- [Editor/EngineGUIWindow/HierarchyFlatten.h:83](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyFlatten.h#L83) — `std::uint64_t revision{};        ///< HierarchyStore::Revision()`
- [Editor/EngineEntry/EditorPlayModeController.h:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorPlayModeController.h#L65) — `enum class PlayState : std::uint8_t`
- [Editor/EngineEntry/EditorPlayModeController.h:74](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorPlayModeController.h#L74) — `enum class InputOwner : std::uint8_t { Editor, Game };`
- [Editor/EngineEntry/EditorPlayModeController.h:76](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorPlayModeController.h#L76) — `const char* PlayStateName(PlayState state) noexcept;`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Editor/EngineGUIWindow/HierarchyFlatten.cpp:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyFlatten.cpp#L18) — `const std::vector<editor::hierarchy_flat_row>& editor::HierarchyFlatView::Rows(`
- [Editor/EngineGUIWindow/HierarchyFlatten.cpp:48](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyFlatten.cpp#L48) — `void editor::HierarchyFlatView::Toggle(int index)`
- [Editor/EngineGUIWindow/HierarchyFlatten.cpp:58](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/HierarchyFlatten.cpp#L58) — `bool editor::HierarchyFlatView::IsSlotExpanded(const Entity* obj, int index) const`
- [Editor/EngineEntry/EditorPlayModeController.cpp:47](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorPlayModeController.cpp#L47) — `PlayModeStatus PlayModeController::Status()`
- [Editor/EngineEntry/EditorPlayModeController.cpp:53](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorPlayModeController.cpp#L53) — `PlayState PlayModeController::CurrentState() noexcept`
- [Editor/EngineEntry/EditorPlayModeController.cpp:58](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorPlayModeController.cpp#L58) — `InputOwner PlayModeController::CurrentOwner() noexcept`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

The authoring hierarchy owns sibling order. Entity storage slots are reusable implementation details and must not determine Hierarchy rows. Play uses current instances, while Stop destroys/rebuilds gameplay objects and restores the authoring snapshot; slot reuse previously changed the root display order even though snapshot restoration already preserved root children order.

- HierarchyFlatten now discovers roots and persistent groups in authored hierarchy traversal order; children already used authored child order. Storage slots do not determine display order.
- Row top/bottom quarters place the dragged entity before/after the destination sibling, with an insertion line. The middle half retains reparent-as-child behavior. Empty-area drop retains root parenting.
- Shared Editor MoveRelative and ParentAt record both parent orders using stable EntityReference owners. Reparent plus placement is one Undo entry; Undo/Redo restores old/new sibling order and existing RectTransform world-placement behavior.
- Scene::ReorderChildren validates exact child membership and live handles before mutation, publishes topology changes, and invalidates layout/display consumers. Scene root/self/cycle/locked edits remain rejected.
- CLI object.order <target> <sibling> <before|after> invokes the same operation. scene.order <parent|-> reports ordered children and actual flattened visible roots for acceptance.

Validation is in Tools/regression/verify-hierarchy-order.ps1 using the existing Editor. It checks 21 order snapshots, same-parent movement, cross-parent placement, Undo/Redo before Play and for fresh edits after Stop, two Play/Stop cycles with a runtime mutation, root display agreement and cycle/self rejection. No separate test project is created. The existing policy in EditorPlayModeController clears editing Undo history on successful Play entry; this policy is preserved. Stop restores authored hierarchy order, not the pre-Play Undo stack.

VS 2026 (v18/v145) Debug and Release Editor builds passed. Both Editor runs passed with `snapshots=21 playCycles=2 rejected=2 undoRedo=passed visibleRoots=passed`. The existing Hierarchy flatten source contract passed 72 checks. Build and run logs are under Build/Verification/hierarchy-order-{debug,release}-{build,test}.log, with command results under Build/Verification/HierarchyOrder/{Debug,Release}/results.jsonl. The two rejected commands intentionally test cycles and self placement.

Pointer-driven drop hit areas still require UI validation; CLI acceptance alone does not prove drag ergonomics. Ordering operations and flattened presentation are validated through the same implementation consumed by the UI.

</details>
