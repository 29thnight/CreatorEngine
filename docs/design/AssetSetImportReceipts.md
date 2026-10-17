# BuildAssetSet import receipts

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
