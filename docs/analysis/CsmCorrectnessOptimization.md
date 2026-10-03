# CSM correctness and optimization

Base: `033858a7d22451ff93aa54a3947c6990b59f53a7`.

## Implemented scope

- Keep a separate legacy shadow-caster input before camera-frustum rejection. Preserve graph casters outside the camera; graph geometry preparation remains shared with surface rendering. Diagnostic visible-draw pose replay also updates the corresponding shadow inputs.
- Carry local bounds centers. Enclose affine transforms (including shear) and the convex hull of bone-transformed source bounds. Cache legacy world bounds once per view preparation. Typed fixtures without supplied bounds derive them from their immutable POSITION bytes.
- Keep stable sphere fitting, snap in light-space XY, pad the fit for snapping, overlap receiver ranges for cascade blending, and include upstream caster bounds in the light-depth range. Unproject corners correctly for perspective and orthographic cameras.
- Separate shadow distance from camera far plane: default 200 world units, configurable with `SetShadowDistance`. This is an engine default, not a measurement of the user's scene scale.
- Default constant bias is 0.5 shadow texels, converted to world distance and then normalized by each light-depth range. `SetBiasTexels` uses these units. The existing `SetBias` entry point retains compatibility with the old normalized-depth knob at a four-radius depth span, including diagnostic negative bias. Slope tangent remains bounded in the shaders.
- Pack the selected directional-light index plus one in `splitDepths.w`; deferred, forward, graph surface, and graph volume consumers apply CSM only to that light. Zero preserves first-light ownership in old isolated fixtures. Surface shadow sampling stops beyond the configured shadow distance.
- Graph draws carry conservative cascade visibility. Skip constants for rejected cascades and avoid repeated pipeline/material binding for consecutive compatible chunks. Do not instance unrelated world-geometry buffers.
- Legacy sort, batch boundaries, work partition boundaries, and cost estimate share geometry/skinning/alpha-texture grouping. Partition on complete groups and whole cascade multiples.
- Vulkan render-target tables belong to frame slots. Owned slice views survive until the owning slot's fence completes; shutdown releases all tables after GPU retirement.

## Fog review

The legacy volumetric fog path deliberately receives the last-cascade matrix and samples layer 2. In `FogScatter.slang`, the computed shadow/cloud visibility currently feeds only a commented-out sun term; the active light loop does not consume it. Changing that policy would introduce new fog lighting behavior and is outside this surface-CSM correction. The separate MaterialGraph volume path actively samples CSM and now respects selected-light ownership.

## Deferred phase: persistent shadow-depth cache

No shadow-content cache is enabled by this change. `EnhancedShadowPass::Declare` creates a graph texture and clears its layers every frame. Texture allocation pooling and shared skinned geometry are not depth-content reuse.

The bounded follow-up belongs to the shadow renderer owner and needs:

1. A persistent depth-map owner per view/light with explicit graph import state and GPU retirement. The graph's transient texture identity cannot act as a content-validity token.
2. Exact invalidation for camera/view identity, cascade matrices and coverage, light identity/direction, caster addition/removal, world transform, palette content, geometry generation and LOD, alpha/coverage/material texture content, and shadow settings. Graph program/instance changes must participate too.
3. A deliberately small first mode: full-map reuse only when every dependency is unchanged, otherwise redraw all cascades. Partial static/dynamic composition requires a separate depth-composition design.
4. Cache-on/off image equivalence across each invalidator, multiple views and frames in flight, plus measured hit rate, memory and GPU time. Acceptance requires a benefit over invalidation/bookkeeping cost.

This needs a separate implementation and validation slice; silently retaining pooled depth would produce stale shadows.

## Validation record

- Standalone `csm_math_probe.cpp`: passed perspective/orthographic corner reprojection, light-space microtranslation and integer-texel stepping, conservative clip intersection, and offscreen caster distinction.
- Initial isolated Debug RenderEngine build: passed. Subsequent graph/bounds changes require the final probe rebuild.
- Added `CheckCsmContracts` to the existing shadow/decal GPU probe: local center, posed extremity, selected light, independent distance, world-bias units, upstream depth inclusion and orthographic finite matrices.
- GPU regressions, final Debug/Release builds, Vulkan frames-in-flight validation, image comparisons and performance measurements are pending. No measured speedup or user-scene visual fix is claimed yet.

Graph camera visibility is currently conservative: offscreen graph objects may still incur shared surface preparation. A separate per-pass surface visibility contract can remove that work without losing shadow casters.
