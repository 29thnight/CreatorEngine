# CSM correctness and optimization

Base: `033858a7d22451ff93aa54a3947c6990b59f53a7`.

## Implemented scope

- Keep a separate legacy shadow-caster input before camera-frustum rejection. Select the union of camera-visible objects and conservative shadow casters before the bounded Graph seal, using the same receiver planner and settings as CSM. Unrelated offscreen objects do not consume the Graph draw budget; relevant offscreen casters retain shared geometry preparation. Diagnostic visible-draw pose replay also updates the corresponding shadow inputs.
- Validate sealed mesh layout and palette before reading them for bounds. Carry local bounds centers. Enclose affine transforms (including shear) and the convex hull of bone-transformed source bounds. Cache legacy world bounds once per view preparation. Typed fixtures without supplied bounds derive them from their immutable POSITION bytes.
- Keep stable sphere fitting, snap in light-space XY, pad the fit for snapping, overlap receiver ranges for cascade blending with matching CPU/shader behavior at positive, negative and zero splits, and include upstream caster bounds in the light-depth range. Unproject corners correctly for perspective and orthographic cameras.
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

The baseline worktree is detached at the base SHA above. The main worktree is isolated from the user's dirty checkout, binaries and running editor. Existing dependency outputs are read without restoring or modifying them.

- Standalone `csm_math_probe.cpp`: passed perspective/orthographic corner reprojection, light-space microtranslation and integer-texel stepping, conservative clip intersection, offscreen caster distinction, negative and zero split boundaries, and pre-budget receiver selection.
- Revised isolated DX12 Debug and Release builds: passed. Revised Vulkan Debug probe build: passed.
- Baseline control: the original shadow test failed fixture 3 with depth `0.4` instead of `1.0`. Its fixture mutates source vertices/indices while retaining the same immutable model generation. With only a distinct generation per fixture, unchanged baseline engine code passed all 42 shadow frames and 114 decal frames, including fixture 6, with validation zero. The revised test preserves the original pixel assertions and adds sealed-index identity assertions.
- Vulkan Debug shadow/decal aggregate: exit 0, 45 shadow frames (`covered=9720`, `gpuComponents=34572`), 114 decal frames (`gpuComponents=293214`), validation zero and encoder drops zero. The queue-gated lifetime regression verified three genuinely pending frames and nine retained depth slices before readback. Legacy alpha grouping verified 384 instances / 192 batches with matching serial and four-worker depth results. Pre-budget selection sealed two relevant draws from 4100 candidates. Logs: `csm-final-vulkan-debug.log`.
- DX12 Debug and Release shadow/decal aggregates: both exit 0, each 45 shadow frames (`covered=9720`, `gpuComponents=34563`) and 114 decal frames (`gpuComponents=293205`), validation zero with GPU validation enabled. Both include the bounds/bias/ownership/overlap contracts, malformed-input rejection, pre-budget selection and serial/parallel legacy alpha batching checks. Logs: `csm-final-dx12-Debug.log`, `csm-final-dx12-Release.log`.
- The 18 changed code/test/script files matched the pre-run SHA-256 snapshot after all runs (`drift=0`). `git diff --check` passed. No engine or test code changed after these builds and executions.

### Remaining acceptance limits

Functional GPU readbacks and submission counts do not establish a measured performance speedup. No before/after GPU timing claim is made. The user's original near-foot/ankle artifact has not been reproduced in an isolated scene with matched camera, geometry scale, lighting and material settings. Scene-level contact-shadow images, camera-motion/seam video and before/after performance capture remain outstanding. The current tests cover projection/bias contracts, GPU depth pixels, alpha/skinning and serial/parallel equivalence instead.

The full raster, volume, refraction and subsurface aggregates have not yet been rerun on the revised head. AddressSanitizer was not run; malformed layout/palette rejection is covered by the native Scene input regression. Release Vulkan execution is also not claimed.

Graph camera visibility is conservative: relevant offscreen graph casters may still incur shared surface preparation. A separate per-pass surface visibility contract can remove that work without losing shadow casters.
