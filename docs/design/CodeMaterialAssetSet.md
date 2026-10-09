# Source-free authored Code materials in AssetSets

This is the separate authored-Code recipe. Existing Lattice material/program
representation 1 is unchanged. ShaderMeta remains a CPU descriptor and never
claims compiled-shader readiness.

## Identities and representations

- `Material`, representation 2, schema 1, `.asset`: canonical CEDO envelope
  `authored_material` containing `schema`, `programAssetId`, sorted unique
  `defaultTextures`, and `material`. The material subtree is the existing
  `experiment::Material` schema 1. Its `shaderAssetId` still names ShaderMeta.
- `MaterialProgram`, representation 2, schema 1, `.cecp`: bounded CECP value
  product. The program UUID and ShaderMeta UUID must be distinct.
- `ShaderMeta`, representation 1: unchanged canonical descriptor CEDO. Its
  bytes must exactly equal the descriptor carried by the selected Code program.

A material has exact typed Hard edges to its Code program, ShaderMeta, authored
nonnull texture references and every metadata default texture. The program has
exact typed Hard edges to ShaderMeta and every default texture. Deduplication
is by identity; using one identity for incompatible kinds fails. Internal and
External scopes are explicit and both work; duplicate, missing, extra, wrong
kind and non-Hard edges fail. ShaderMeta itself has no program/texture edges.

## Authored inputs

Keep the existing flat material schema 1 file with its canonical UUIDv4 identity.
Model-derived UUIDv8 materials remain on their model producer recipe. The AssetSet source declaration
adds an explicit program UUID and a verified bundle path:

```yaml
assetId: 11111111-1111-4111-8111-111111111111
kind: Material
source: Materials/example.asset
codeProgram: 22222222-2222-4222-8222-222222222222
verifiedProgram: Verified/example.cecp
dependencies:
  - {assetId: 22222222-2222-4222-8222-222222222222, kind: MaterialProgram, dependency: Hard}
  - {assetId: 33333333-3333-4333-8333-333333333333, kind: ShaderMeta, dependency: Hard}
  # Also declare every authored/default texture as a typed Hard dependency.
```

The MaterialProgram source uses a `.codeprogram` descriptor, a program identity
sidecar (`.codeprogram.meta`), and explicit `verifiedProgram`. Its descriptor is:

```yaml
code_program:
  schema: 1
  shaderMetaAssetId: 33333333-3333-4333-8333-333333333333
  metadata: Shaders/example.shadermeta
  source: Shaders/example.hlsl
  compilerSha256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
```

The example digest is a placeholder, not a supported compiler identity. The
actual digest is an externally verified compiler/toolchain agreement supplied
by the trusted bundle producer. It must exactly match the recipe. CECP parsing
validates provenance fields and structural consistency; it does not authenticate
a third-party compiler or claim to have compiled/validated executable machine
code. No compiler is invoked by this producer, reader or receipt replay.

`formatStamp = creator-code-verified-v1` and
`abiStamp = creator-code-material-abi-v1` are fixed engine-Code recipe versions.
The Code ABI is separate from the enclosing AssetSet target ABI/platform; normal
catalog/mount compatibility still validates that enclosing target. The recipe
uses default compile options, VS/PS/CS profiles 6.0, both DXIL and SPIR-V,
canonical material keyword defines, ModelVertexInput's mask defines, and the
engine's Forward lighting constants/REFERENCE_PATH when applicable.

## Verified bundle and complete coverage

CECP is a value-only product: IDs, canonical metadata CEDO, compiler/format/ABI
stamps, canonical Assets-relative input paths with sizes and SHA-256, and sorted
backend/pass/keyword/mask/reference variants. Each variant contains ordered
stage/entry/profile records with bytecode and reflection encoded by
`rhi_shader_verified_cache`. Source text, device state, a compiler, runtime
manager, owning raw pointer and PSO handles are absent.

The reader reconstructs the binding layout from every carried variant's stage
reflections. All variants must agree and pass the shared pure
`LX::Runtime::ValidateShaderGeneration` invariant, including the 64 KiB constant
buffer bound, nonoverlapping numeric fields and unique texture registers.
Coverage is exhaustive, never truncated:

- Every pass and every keyword combination, on DXIL and SPIR-V
- Graphics: legacy mask 0 and all eight `assets::kModelVertexMasks`
- Forward: both ordinary and reference paths for each graphics variant
- Other graphics: ordinary path only
- Compute: one mask-0, non-reference variant per backend/pass/keyword selection
- Exactly VS+PS for graphics, or CS for compute, with declared entries/profiles

Duplicate variants, unsupported extra variants, partial backends, inconsistent
permutations/reflections and unsupported stamps fail. The maximum is 4,096
variants and 160 MiB per bundle. Retained charges count owned capacities,
including stage bytecode, reflection, metadata, inputs and all variant strings.

## Captured-input and publication checks

