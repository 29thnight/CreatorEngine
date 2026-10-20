# Source-free authored Code materials in AssetSets

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/code_material_runtime_probe.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/code_material_runtime_probe.cpp#L28) — `auto base = own::make_shared<experiment::Material>();`
- [Tools/regression/code_material_runtime_probe.cpp:188](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/code_material_runtime_probe.cpp#L188) — `const auto existing = std::ranges::find(value.properties, propertyName, &experiment::MaterialProperty::name);`
- [Tools/regression/code_material_runtime_probe.cpp:197](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/code_material_runtime_probe.cpp#L197) — `Require(experiment::BindPreparedMaterialTextures(value, supplied, error),`
- [Editor/RenderTests/ExperimentParity/ExperimentCodeMaterialCodecSelfTest.cpp:136](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/ExperimentParity/ExperimentCodeMaterialCodecSelfTest.cpp#L136) — `if (!experiment::SerializeMaterialAuthoring(material, source.Root(), failure))`
- [Editor/RenderTests/ExperimentParity/ExperimentCodeMaterialCodecSelfTest.cpp:279](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/ExperimentParity/ExperimentCodeMaterialCodecSelfTest.cpp#L279) — `auto material = ck::BuildMaterialAssetSetProduct({CodeId(1), CodeBytes(fixture.materialText),`
- [Editor/RenderTests/ExperimentParity/ExperimentCodeMaterialCodecSelfTest.cpp:297](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/RenderTests/ExperimentParity/ExperimentCodeMaterialCodecSelfTest.cpp#L297) — `check.Check(!ck::ValidateAuthoredMaterialBinding(document, decoded, failure), "wrong doubleSided raster policy type rejected");`
- [BuildTool/Metadata.cs:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Metadata.cs#L9) — `internal sealed record FileEntry(string Path, long Bytes, string Sha256);`
- [BuildTool/Metadata.cs:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Metadata.cs#L11) — `internal static class Metadata`
- [BuildTool/Metadata.cs:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Metadata.cs#L65) — `if (value.Int("schemaVersion") != 1 || value.Text("productName") != "CreatorEngine 2") throw new BuildException("Unsupported engine product metadata.");`
- [Engine/RenderEngine/ShaderMeta.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L21) — `struct ShaderMetaAssetOrigin;`
- [Engine/RenderEngine/ShaderMeta.h:62](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L62) — `struct ShaderMaterialSampler`
- [Engine/RenderEngine/ShaderMeta.h:174](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L174) — `own::shared_owner<const AssetDepot::ShaderMetaAssetOrigin> assetOrigin{};`
- [Engine/RenderEngine/Experiment/Model.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L9) — `class ModelLoader;`
- [Engine/RenderEngine/Experiment/Model.h:52](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L52) — `[[nodiscard]] std::span<const Material> Materials() const noexcept;`
- [Engine/RenderEngine/Experiment/Model.h:58](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/Model.h#L58) — `[[nodiscard]] const Material* TryGetMaterial(MaterialIndex index) const noexcept;`
- [Engine/RenderEngine/AssetDepot/AssetLink.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetLink.h#L9) — `class Material;`
- [Engine/RenderEngine/AssetDepot/AssetLink.h:10](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetLink.h#L10) — `struct ShaderMeta;`
- [Engine/RenderEngine/AssetDepot/AssetLink.h:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetLink.h#L54) — `struct AssetTypeTraits<experiment::Material>`
- [Engine/RenderEngine/Experiment/MaterialInstance.h:34](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/MaterialInstance.h#L34) — `explicit MaterialInstance(own::shared_owner<const Material> base)`
- [Engine/RenderEngine/Experiment/MaterialInstance.h:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/MaterialInstance.h#L39) — `[[nodiscard]] const own::shared_owner<const Material>& Base() const noexcept`
- [Engine/RenderEngine/Experiment/MaterialInstance.h:87](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Experiment/MaterialInstance.h#L87) — `own::shared_owner<const Material> base_{};`
- [Engine/RenderEngine/ShaderMeta.cpp:571](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L571) — `meta.generatedMaterial = std::move(generated);`
- [Engine/RenderEngine/ShaderMeta.cpp:610](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L610) — `std::filesystem::path ShaderMeta::ResolveSource(`
- [Engine/RenderEngine/ShaderMeta.cpp:616](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L616) — `bool ShaderMetaLoader::LoadFile(const std::filesystem::path& path,`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

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

</details>
