# BuildAssetSet import receipts

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../analysis/MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/AssetCooker/AssetSetBuild.cpp:1355](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetSetBuild.cpp#L1355) — `auto cooked = ck::BuildShaderMetaCookProduct({ source, assetRoot });`
- [Tools/AssetCooker/AssetSetBuild.cpp:1468](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetSetBuild.cpp#L1468) — `cooked = ck::BuildMaterialProgramAssetSetProduct({ authored.asset.key.assetId,`
- [Tools/AssetCooker/AssetSetBuild.cpp:1473](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetSetBuild.cpp#L1473) — `cooked = ck::BuildMaterialAssetSetProduct({ authored.asset.key.assetId, sourceInput, metaInput,`
- [Tools/regression/asset_set_import_receipt_probe.cpp:67](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/asset_set_import_receipt_probe.cpp#L67) — `Require(cold.succeeded && cold.recookedImports == 2u && cold.reusedImports == 0u && cold.blobs == 1u,`
- [Tools/regression/asset_set_import_receipt_probe.cpp:99](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/asset_set_import_receipt_probe.cpp#L99) — `Require(warm.succeeded && warm.reusedImports == 2u && warm.recookedImports == 0u,`
- [Tools/regression/asset_set_import_receipt_probe.cpp:107](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/asset_set_import_receipt_probe.cpp#L107) — `Require(remanifest.succeeded && remanifest.reusedImports == 2u && remanifest.recookedImports == 0u,`
- [Engine/RenderEngine/ShaderMeta.h:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L24) — `enum class ShaderPropertyType : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:98](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L98) — `enum class ShaderPassQueue : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:106](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L106) — `enum class ShaderBlendMode : std::uint8_t`
- [Engine/Utility_Framework/Sha256.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L28) — `class Sha256`
- [Engine/Utility_Framework/Sha256.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L30) — `public:`
- [Engine/Utility_Framework/Sha256.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L40) — `std::memcpy(m_buffer + m_bufferUsed, p, take);`
- [Tools/AssetCooker/AssetSetBuild.h:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetSetBuild.h#L8) — `struct AssetSetBuildRequest final`
- [Tools/AssetCooker/AssetSetBuild.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetSetBuild.h#L18) — `struct AssetSetBuildResult final`
- [Tools/AssetCooker/AssetSetBuild.h:33](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetSetBuild.h#L33) — `[[nodiscard]] AssetSetBuildResult BuildAssetSet(const AssetSetBuildRequest& request);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Tools/AssetCooker/AssetCooker.cpp:123](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetCooker.cpp#L123) — `const auto size = std::filesystem::file_size(path, error);`
- [Tools/AssetCooker/AssetCooker.cpp:1570](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetCooker.cpp#L1570) — `ck::TextureCookProductResult result = ck::BuildTextureCookProduct(`
- [Tools/AssetCooker/AssetCooker.cpp:1604](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetCooker.cpp#L1604) — `ck::BuildShaderMetaCookProduct({ shaderMeta, assetRoot });`
- [Engine/RenderEngine/ShaderMeta.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L28) — `outError = std::string(context) + ": " + std::string(detail);`
- [Engine/RenderEngine/ShaderMeta.cpp:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L65) — `const bool known = std::any_of(allowed.begin(), allowed.end(),`
- [Engine/RenderEngine/ShaderMeta.cpp:102](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L102) — `else return Fail(context, std::string("'") + key + "'는 true|false여야 한다", outError);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Status: source implementation and unrun regression harness only. No compiler,
engine binary, shader compiler or test was executed for this change.

## What is skipped

The receipt hit branches return before `BuildTextureCookProduct`,
`BuildShaderMetaCookProduct`, `BuildMaterialAssetSetProduct`,
`BuildMaterialProgramAssetSetProduct`, or `BuildModelAssetSetProducts` is called.
A model source and its selected subassets form one import/conversion transaction.
A source-sidecar change invalidates that transaction; unrelated source groups can
hit. Changing a manifest revision or another source's bytes does not itself
invalidate unchanged groups. Changing a model selection conservatively imports
that model again; this is not a per-mesh source parser cache.

`AssetSetBuildResult.reusedImports` and `recookedImports` count these source
transactions (attempts on failure), independently from `reusedBlobs`. The completed
native build prints both. A hit still reads/hashes current source inputs, reads and
hashes immutable blobs, validates their typed payloads, and checks the selected
cross-asset bindings. In particular pass-through texture payload validation still
decodes the encoded image. No claim of skipping typed artifact validation or all
CPU decoding is made. The existing five-field completion report is unchanged.

## Exact input proof

The versioned, length-framed recipe key contains:

- Build/receipt/importer and selected representation/schema versions
- The verified distribution/tool fingerprint supplied by BuildTool, covering
  its orchestration assembly and native importer/decoder/compiler dependency
  payload; the C++ API continues to require this verified fingerprint
- Target platform and ABI, normalized texture settings, the fixed material budget
  and strict captured-resolver domain
- Every selected typed ID, normalized source/verified-program path and declared
  typed dependency, including hard/loadable and internal/external scope
- Root source bytes and canonical sidecar bytes; for models, the authored identity
  epoch header; for MaterialProgram, the explicitly supplied verified program

The successful import records a complete project-relative file inventory, with
byte sizes and SHA256 for every input. ShaderMeta includes referenced HLSL and its
sidecar. Models include every external buffer/image/file requested by the existing
captured importer callback, in addition to source/sidecar/epoch. Material and
MaterialProgram producers consume only their captured byte spans; material budget
and verified compiler identity are validated inside the shared program producer.
A model's capture callback supplies exactly the bytes used by the importer, rather
than observing the filesystem before and after an uncontrolled read.

The pre-import recipe directory is only a candidate locator. A hit also requires
all full-inventory sizes/hashes to match, mandatory initial inputs to be present,
all typed product metadata to equal the current format contract, and each exact
CAS artifact's size/hash and typed schema to validate. Restored product metadata
includes importer, representation, schema, extension and normalized input digest.
Dependencies come from the current declaration, whose complete typed edge list is
bound into the recipe key; the producer's semantic edge match was required when
the receipt was created. `sourceImportKey` is the hash of the recipe key and the
complete captured input inventory, never an output-content-only key.

Missing-file resolution is strict: captured glTF only opens explicit local relative
URIs through its supplied reader, and captured FBX marks the entire import failed
if any callback open fails. A successful recipe therefore has no tolerated absent
alternate path or directory-search dependency. If a future importer permits
fallback/negative probes, it must record and revalidate them and bump the resolver
and importer version before its products can use these receipts.

The source `InputCapture` holds Win32 read handles without write/delete sharing,
so an already active writer prevents capture and later ordinary writes/deletes are
excluded until the bytes are consumed. It verifies file ID, byte size, change/write
time, current pathname identity and bytes. Both the receipt inventory and actual
conversion use captured bytes; a changed/reverted path cannot silently bind B's
product to A's hash. Read checks also reject a file changed during capture.
The definition itself is parsed from captured bytes and held through the build.

## Storage and failures

Receipts are local cooker data, not runtime assets. Runtime has no receipt reader
and no source recook fallback. Stored paths are under
`ImportReceipts/<recipe-key>/<receipt-bytes-sha256>.receipt`. The complete binary
record is length framed and its filename verifies its complete bytes. Limits are
64 MiB per record, 4096 inputs, 512 MiB per input, 1 GiB per captured transaction,
65536 selected products (the existing definition limit), and 256 candidate records
per recipe key. Exceeding a limit fails explicitly rather than truncating records.

The existing OS-scoped cache lock now encloses preparation and receipt lookup as
well as CAS publication. No additional global lock or cache manager is introduced.
All blobs and the candidate manifest are typed-validated before receipt publication.
Receipt candidates are written and read back in the existing cache-local unique
`.incomplete` work directory, then renamed once to their immutable final path.
Killed-process work directories are never searched, resumed or automatically
removed. A process kill leaves either ignored candidate bytes or a complete final
record. This is a process-termination guarantee, not a new power-loss durability
claim. Output releases still own separate immutable copies of CAS files.

No matching receipt starts fresh production. A positive-input digest mismatch
or missing old dependency tries the next bounded candidate, then produces fresh
bytes. Each candidate owns its own capture; rejected candidate inputs never enter
a later hit or a new producer receipt. Corrupt/incompatible records, unsafe paths
or damaged CAS fail with the exact
record/input path and reason; they are not silently repaired or overwritten. Cache
maintenance is an explicit caller operation and is outside this change.

## Regression coverage still to run

`Tools/regression/asset_set_import_receipt_probe.cpp` is an opt-in Windows harness
which includes AssetSetBuild.cpp instead of separately compiling that translation
unit, and links the AssetCooker native dependencies. It initializes COM for WIC.
It creates only a unique temporary fixture and checks cold import, equal-payload
CAS deduplication, warm zero-import hit, unchanged recipe metadata, manifest-only
revision, one-sidecar and one-source invalidation, tool fingerprint invalidation,
existing/later writer rejection, ignored killed-work debris, corrupt CAS rejection,
and bounded receipt parsing. It has not been compiled or run.

Further real-fixture release gates are deliberately unclaimed:

1. Cold/warm `.gltf` external-buffer and `.fbx` imports, then mutate each external
   buffer, source, sidecar setting and identity epoch separately; unrelated source
   groups must remain hits
2. Generated UUIDv8 MaterialProgram with authored `.shadergraph` and explicit
   verified program; warm build must bypass graph conversion, and mutations to
   source/meta/program/tool/ABI must miss
3. Authored ShaderMeta/HLSL/sidecar and Material sources, including wrong typed CAS
   schema, dependency scope changes and external mount-set contracts
4. Kill after candidate creation, during receipt write, after CAS publication and
   around final rename; next process must never use a partial record
5. Race source replacement, modification and A-to-B-to-A attempts during capture;
   fail safely or consume precisely the protected captured inputs
6. Truncated/oversized counts, duplicate inputs, traversal/reparse paths, missing
   mandatory inputs, altered filename/hash, missing blobs and candidate limit

The current static-only implementation is not runtime or performance evidence.

</details>