The producer captures and hashes the descriptor and its sidecar, explicit bundle,
metadata and its sidecar, root shader and its sidecar, and include closure. The
metadata source canonical CEDO must equal the carried metadata bytes; sidecar
identities are checked independently. Every captured byte length/SHA-256 must
match the bundle inventory. Input snapshots are verified again before receipts
and output publication. Missing bundles or changed inputs never trigger source
compilation, a last-known product lookup, or metadata-only readiness.

Version 1 deliberately supports literal `#include` closure only. Quoted includes
resolve relative to their containing file; angle includes resolve from Assets.
Escaped newlines and comments are handled before scanning. Macro includes,
Slang module imports, file-probing directives, `include_next` and `embed` are
rejected. Includes in conditionally disabled branches are conservatively required
too. The inventoried shader files must equal the reachable literal closure;
omitted and unrelated files fail. This narrow contract avoids presenting a
claimed compiler inventory as proof of unsupported dynamic dependency lookup.

`doubleSided` is the existing bool raster-policy exception when absent from
ShaderMeta. Other unknown properties, wrong types, duplicate properties, invalid
keyword selections and inconsistent default-texture inventories fail binding.

Receipt identities include the explicit Code program ID, bundle path, recipe
version and complete captured input inventory. Receipt/CAS payload validation
selects the exact representation/schema reader. After cooking local products,
selected material/program binding and independently cooked ShaderMeta CEDO
identity are checked before publication. External closure checks are deferred to
the captured union/runtime loader, without local source fallback.

## Regression source and execution status

`ExperimentCodeMaterialCodecSelfTest.cpp` adds synthetic codec/producer checks to
the existing catalog self-test entry point. Its stage payloads are value-codec
fixtures, not executable shaders. It covers successful source-free round trips,
transactional failures, exact dependency kinds/scopes, incomplete coverage,
stamps, profiles/reflection, oversized/overlapping common layouts, aliased texture
registers, captured-input changes, original authored schema1,
default-texture closure and invalid authored values. The test source and project
entries were added; no build, tests, engine, shader compiler or AOT execution was
performed for this change.

## Runtime ownership and consumer boundary

The concrete AssetDepot views are deliberately separate:

- `AssetLink<experiment::Material>` accepts authored Material representation 2
- `AssetLink<::Material>` accepts graph representation 1 or the prepared code facade of representation 2
- `AssetLink<LX::Runtime::ShaderGeneration>` accepts Code MaterialProgram representation 2
- `AssetLink<material_graph::Generation>` accepts graph MaterialProgram representation 1
- `AssetLink<ShaderMeta>` remains the independent metadata descriptor

Wrong concrete representations terminate as `Failed/UnsupportedRepresentation`.
A shared manifest kind is never concrete C++ type proof. The existing typed
request cache/worker DAG owns all program/metadata/texture dependencies and its
completion covers the complete CPU closure. Metadata decoding alone never
produces code readiness. Code generations pass the existing LX layout validator
before Ready, keep weak numeric metadata slots separate from GUID-to-latest
source metadata, and retain the exact compiled blob/descriptor/default texture
sources. Cache budgets account for reachable capacities; zero retention does
not revoke consumer owners. Logical unmount prevents current-link reacquisition
while already-pinned program/material/frame owners remain self-contained.

Scene discovery submits mounted material/program IDs asynchronously and waits by
polling before component construction. Ref-based and inline authored code
materials keep their exact program and texture generations. A texture override
outside the base asset closure must receive an explicitly prepared owner from
the same captured scene resolver revision, using an untransformed default-variant descriptor. `BindPreparedMaterialTextures` and
`MaterialInstance::BindPreparedTextureOwners` extend only the runtime instance's
closure, never an asset-cache root. Missing or mismatched owners fail without a
source or current-catalog fallback. Inline and ref-style authored values keep their explicit
texture color spaces; generated mip variants are never reused as raw override sources. Source authoring remains on its legacy path when the ID
is not mounted.

Existing explicit/reference GBuffer and Forward helpers consume carried graphics
stages through `RestoreCodeGraphics`; `RestoreCodeCompute` exposes carried compute
stages with the same owner rule. No compiler/cache lookup is performed for these
mounted code generations. Source-derived GBuffer meshlet/occluder optimizations
are disabled for sealed code programs because their derivative entries are not
part of this bundle. GPU upload/PSO readiness and device retirement remain owned
by the existing renderer.

The production live renderer remains graph-only. This work does not revive its
retired code-material live pipelines or claim source-free live code rendering.
`Tools/regression/code_material_runtime_probe.cpp` is an unrun dedicated-host
fixture for async completion, cross-representation rejection, source-free owner
resolution, zero retention and post-unmount pins. No build/test/runtime/renderer
execution result is implied by this source-only implementation.

Code facade serialization uses its pinned metadata and texture sampling values,
without loading ShaderMeta by GUID. Unmodified metadata-default matrices are
omitted and restored from the program; edited matrices are rejected because the
existing authored value schema has no matrix variant. Anonymous inline code
values that carry only a ShaderMeta ID must acquire a stable cooked base Material
before source-free construction; a program identity is never guessed from metadata.
