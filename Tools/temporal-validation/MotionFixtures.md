# TR1 independent motion and route-ownership fixture

Status: source-authored, **unexecuted**. No compiler, shader, runtime or checker
was run. The descriptor schema name does not assert successful execution.

## Product readback

`render.pbr.capture <new-absolute-directory> game motion` (or `editor motion`)
holds the native-only capture exclusion and captures a non-reset submitted
frame. It does not freeze or replay simulation. The dedicated fixture driver
coordinates authored poses with exact submitted-step acknowledgements; the
ordinary capture CLI alone cannot sequence this experiment.

`captureMode: temporal-motion-v1` adds float32 little-endian row-major targets
after the seven PBR attachments: `temporalMotionRG` (2 channels),
`temporalReactive`, `temporalTransparency`, `temporalResponsive`, and
`temporalDepth` (1 channel each). The graph copies final blackboard versions
after material, decal and sprite writes. TU, FG, NIS and DeepDVC remain excluded.
Normal/controlled PBR capture retains its original seven attachments.

## Legacy numeric input remains incomplete

`Compare-TemporalMotion.py --capture <directory> --fixture ../regression/fixtures/temporal-motion/analytic-probes.json --output <report.json>`

The `temporal.motion.analytic-fixture.v1` format compares authored point
translations, weighted bone translations and scalar projection with GPU motion.
Its historical vectors are static (-4,+2), skin (-8,-4), instance (+6,+3), decal
(+8,+4), alpha (-3,-6), sprite (-2,+1) pixels. These are expectations, not measured
results, and are not the new driver's seven-route expectations. Legacy numeric
agreement always leaves `routeCoverageValidated: false`; it cannot return exit 0.

## Product descriptor: temporal.motion.executed-fixture.v2

The new scene driver and oracle are source-authored and unexecuted. Future
execution needs a supported device and separate authorization. The comparator
opens actual product manifests and float bytes and accepts only a complete
seven-route experiment: static, skinned, instanced, meshlet, alpha, decal, sprite.

Future authorized invocation (documented only, not run):

```powershell
./Tools/temporal-validation/Invoke-TemporalStage.ps1 -EndpointFile C:/Player/endpoint.json -Stage motion-fixture -OutputDirectory C:/Evidence/motion -TimeoutSeconds 300
```

Sprite motion uses a child RectTransform's anchored position under a fixed
WorldSpace Canvas, producing the authored world-space translation through the
normal UI layout and sprite proxy publication path.

Root fields:

- `extent`: two positive native render/display dimensions
- `camera`: axis-aligned +Z orthographic camera, with `position`, `width`,
  `height`, positive `near`/`far`, and `depthConvention: forward-z`
- `inputAssets`: nonempty `{path, sha256}` references to the actual mounted
  authored/cooked input bytes, resolved relative to the descriptor
- `cases`: one case per required route

Each case contains `route`, `sessionId`, `baselineStepId`, `currentStepId`,
`controlStepId`, `geometry`, `previous`, `current`, `ownership`, `input`, and
`captures`, plus `allowMeshlets` (true only for the meshlet case). That route
choice must match current/predecessor sealed stamps and native ACK; it does not
replace actual draw/dispatch evidence. Vertices in `geometry.vertices` have `position` and, for skinning,
normalized `weights`; alpha vertices additionally have `uv`. `geometry.indices`
is a flat triangle index list. Endpoints contain `worldTranslation`, optional
`boneTranslations` (effective skin translations after bind correction), and
optional `instanceTranslations`. This narrow fixture authors translation-only
poses, not arbitrary rotations or inferred animation poses.

`input` references immutable JSON bytes by `{path, sha256}`. The object contains
the case's `route`, `allowMeshlets`, session/step IDs, `geometry`, `previous`, `current`, `ownership`, plus root
`camera`, `extent`, and `inputAssets`. Exact descriptor/input agreement is
required. This full-case hash is sealed into all three submitted steps; session
and step distinguish baseline, moved and moved-but-disabled poses. The native
ACK must carry matching input and predecessor hashes. Changed assets or stale
pose files cannot supply this proof.

