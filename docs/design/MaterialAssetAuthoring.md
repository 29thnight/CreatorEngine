# Asset-centered material authoring

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [ScriptCore/MeshRenderer.cs:13](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L13) — `public sealed class MeshRenderer : NativeComponent`
- [ScriptCore/MeshRenderer.cs:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L15) — `public string MaterialName => Native.MeshGetMaterialName(OwnerHandle);`
- [ScriptCore/MeshRenderer.cs:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/MeshRenderer.cs#L24) — `public void InstantiateMaterial(string newName) => Native.MeshInstantiateMaterial(OwnerHandle, newName);`
- [Engine/SceneRuntime/Socket.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.h#L5) — `class Entity;`
- [Engine/SceneRuntime/Socket.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.h#L18) — `math::matrix4x4 m_local{ math::matrix4x4::identity() };`
- [Engine/SceneRuntime/Socket.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.h#L30) — `math::matrix4x4 m_offset{ math::matrix4x4::identity() };`
- [Engine/RenderEngine/DataSystem.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L30) — `own::shared_owner<const Texture> descriptor;`
- [Engine/RenderEngine/DataSystem.h:44](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L44) — `std::vector<own::shared_owner<const Material>> materials;`
- [Engine/RenderEngine/DataSystem.h:45](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L45) — `std::vector<own::shared_owner<const Texture>> textures;`
- [Engine/SceneRuntime/MeshRenderer.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L19) — `class [[reflgen::reflect]] MeshRenderer : public meta::identity<MeshRenderer, Component>`
- [Engine/SceneRuntime/MeshRenderer.h:32](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L32) — `MeshRenderer();`
- [Engine/SceneRuntime/MeshRenderer.h:41](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/MeshRenderer.h#L41) — `virtual ~MeshRenderer() override;`
- [Engine/Utility_Framework/Ownership.h:1](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Ownership.h#L1) — `#pragma once`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/SceneRuntime/Socket.cpp:7](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.cpp#L7) — `Socket::Socket()`
- [Engine/SceneRuntime/Socket.cpp:12](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.cpp#L12) — `void Socket::ReleaseManagedResources()`
- [Engine/SceneRuntime/Socket.cpp:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Socket.cpp#L14) — `if (auto* manager = SceneManager::GetIfAlive())`
- [Engine/RenderEngine/DataSystem.cpp:55](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L55) — `for (const auto& entry : file::directory_iterator(directory))`
- [Engine/RenderEngine/DataSystem.cpp:129](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L129) — `throw std::runtime_error("Asset image decoder apartment initialization failed.");`
- [Engine/RenderEngine/DataSystem.cpp:139](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L139) — `std::ranges::transform(value, value.begin(), [](unsigned char character)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

The Material Node Editor edits existing material assets independently of Scene selection. It does not introduce a second asset system.

## Ownership and workflow

- `.shadergraph` owns nodes, links, graph parameter declarations, and connected Surface Settings
- `.asset` owns its graph GUID, default parameter values, texture references, and material flags
- `MeshRenderer` owns the material asset reference and optional instance overrides
- An open editor session owns its draft graph/defaults, private compiled candidate, and last valid preview

Open the Material Node Editor from the window menu without selecting an object. Use **New Material Asset** or **Open Material Asset**. The Inspector's **Open Node Editor** opens the referenced base asset, not a mesh's instance overrides. An unbound/imported material creates an independent draft; existing legacy assets are retained rather than converted in place.

1. Edit nodes and graph parameters. A newly added Parameter node has **Declare Parameter** in Node Details. Graph declarations and material defaults are separate controls. Socket-backed imported parameters retain their existing effective-default source
2. The editor coalesces changes for 250 ms and prepares the latest draft on the existing job scheduler. One pending job is allowed per session. Obsolete completions are discarded. CPU compilation produces an isolated owned generation; texture/instance binding returns to the editor thread
3. **Apply to Material Asset** publishes the prepared valid candidate to the existing cache. Resident assets sharing the edited graph are rebuilt with their own defaults before publication. If any candidate fails validation, publication is rejected
4. **Save Material Asset** persists the already-applied material and graph. Apply is required before Save. Save checks disk changes, deletions, cache ownership, and asset identities. It does not assign a mesh
5. **Assign Asset to Selected Mesh** explicitly assigns a saved accepted asset to the current selected, unlocked MeshRenderer. Save the Scene to persist that reference

**Save As Copy** in the graph editor creates independent material and graph identities in an unassigned draft. The Inspector's material-copy action instead shares the graph, copies the current material values into the new asset defaults, and offers assignment separately. Reload Saved Draft reads the exact saved pair into the editor without publishing it; Apply remains explicit.

Material node descriptions are available only by hovering the existing information-font glyph at the upper-right of each node header. The icon reserves title space and does not start node selection/drag; pin interaction remains higher priority.

The generic graph canvas supports stationary right-click on a connected input to disconnect, and dragging a new output onto a connected single input to replace its link. Replacement is one undo/revision and invalid replacement preserves the original graph. Right-drag still pans.

## Publication and lifetime contracts

- Accepted material owners use the same immutable ownership_cpp and bounded cache contracts as AssetDepot. Apply clones the draft, validates every replacement and retained-budget admission before compare-and-publish, then refreshes consumers using the accepted owner. Oversized replacement batches fail without publishing
- Isolated and queued graph preparations share a never-reused generation identity space. Authoring publication invalidates stale preparation tickets; cooked AssetDepot graphs retain the same content digest and surface-alpha metadata
- Draft compilation never writes the accepted graph-generation or material caches. Generated-source scratch directories are per request and released after the owned product is created
- Job captures own the graph, paths, output, and error text; they never capture a Session or DataSystem pointer. The document/revision pair must still match before accepting completion
- A compile or binding failure leaves the last good preview and accepted Scene material intact. Pending and error states are visible in the editor
- Preview capture only copies immutable source owners. It does not compile or load the selected mesh. The preview floor uses the same owned background preparation
- Editor shutdown stops new draft submissions, drains pending jobs outside the existing session lock, and releases preview owners before renderer/compiler teardown
- The material cache publication revision gates Scene refresh at the GT render-proxy commit boundary. Unchanged frames do not add per-mesh cache lookups
- A mesh retains the accepted base used to interpret its overrides. Explicit-override provenance survives later asset defaults that happen to equal an override, serialization, and undo/redo
- A removed asset or an incompatible override keeps the last valid mesh instance. Unresolved serialized references retain their original payload for later recovery
- Graph reference serialization uses the existing typed material instance codec, with `ref` and a `graphOverrides` delta; legacy and inline material readers remain available
- Explicit alpha mode follows the final connected Surface Settings closure through groups. Its canonical generated-source marker is retained in cooked graph products, so shared-graph assets use the same routing in Editor and source-less Player. Marker-less legacy products retain their material-document mode
- External `.asset` and `.shadergraph` changes use validated replacement. A failed reload retains the accepted generation. Cold loads must recheck the current graph before insertion so a concurrent Apply cannot leave a late cache entry on an old generation
- Saving the material, graph, and their sidecars is rollback-capable, not a multi-file filesystem-atomic transaction. Authored final-state fingerprints suppress delayed watcher events from successful writes and rollback; an external differing state is still processed

## Boundaries

This change does not add shader-function libraries, general UE feature parity, or new rendering backends. Transparent surface routing is preserved, but the isolated sphere preview renderer still displays only Opaque and Masked surfaces; the editor states that limitation instead of showing an opaque substitute. GPU pipeline preparation remains asynchronous in the existing renderer after CPU draft compilation.

Graph document undo/redo handles node/link/socket/layout operations. Material-default and graph-declaration metadata controls are draft edits outside that graph-only undo history. Closing the panel keeps its draft in memory; unsaved drafts are not a replacement for saving before exiting the editor.

## Validation

The integration with master `d5a6a26a78da93686b8168429183ee7e0d50a8de` was reviewed for immutable cache ownership, asynchronous preparation, cooked metadata, material references, and model texture context. Only source/static checks were performed for this change. No compiler, build, engine execution, unit/runtime tests, or benchmarks were run.

Owner validation checklist:

- Build Editor and Player in the supported Windows configurations; check DX12 and Vulkan graph paths and cook/package compatibility
- With no mesh selected, create a material, add/edit nodes, inspect the draft preview, Apply, Save, close/reopen, then assign to a mesh
- Share the asset across two meshes. Override one mesh's roughness. Change the asset default: the inherited mesh updates and the override remains. Change the default to equal the override, then change it again; the override must still remain
- Save/reopen the Scene and exercise undo/redo of assignment and instance edits across asset Apply operations
- Open two distinct material assets sharing one graph, change a graph parameter and alpha mode, Apply/Save, then cook and inspect both in Player
- Try invalid graph links, a missing texture, a private/deleted parameter with existing overrides, and a failing shader compile. Accepted assets and last-good previews must stay valid
- Modify the draft repeatedly during compilation, change the active document/Scene/selection, close the panel, and exit while a job is pending. No obsolete result may publish to another session or object
- Create independent graph-editor copies and shared-graph Inspector copies; cancel/switch selection during modal UI. No unintended mesh assignment or reference loss
- Delete, externally edit, or rename material/graph files while open; verify conflict/reload messages, last-good behavior, and recovery. Force a pair-save failure and confirm the rollback watcher does not undo the accepted in-memory Apply
- Exercise imported/legacy materials, nested/dotted names, exposed textures, socket-backed parameters, masked/transparent routing, and valid/invalid link replacement with one-step undo
- Hover information icons at different zoom/DPI values, including collapsed/readonly nodes; check tooltip wrapping, header/title clipping, pin drag, node dragging, and overlapping windows

</details>
