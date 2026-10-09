# TR1 raw motion input and independent probes

Status: source-authored, unexecuted. No compiler, shader, runtime or checker was run.

## Product readback

`render.pbr.capture <new-absolute-directory> game motion` (or `editor motion`)
holds the existing native-only capture exclusion, lets the reset frame submit,
then captures a non-reset frame. It does not freeze or replay simulation. The
producer waits on actual committed temporal history, not an arbitrary frame count.
The existing command timeout still bounds a camera that continually cuts.

The completed manifest uses `captureMode: temporal-motion-v1` and adds these
float32 little-endian row-major attachments after the existing seven PBR targets:

- `temporalMotionRG.f32`: two channels, final current-to-previous render-pixel vectors
- `temporalReactive.f32`, `temporalTransparency.f32`, `temporalResponsive.f32`: one channel each
- `temporalDepth.f32`: one channel, final temporal depth

The graph copies the final blackboard versions after material, decal and sprite
motion writes. TU, FG, NIS and DeepDVC remain excluded. A normal or controlled PBR
capture still contains exactly the original seven attachments. `material motion`
and combining motion mode with controlled replay are rejected.

## Independent oracle

`Compare-TemporalMotion.py --capture <directory> --fixture ../regression/fixtures/temporal-motion/analytic-probes.json --output <report.json>`

The oracle uses independent scalar pinhole/orthographic projection, authored
previous/current local points, translations and weighted bone translations. It
does not use production motion helpers, captured motion as its expectation, or
capability/route-presence booleans. Pixel-center alignment, finite input bytes,
real-frame provenance, exact byte counts, non-reset history and sign/magnitude
are checked. The authored orthographic probes produce, respectively:

- Static rigid: (-4, +2) pixels
- Weighted skinning: (-8, -4) pixels
- Instanced: (+6, +3) pixels
- Decal: (+8, +4) pixels
- Alpha-covered mesh: (-3, -6) pixels
- World sprite: (-2, +1) pixels

These are expected vectors, not measured results. The descriptor requires
matching visible, non-occluded geometry at its authored pixel centers and real
previous/current submissions; it does not create that scene.

## Remaining acceptance gap

The product scene driver remains unauthored, not merely unexecuted. There is no
registered driver that creates these six routes,
advances their exact previous/current poses at submission boundaries and proves
the route owning each probe pixel. That driver must prepare a receiver for the
decal, weighted skinning data, independent instance identity and visible alpha
coverage, then coordinate its motion with the capture's native-history warmup.
The ordinary capture CLI continues the game simulation and cannot itself supply
this fixture sequencing. A fixture label or `temporal.motion` boolean is not
route ownership evidence.

Accordingly the checker never returns full TR1 success: exit 1 means rejected or
numeric mismatch; exit 2 means numeric agreement with route acceptance still
incomplete. `routeCoverageValidated` is always false until that producer-side
fixture and independent ownership proof are implemented and actually exercised.
Do not map exit 2, generic capture completion or named probes to acceptance PASS.
