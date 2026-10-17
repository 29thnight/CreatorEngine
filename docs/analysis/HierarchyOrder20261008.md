# Hierarchy order and drag placement — 2026-10-08

The authoring hierarchy owns sibling order. Entity storage slots are reusable implementation details and must not determine Hierarchy rows. Play uses current instances, while Stop destroys/rebuilds gameplay objects and restores the authoring snapshot; slot reuse previously changed the root display order even though snapshot restoration already preserved root children order.

- HierarchyFlatten now discovers roots and persistent groups in authored hierarchy traversal order; children already used authored child order. Storage slots do not determine display order.
- Row top/bottom quarters place the dragged entity before/after the destination sibling, with an insertion line. The middle half retains reparent-as-child behavior. Empty-area drop retains root parenting.
- Shared Editor MoveRelative and ParentAt record both parent orders using stable EntityReference owners. Reparent plus placement is one Undo entry; Undo/Redo restores old/new sibling order and existing RectTransform world-placement behavior.
- Scene::ReorderChildren validates exact child membership and live handles before mutation, publishes topology changes, and invalidates layout/display consumers. Scene root/self/cycle/locked edits remain rejected.
- CLI object.order <target> <sibling> <before|after> invokes the same operation. scene.order <parent|-> reports ordered children and actual flattened visible roots for acceptance.

Validation is in Tools/regression/verify-hierarchy-order.ps1 using the existing Editor. It checks 21 order snapshots, same-parent movement, cross-parent placement, Undo/Redo before Play and for fresh edits after Stop, two Play/Stop cycles with a runtime mutation, root display agreement and cycle/self rejection. No separate test project is created. The existing policy in EditorPlayModeController clears editing Undo history on successful Play entry; this policy is preserved. Stop restores authored hierarchy order, not the pre-Play Undo stack.

VS 2026 (v18/v145) Debug and Release Editor builds passed. Both Editor runs passed with `snapshots=21 playCycles=2 rejected=2 undoRedo=passed visibleRoots=passed`. The existing Hierarchy flatten source contract passed 72 checks. Build and run logs are under Build/Verification/hierarchy-order-{debug,release}-{build,test}.log, with command results under Build/Verification/HierarchyOrder/{Debug,Release}/results.jsonl. The two rejected commands intentionally test cycles and self placement.

Pointer-driven drop hit areas still require UI validation; CLI acceptance alone does not prove drag ergonomics. Ordering operations and flattened presentation are validated through the same implementation consumed by the UI.
