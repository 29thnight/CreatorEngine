# GPU-driven material coverage extension

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/ShaderMeta.h:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L24) — `enum class ShaderPropertyType : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:98](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L98) — `enum class ShaderPassQueue : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.h:106](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.h#L106) — `enum class ShaderBlendMode : std::uint8_t`
- [Engine/RenderEngine/ShaderMeta.cpp:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L28) — `outError = std::string(context) + ": " + std::string(detail);`
- [Engine/RenderEngine/ShaderMeta.cpp:65](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L65) — `const bool known = std::any_of(allowed.begin(), allowed.end(),`
- [Engine/RenderEngine/ShaderMeta.cpp:102](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/ShaderMeta.cpp#L102) — `else return Fail(context, std::string("'") + key + "'는 true|false여야 한다", outError);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

This source checkpoint extends the GPU geometry milestone without replacing material
programs, disabling effects, or changing the globally merged Code/Graph depth order.
Merged in PR #123. The merge-time DX12 checks covered the opaque meshlet/HZB path only
(see the [implementation plan](../plans/GpuDrivenGeometryImplementationPlan.md#merge-and-first-execution-2026-10-06));
the material routes below still have no executed parity check.

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

</details>
