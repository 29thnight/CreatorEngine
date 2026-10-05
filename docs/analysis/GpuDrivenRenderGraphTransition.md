# GPU-driven / RenderGraph transition slices

Status: implementation in progress. This ledger is the opening review slice, not an acceptance report.

Base: `master` at `29024ddf245575360c4e43471fc13d8858798439` (2026-10-05).

## Implementation scope

1. Add a backend-neutral single indexed-indirect draw contract, native DX12/Vulkan implementations, explicit capabilities, buffer usage/state validation, and unsupported-device fallback. Do not assume multidraw, indirect count, or nonzero first-instance support.
2. Add GPU geometry visibility and compacted instance/argument buffers to both Enhanced GBuffer and LX.Scene.GBuffer. Keep CPU material/PSO bins and supported nonmigrated cases. Declare producer/consumer edges and retain all submitted resources until GPU completion.
3. Separate runtime scene shading from reference IBL bake records. Reuse GPU scene/material inputs and existing split-sum lighting evaluation instead of retaining full bake records for every ordinary screen pixel. Preserve special-material semantics, account live/replacement/scratch resources, and distinguish recoverable resource pressure from renderer corruption.
4. Finish remaining production access/version declarations (Fog, PostChain, UI, Editor overlays), wire current final outputs and capture consumers, and select the existing ExplicitVersioned dependency scheduler for the product path. Keep intentional legacy diagnostic fixtures distinct from production use.
5. Expose actual immutable compiled graph snapshots to the reader/viewer with generation, resource/version, dependency, execution order, lifetime and barrier information.

## Review and delivery boundaries

- Publish small statically reviewed commits to the same Draft PR against master as each slice is ready.
- Code and static review only for this request: no build, runtime, automated test, capture, GPU validation or performance execution.
- Existing RG5/RG6/RG-V acceptance gates remain pending unless supported by their original evidence; these edits cannot establish new pixel, performance or backend parity acceptance.
- Do not revive shadow-cache or compiled-plan-cache proposals. Mesh shaders, DXR, WorkGraphs, async queues, aliasing and a new light-volume subsystem are outside this implementation slice.
- Special-material memory remains a separate concrete implementation concern; a recoverable-pressure response is not evidence that a 4K effect has become admissible.

## Architectural references

Borrow principles, not engine source: explicit dependencies and graph-owned lifetimes from [Unreal RDG](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine), GPU scene-depth access from [Depth Material Expressions](https://dev.epicgames.com/documentation/en-us/unreal-engine/depth-material-expressions-in-unreal-engine), and separation of precomputed lighting data from screen-sized records from [Volumetric Lightmaps](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-lightmaps-in-unreal-engine).

The detailed project gates remain in [RenderGraphDependencySchedulingPlan.md](../plans/RenderGraphDependencySchedulingPlan.md).
