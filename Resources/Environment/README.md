# Cooked engine environment

`forest.ceibl` is the engine default selected by the user. Only cooked pixels,
the generated `forest.cook.json` record and the CC0 notice are distributed here.
The original EXR is an authoring input and is not a runtime dependency.
Bootstrap retains its diffuse/specular IBL while hiding the forest background.
Hidden backgrounds use neutral linear gray (RGB 0.18); explicit HDR/cooked
environment selection enables the background. Scene Show can hide it locally.

```powershell
& Tools/AssetCooker/cook-environment.ps1 -Configuration Release
```

The default source is Blender 5.1.1's installed `forest.exr`. Supply `-Source` and
`-Blender` for other installations. A matching source/recipe cache bypasses EXR
decoding and IBL generation. `.hdr` inputs use the engine's existing decoder.
The producer refreshes the `.cook.json` record on both hits and misses.

`CEIBL005` is little endian: 8-byte magic, 32-byte SHA-256 of the remaining
body, uint32 cube/brdf sizes, 32-byte source hash, 32-byte recipe hash, uint32 source width/height, then
tightly packed images in environment/irradiance/prefilter/BRDF order. Environment
and prefilter use RGBA32Float to retain HDR emitters above the half-float range;
irradiance and BRDF use RGBA16Float. Rect-to-cube uses a solid-angle-weighted 4x4
texel footprint with source mip 0. Cube size remains 512. The default cook is
103,159,928 bytes (98.38 MiB), 128 KiB more than v4.
Each image is face-major, then mip-major. Three RGBA32Float 2D maps follow:
full-resolution cube luminance/CDF rows (`cubeSize × 6*cubeSize`), marginal
(`1 × 6*cubeSize`), and cached direction/PDF samples (`5120 × 2`). The first
1024 columns retain the original proposal; the last 4096 are the Scene
reflection proposal. Diffuse/sheen keep the first bank. Reflection balances
1024 BRDF samples against 4096 environment samples. These maps are
followed by the retained decoded RGBA32Float source (1024 x 512 for forest).
All eight maps are uploaded at bootstrap; no CDF generation or
generation shaders are needed on a warm load. Old CEIBL001 authoring files can
be read with a one-time CPU proposal derivation; CEIBL002 retains its old half-float
layout; CEIBL003 retains float radiance without the source. CEIBL004 retains
its source and original 1024-sample layout. Historical layouts miss the current
recipe and are recooked on authoring selection; an existing source-bearing
1024-sample capture preserves its v4 layout when roundtripped.
Scene base/coat/sheen reflections read the cached 2D source with float bilinear
wrap-U/clamp-V interpolation. The original EXR is not loaded at bootstrap.
See `CookedEnvironment.cpp` for shape
validation and the recipe version. Shader/include changes require recooking.

Source: Blender 5.1.1, Greg Zaal / Poly Haven `ninomaru_teien`, CC0. Preserve
`LICENSE-forest.txt` when distributing the default environment.
See `docs/analysis/MAT9HdriConvergence.md` for the current fixed HDRI acceptance
and remaining performance/special-material gates.
