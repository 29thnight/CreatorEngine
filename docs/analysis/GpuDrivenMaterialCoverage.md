# GPU-driven material coverage extension

This source checkpoint extends the GPU geometry milestone without replacing material
programs, disabling effects, or changing the globally merged Code/Graph depth order.
Execution and shader compilation remain unverified.

## Supported geometry routing

| Existing route | GPU-produced submission on capable devices | Preserved semantics |
|---|---|---|
| Native opaque/masked GBuffer | Indexed indirect; eligible standard geometry may use mesh dispatch | Original shaders, topology and material bindings |
| Native custom GBuffer | Indexed indirect, zero or complete original instance count | Raw instance IDs, original instance stream and custom identity-buffer extent |
| Native Forward, including transparent/custom/skinned | One indexed indirect command per original merged segment/batch intersection | Original CPU depth order, instance ordinals, PSOs, flow and palette data |
| LX GBuffer/ordered surface/color | One zero-or-one indexed command per owner | Original owner ID and program |
| LX refraction/lookup/SSS/runtime-special captures | The same zero-or-one command at every raster consumer | Per-draw background snapshots, capture clears, effects and final color coverage |
| Native/LX shadow geometry | Independent per-cascade indexed indirect | Existing caster domain, alpha/sidedness and shadow topology; no camera HZB |
| Native indexed occluder prepass | Independent frustum-only indirect arguments | No dependency cycle with main HZB arguments |
| Decals and simple world sprites | Nonindexed indirect | Adjacent blend/canvas order and procedural vertex IDs; explicit shader instance base |

Unknown native position semantics conservatively retain the complete draw. The GPU still
writes its executable count, but this does not imply useful culling for that shader.
Preserved bins write the complete identity sequence and emit only zero or the original
count. They never compact/reorder an arbitrary custom instance stream.

CPU ordering, PSO/material binding, and command submission remain intentional. GPU sorting
alone cannot change arbitrary PSOs, and no CPU readback or claim of GPU-only submission is
introduced. Per-turn refraction background captures remain at their original positions.

## Availability versus material fallback

Direct geometry compatibility remains for unavailable indexed/nonindexed indirect
capabilities. A missing prepared bin on a capable device is an error, not permission to
silently switch a material to another shader or disable its effects. Mesh shaders/LOD are
still optional optimizations; an accepted material can remain indexed GPU-driven.

Fullscreen and compute operations, including volume integration/composite, lookup bakes,
SSS filters and runtime depth copies, are algorithms rather than CPU-direct material
fallbacks. Their existing work is not forced through geometry indirect commands.

Pre-existing unsupported features stay explicit: ShaderMeta material pipelines do not
expose hull/domain/geometry stages, and scene media require the existing static closed
boundary contract/budgets. This extension does not claim new tessellation or skinned media.

## Included standard skinning shader contract

The standard GBuffer shader and source wrappers that include it expose the optional
`GBufferSkinPaletteExtentV1` constant buffer at b4/space0. Its reflected 16-byte layout is
four uint32 fields: matrix count, version, and two reserved zero fields. The renderer
accepts the exact field names/types/offsets and retains the optional root index with the
immutable shader generation. It supplies version 1 and the actual sealed matrix count,
not upload-allocation padding. Programs without this contract retain their prior layout.

This uniform preserves standard skinning in non-compacted include wrappers without
rewriting arbitrary custom instance padding or inferring semantics from filenames.
Explicit instance bone counts are clamped to the available palette extent; legacy wrappers
use the remaining extent. Active indices are checked before any matrix fetch. Declaring
this storage-safety contract does not establish safe deformation bounds or enable culling.

## Diagnostics and static review

The debug view separately reports backend indirect capabilities, prepared compaction bins,
preserved-stream bins, and conservative keep-visible inputs. These are preparation categories,
not GPU-visible counts, culling rates or performance measurements.

Independent source reviews covered native instance identity and range preservation,
Code/Graph ordering, empty special-effect captures, sealed skin inputs, procedural command
ABI, graph dependencies and recording ownership. The nonindexed 16-byte command deliberately
uses the compatible prefix of GPU-written 20-byte records with zero vertex/base-instance
fields, one command per call and its original 20-byte record offset.

No builds, tests, shader compilation, renderer execution, image comparisons or GPU timings
were run. Runtime correctness, effect parity and performance acceptance remain unrun.
