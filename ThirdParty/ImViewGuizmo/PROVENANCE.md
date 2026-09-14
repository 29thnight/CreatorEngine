# ImViewGuizmo provenance

- Upstream: https://github.com/Ka1serM/ImViewGuizmo
- Pinned commit: `330a773ba890121eb32ecb68feb40a8a8ad6ac42`
- Retrieved: 2026-09-12
- License: MIT, copyright Marcel Kazemi. See [LICENSE](LICENSE).
- Source: upstream `ImViewGuizmo.h`; locally adapted header plus `MathematicsAdapter.h`.

## CreatorEngine changes

- Replaced the GLM default types/functions and preprocessor configuration with the engine's
  Mathematics vector, quaternion and matrix types. The widget is an inline header.
- Matrix operations use Mathematics row-vector storage/order; quaternion composition reverses
  the upstream Hamilton argument order. Camera forward is +Z and up is +Y. Snap rotations use
  `look_at_lh`, and axis depth sorting accounts for the engine camera convention.
- Blender reference styling: persistent translucent disc, colored positive axis handles and
  labels, hollow negative handles. Axis colors and positive labels remain readable on light
  scene backgrounds. Sizes are configured by the Scene overlay's DPI scale.
- Hovering a negative handle fills it with the full axis color and prints its `-X`/`-Y`/`-Z`
  label at full strength; the two-glyph label is shrunk to fit inside the handle circle.
  Upstream never draws the negative labels, so the pointer had no way to tell which of the
  three hollow handles it was over.
- All visible handles accept clicks, including the far side; depth order resolves overlaps.
- Fixed `BeginFrame` reset braces. Axis snapping requires the press to start on that axis;
  dragging from another control and releasing over an axis cannot rotate the camera.
- Replaced direction/up vector lerp with Mathematics quaternion slerp for snap animation.
  Opposite directions must not interpolate through a zero direction vector.
- `IsUsing` includes snap animation. The host owns viewport clipping, popup/focus blocking,
  press/release ownership and camera rig pose synchronization.

Normal builds never fetch from the network. To update, compare this pinned upstream header
with the proposed revision, preserve the local changes above, run the Mathematics/layout
checks in `editor.selftest`, then verify all six axis views, orbit dragging and responsive
toolbar placement in the DX12 Editor.
