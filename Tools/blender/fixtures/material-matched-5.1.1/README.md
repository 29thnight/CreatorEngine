# MAT-9 matched Core / Layered reference

Pinned **Blender 5.1.1 Cycles CPU**, 1,024 samples, seed 0, 64×64 linear HDR.
This supplements the preserved MAT-0 `material-reference-5.1.1` fixture.

`manifest.json` owns the authored values, lighting/camera settings, case list and
SHA-256 identities of the reference pixels and shared triangle geometry.
Each `.inputs` file is consumed by the native material graph builder; the validator
checks its values against the manifest. `.f32` is little-endian bottom-up RGBA32F;
`.exr` preserves Blender's scene-linear source image.

`sphere.bin` contains a little-endian uint32 corner count followed by twelve
float32 values per triangle corner: position xyz, normal xyz, tangent xyzw and uv.
Native raster uses the same mesh, perspective view and lighting. The directional
light has irradiance 1; the furnace has white radiance 1 and no direct light.

The twelve cases run in both lighting conditions: ten MAT-0 Core/Layered constant
materials, emission-only and white diffuse controls. `core_normal` uses geometry
normals here. Texture filtering, normal maps, Special transport and the original
area-light/HDRI grid are excluded. Explicit UV tangents and a 0.01-width Gaussian
filter isolate point shading; this fixture does not test anti-aliasing equivalence.
`debug-normal` and `debug-view` are setup diagnostics, not extra material cases.

Recreate into a **new** directory:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup `
  --python Tools/blender/material_matched_reference.py -- --output Build/Obj/Mat9NewReference
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label matched
```

The runner refuses to overwrite captures. It checks reference identity, captures
the exact native graph inputs/geometry, measures linear pixels and emits a contact
sheet in manifest order: **Blender | native | absolute difference ×10**.
Successful measurement does not accept a material approximation. In the first
measurement, thin-film relative RMS remained 18.348% directional / 15.646% furnace.
See `docs/analysis/MAT9BlenderImageComparison.md` for measurements and remaining gates.
