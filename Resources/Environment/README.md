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

`CEIBL001` is little endian: 8-byte magic, 32-byte SHA-256 of the remaining
body, uint32 cube/brdf sizes, 32-byte source hash, 32-byte recipe hash, then
tightly packed RGBA16Float images in environment/irradiance/prefilter/BRDF order.
Each image is face-major, then mip-major. See `CookedEnvironment.cpp` for shape
validation and the recipe version. Shader/include changes require recooking.

Source: Blender 5.1.1, Greg Zaal / Poly Haven `ninomaru_teien`, CC0. Preserve
`LICENSE-forest.txt` when distributing the default environment.
See `docs/analysis/MAT9ThinFilmAndEnvironment.md` for implementation and measured limits.
