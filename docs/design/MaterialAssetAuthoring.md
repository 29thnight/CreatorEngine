# Asset-centered material authoring

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