`captures.current` and `captures.control` have `path` and `manifestSha256`.
Current must capture the exact baseline-to-current submitted step transition;
control captures current-to-disabled at the unchanged current pose. Both retain
the actual schema-2 `previousSubmittedRealFrameId` seam. Root, temporal and native
submission stamps must agree on session, step, source/submitted frame,
predecessor, view, scene, history and input hashes. Reset/coalesced frames reject.
Unbound ordinary motion observations cannot pass as exact-tagged fixtures.
Nonadjacent real-frame numbers are allowed: the actual submitted predecessor is
authoritative, not `currentFrame - 1`. Its held pose needs a verified source
stamp. A separate baseline image is unnecessary and generally would not be the
immediate predecessor after readback latency.

`ownership` has `attachment`, `expectedColor` (linear RGB), `colorTolerance`
(at most .05) and `minimumContrast` (at least .05). The attachment is `baseColor`
except for world sprites, which require `postSpriteHdr`. This logical attachment
resolves exactly one `diagnosticStages` association named `hdr-<integer>-Sprite`
and its saved readback owner in the same hashed capture manifest. Shared control
readbacks must match the same graph resource/version/epoch and image metadata.
Missing/ambiguous stage associations cannot fall back to final `preToneHdr`,
which may already contain later SSS/SSR/fog processing. Alpha also has
`alphaTexture: {path, sha256}`, exactly one of the hashed input assets.
The independent reader decodes RGBA8-sRGB CECT v2 bytes and uses authored UVs
to locate both opaque pixels and holes. This fixture uses binary white texels,
point sampling, and magnified mip zero, avoiding texel-boundary probes.
Unsupported minification remains incomplete rather than guessing a mip.

## Independent ownership and motion checks

The oracle derives current interior pixel centers from authored triangles,
independently interpolates previous/current positions and nonuniform weights,
then projects with scalar orthographic math. Forward depth is
`(z - near)/(far - near)`. No production projection helper, captured motion,
renderer-computed expected vector, route flag or visibility count chooses the
expectations. Case vectors are distinct and nonzero; skinned and instanced cases
also need spatially varying vectors to reject rigid/shared-instance fallbacks.
Captured view/projection matrices must match the authored fixed camera; current
and previous jitter must be zero. Unsupported pose transforms are rejected.

Every covered sample must show expected GPU color, geometric depth and motion,
then lose its color patch and motion in the disabled control. Mesh/sprite depth
also disappears; the decal receiver remains by design. Sprites use temporal
depth and HDR, because they do not own GBuffer color or opaque scene depth.
Alpha holes must match disabled color/depth/motion, so an opaque replacement
cannot pass on the covered half alone.
Joined GBuffer command records corroborate isolation: disabled mesh controls
contain no selected geometry; decal keeps the exact receiver IDs; sprite has no
GBuffer draw; instanced geometry retains distinct nonzero instance identities.

Meshlet command evidence must belong to the captured source frame, contain a
submitted mesh dispatch without indexed fallback, and have no mesh dispatch in
the disabled control. This corroborates the path; it is not a GPU visibility
count and cannot replace pixel ownership checks. Unsupported hardware leaves
meshlet coverage incomplete.

Exit 0 and `acceptanceStatus: PASS` require complete v2 proof. Exit 1 means bad
evidence or measured disagreement. Exit 2 is INCOMPLETE for missing proof/files,
unsupported coverage, or legacy numeric agreement. Never map exit 2, generic
capture completion, authored tests, dispatch counts or a schema name to PASS.

`Tools/regression/TemporalMotionOracleTests.py` contains source-authored synthetic
tests of projection, weighted motion, input tampering, submitted identities,
alpha decoding and control rejection. They have not been run and are never GPU
execution evidence.
