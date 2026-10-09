# Source-free AssetSet package composition

Status: source implementation and unrun fixtures. No build, package, smoke test, shader compilation, or performance result is claimed.

## Two explicit package paths

The default `package-game --content-mode Legacy` retains Project/Workspace/Tracked staging, generation export and the existing CEMF v2 cook. It can also attach validated AssetSets.

`package-game --content-mode PrebuiltAssetSets` composes a verified installed Player, a prebuilt GameScripts assembly, an immutable runtime bootstrap and 1–64 independently built AssetSets. It never stages project assets, invokes model-generation authoring, calls the legacy cook, compiles runtime documents, or compiles managed/native code. It still requires a project engine pin and existing `Assets` / `ProjectSetting` directories, but `Assets` may be empty. `GameCompiler` only validates/copies the supplied prebuilt assembly in this mode. It captures a bounded receipt and holds both source handles without write/delete sharing, checks the copied bytes against the captured hash, rechecks the receipt, retains that expected hash in the emitted receipt, and checks again before publication.

Required options:

```text
package-game --engine-distribution C:\Engine --project C:\Game --config Release
  --content-mode PrebuiltAssetSets --bootstrap-root C:\Content\Bootstrap-r1
  --asset-set-list C:\Content\sets.txt --game-scripts-assembly C:\Game\Compiled\GameScripts.dll
```

`--input-mode Workspace|Tracked`, `--asset-list`, `--startup-scene`, `--render-backend` and `--build-native` are rejected in PrebuiltAssetSets mode. Startup/backend/settings belong to the frozen bootstrap. `--asset-set-abi` is an optional exact assertion of the installed content ABI, not a compatibility selector.

Native leased AssetSet copy, pre/post CAS verification, CEAS group validation, PAK reopening, smoke verification and transactional current-pointer publication are unchanged. `--skip-verify` retains an unpublished candidate. Package provenance names the exact bootstrap content digest, includes its producer receipt, and records `cook: null`; it does not attribute the bootstrap's earlier document compilation to the current packaging invocation.

## Producing the document bootstrap

Scene, Prefab and Audio remain existing runtime document/audio subsystems. There are no new typed Scene/Prefab/Audio/Font loaders.

```text
build-runtime-bootstrap --engine-distribution C:\Engine --project C:\Game
  --output C:\Content\Bootstrap-r1 --asset-set-list C:\Content\sets.txt
  --startup-scene Start.creator --render-backend dx12
```

This independent, offline command requires no model generations, model sources, texture sources or material/graph sources. Its strict staging boundary includes scene/prefab documents and sidecars; existing input-map, behavior-tree, blackboard, render-profile, terrain and foliage documents; collision-geometry authoring documents; audio clips/sound graphs/presets and sidecars; shader files under `Assets/Shaders` needed by the existing renderer; and project `.asset` / `.celayers` settings. Other sources are not copied. An optional exact `--asset-list` rejects non-document selections and still requires the selected startup scene and shader closure. Preserved `.foliage` documents receive a bounded bootstrap-only typed-reference preflight against the captured AssetSet union. Other ancillary document support remains the existing subsystem contract, and final Player verification remains required.

The native `--build-runtime-bootstrap` mode rejects model/texture/material/graph import options and non-document staged inputs. It reuses the existing SceneCookProducer, audio/sound and collision producers plus CEDO compilation. Verified prebuilt MaterialProgram bytes may be read to validate inline instance overrides; no shader source compiler runs. Existing scene texture-name fallback is rejected. Sources and sidecars are inputs only; audio and geometry authoring inputs are removed. Scene/prefab identity paths and renderer shader documents remain for existing runtime identity/path contracts, while scene loading uses cooked CEDO artifacts.

Output is a new immutable `Assets` / `ProjectSetting` tree plus `bootstrap-report.json`. The report binds engine build ID, installed payload digest, content ABI, every staged source-input hash, every output file hash, startup/preflight and runtime-document facts. Packaging validates the full exact inventory, forbids model/texture/material legacy payloads, rechecks source/report after copying, and validates the copied bytes. Bootstrap inputs are ordinary caller-owned immutable directories; this is not enrollment in the AssetSet collectible-store protocol.

## CEBR1 document-reference bridge

CEMF v2 schema, reader and standalone dependency rules remain unchanged. The document bootstrap's v2 manifest contains only local Scene, Prefab, AudioClip, SoundGraph, SoundPreset and CollisionGeometry entries. Local edges retain existing meanings and must close inside that manifest. The bridge neither fabricates v2 model entries nor converts a v2 package into an AssetSet.

