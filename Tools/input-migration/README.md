# InputGraph assets and offline migration

## Runtime path

InputGraph is a cataloged `.inputgraph` LX source asset with a matching `.meta` UUIDv4. Editor save validates the graph, writes the source/identity through `EditorAssetDatabase`, creates an immutable CEIG artifact and queues an AssetDepot mount. Source compilation failure leaves the previous runtime lease intact. Layout-only saves skip compilation/publication. A stable signal ID cannot change its value type.

Player uses only the typed CEMF v3 `InputGraph` asset kind and CEIG artifact. The decoder checks identity, representation, schema/compiler/API versions, required capabilities, byte and collection bounds, enum tags and semantic hash before preparing immutable CPU tables. It never parses LX, legacy YAML, callback strings or source paths. Current links resolve current catalog revisions; an existing session owns its old program until a valid replacement is committed.

CEIG stores normalized runtime definitions, including ordered binding and post-combine processors. CPU preparation at decode rebuilds and validates derived indices once. It does not execute input or resolve callbacks. GUID high/low words correspond to the canonical UUID bytes in big-endian order; all wire integer words are encoded little-endian explicitly.

User profiles use CEIO, with user ID, graph ID, archive/schema/compiler/API versions and stable binding IDs. Wrong-user, wrong-graph, orphan, duplicate and incompatible overrides fail closed. The component persists only committed overrides. Invalid reads preserve prior overrides/programs.

## Canonical example

`Dynamic_CPP/Assets/InputGraph/Gameplay.inputgraph` plus its sidecar provides:

- GUID `9ad58e30-9ff7-4f2a-a8c2-435a3c126801`
- Gameplay layer: stable high `0x3217cd6aa3e5272b`, low `1`
- Move: Vector2, stable low `10`, physical WASD, persistent value
- Jump: Button, stable low `20`, physical Space
- Look: Vector2, stable low `30`, relative mouse counts, delta lifetime

The signal high word is `0x3217cd6aa3e5272b`. The example source intentionally has no string callback properties. C# typed consumer/accessor examples use this exact graph identity. `Gameplay.assetset` is a source AssetSet definition for AssetCooker `build-asset-set`, with the asset root set to `Dynamic_CPP/Assets`. Activate its cooked release through the existing AssetSet workflow; do not copy `.inputgraph` into runtime document/CEDO processing.

## Historical corpus disposition

The six files in `fixtures/legacy/` are the original `Dynamic_CPP/Assets/InputMap` files, retained byte-for-byte as offline migration inputs. They are outside Assets and excluded from runtime packaging. There is no legacy runtime importer or fallback.

Static inspection found 26 actions, each containing string callback references. None of their callback implementations (`Player`, `CameraMove`, `MenuKeyObserver`, `ItemUIPopup`, `InputDeviceDetector`) exists among the tracked script sources. The tracked sample scene/prefabs do not reference these maps. Four `Value` actions lack a persisted value type:

- `Player.Move`
- `PlayerKeyBoard.Move`
- `ControllCamera.CameraMove`
- `PlayerSelect.PlayerSelectUIMove`

The historical callbacks were not guessed, silently discarded or claimed migrated. `Pressed` previously invoked a callback repeatedly while held, which must become an explicitly reviewed `ReadHeld` consumer rather than an invented Hold threshold. Released callbacks also require reviewed Completed/cancellation semantics. The keyboard composite previously gave the negative key precedence; the converter requires explicit acceptance of neutral output when opposite directions are summed.

## Offline converter

`migrate_inputmap.py` uses only Python's standard library. It is not linked to the engine. Without `--review` it reports unresolved type and callback decisions as JSON without writing anything:

    python Tools/input-migration/migrate_inputmap.py Tools/input-migration/fixtures/legacy/Player.inputmap

Conversion requires an explicit review JSON bound to the SHA-256 of the exact original bytes. Supply a canonical `graphGuid` and an `actions` object with exactly one decision per action:

- `valueType`: Button, Float or Vector2; unsupported source/type pairs are rejected
- `domain`: Game or UI
- `claim`: PassThrough, OnPress or OnPerformed
- `layerName`: optional explicit label
- `consumerReviewed`: true, with `consumerFile` and `consumerSymbol`
- `dispatch`: Performed, Completed, ReadHeld or ReadValue
- `axisPolicy`: SumOppositesNeutral for legacy keyboard Vector2 composites

The converter emits canonical LX source, a matching sidecar and a `.migration.json` consumer contract report. It does not rewrite gameplay code. Missing decisions, changed source bytes, unknown enum values, unknown/duplicate fields, ambiguous keyboard layout/OEM keys or unsupported value conversions block output. Known Win32 VK keys are mapped to an explicit physical Set-1 scan-code table; a machine's active keyboard layout is never consulted. Stable node/link IDs are deterministically derived from the explicitly supplied graph identity, original action name and structural role.

    python Tools/input-migration/migrate_inputmap.py old.inputmap --review reviewed.json --output New.inputgraph

Review and implement the consumer report, then run native graph round-trip/cook checks before adopting the generated source. The converter refuses to overwrite existing source, meta or reports.

## Verification status

Implementation-time work was restricted to source editing and static inspection. No native build, engine, converter, repository script or test was executed. Tests below are written verification sources, not passing results:

- `test_migrate_inputmap.py`: unresolved corpus, explicit review, source hash, stable output, unsupported enum/field contracts
- `Editor/RenderTests/InputGraph/InputGraphAssetCodecSelfTest.cpp`: source-free round-trip, truncated/unsupported artifacts, last-good retention, stable semantic identity, user override round-trip and orphan rejection

Native Debug/Release/Shipping, non-unity compilation, packaged Player activation, actual hot reload, editor round-trip and rebind profile persistence remain execution gates.