`Assets/Derived/bootstrap-asset-references.cebr` explicitly records checked external typed references from scene/prefab documents. Recognized serialized fields supply expected kinds, including direct Mesh, Model/Animator, Texture, context-specific `m_Material.ref` base Materials and their typed texture overrides, nonnil inline authored Material identities plus ShaderMeta, and Lattice MaterialProgram dependencies. Override values use the existing MaterialAuthoringCodec (`texture.guid` on the wire); generic `ref` or `assetId` metadata fields are not reinterpreted. Bootstrap-only inline authored materials require a nonnil base Material identity for source-free Code-program binding; legacy inline nil identities remain unchanged. A direct MeshRenderer Mesh reference does not require an unrelated parent Model source. Foliage is context-specific: `m_foliageTypes` and the existing `FoliageAsset.Types` document shape require a nonnil typed Model even with explicit Mesh/Material IDs; optional nil child IDs retain descriptor-default selection. Every explicit nonnil child ID is checked with its expected Mesh/Material kind. A standalone inline FoliageType is recognized only by its complete reflected Model/Mesh/Material/legacy-flag shape, not a coincidental flag alone. Name-only fallback is rejected by source-free bootstrap. `.foliage` preflight uses the same bounded reference walker and existing document parser (maximum 16 MiB per file, 64 MiB total and 16384 files); report input/output hashes and the exact CEBR AssetSet group bind the checked documents without a new CEBR record/kind or runtime Foliage payload reads. Nested prefab overrides are inspected as documents. Each checked external reference is removed from only that document's legacy edge list and represented explicitly in CEBR; unexplained unresolved v2 edges still fail the unchanged writer.

Canonical UTF-8 format, LF terminated:

```text
CEBR1
win-x64
creator-content-v1
legacy <exact CEMF-v2 SHA256>
set <exact CEMF-v3 SHA256>
document <scene-or-prefab UUID> <cooked-document SHA256>
reference <owning-document UUID> <expected-kind decimal> <target UUID>
```

Sets and documents are sorted; references are sorted by typed-reference ordering within their owning document. Every scene/prefab appears even if it has no external references. Limits are 4 MiB, 1–64 distinct sets, 1–16384 documents and at most 65536 references. Duplicate/conflicting identities, noncanonical records, unexpected kinds, unknown lines, control characters and changed document/set/legacy hashes are rejected. AssetSet source recipes still use explicit v3 Hard/Loadable edges; CEBR does not classify scene references as those ownership edges.

Package validation resolves the complete candidate v3 union and each expected CEBR type. Startup validates the same receipt, legacy manifest metadata and exact v3 manifest group before atomic resolver publication. It reads no Scene/Texture/Model/Program bulk payload. Normal scene admission subsequently performs asynchronous typed acquisition using the existing consumer path. Changing any activated set manifest requires a new document bootstrap, conservatively binding content updates without recompiling Player. This is a local/pak bridge, not CDN delivery, patching, signing or a general Scene asset system.

## Installed content compatibility

`CreatorContentAbi::Version` and `CreatorContentAbi::Token` are the compiled authority, currently `1` / `creator-content-v1`. They are separate from host ABI 1 and managed script API 35. `CreatorHostGetContentInfoV1` is an additive export; `CreatorHostInfoV1` layout is unchanged. The launcher checks its host's content identity and appends it to its existing `--engine-info` JSON. Engine publication requires agreement among all four compiled host records and includes the identity in the engine manifest. Opt-in content paths bind the installed Player/AssetCooker records and their hashed payload inventories to that manifest. Old distributions without these fields fail clearly on AssetSet/bootstrap use; legacy distribution loading remains available.

CEAS no longer takes its own token as proof that the host supports it. Its token must equal the compiled host constant, and every incoming manifest must satisfy that expected identity plus the existing per-kind representation/schema checks. Arbitrary tokens remain possible for independent generic cooker/catalog experiments; they cannot self-authorize activation in this Player.

Build-record hashes provide local provenance and integrity, not a cryptographic publisher signature or defense against an attacker able to rewrite an entire trusted distribution and its receipts. Existing publication trust assumptions are unchanged.

## Unrun verification sources

- BuildTool/Tests/Program.cs: strict prebuilt option combinations, bootstrap source/output boundaries, old-distribution rejection, AssetSet copy hash and interruption checks
- Tools/regression/runtime_bootstrap_probe.cpp: canonical receipt parity/rejections, exact group/document/type rejection, metadata-only validation without payload files, typed direct-Mesh extraction and unchanged legacy scene semantics
- Tools/regression/asset_set_activation_probe.cpp: installed ABI rejection even when both policy and manifest assert the same incorrect token

Windows build, end-to-end source-free package/smoke, all supported rendered kinds, failure injection and shader/backend runtime checks remain unrun.
