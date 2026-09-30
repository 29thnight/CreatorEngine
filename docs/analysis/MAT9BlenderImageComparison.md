# MAT-9 Blender / native image comparison

2026-09-30. Matched **Core / Layered constant-input** image measurement, updated
after the visible-spectrum thin-film implementation.
MAT-9 remains in progress; these results do not close Special transport, the
textured area-light/HDRI grid, route parity or performance acceptance.

## Shared inputs and capture

- Blender **5.1.1 Cycles** (build `b70da489d7f4`), 64×64 perspective view, eye (0,0,3), 45° FOV,
  no denoising, exposure zero, Raw view, 1,024 samples.
- One shared imported sphere: Blender exports the exact triangle corner positions,
  normals, active UV tangents and UVs consumed by native raster.
- Twelve cases under a unit-irradiance directional light with no environment, then
  under a unit-radiance white environment with no direct lights: **24 images**.
- Ten MAT-0 Core/Layered authored constant-value cases plus an emission-only control
  and a diffuse white / IOR=1 control. The `core_normal` case uses the default
  geometry normal here; the MAT-0 normal texture is excluded from this measurement.
- Native DX12 Release uses actual graph generation, complete Scene compilation,
  immutable material instance, geometry sealing, SceneHost GBuffer/lookup/color and
  shared depth. AO is neutral white; shadows, decals and post effects are excluded.
- Native half-float lighting is read back and converted to bottom-up RGBA32F for
  comparison with Blender scene-linear EXR pixels. No display transform is used
  for metrics. The contact sheet applies the same sRGB display transform to both.
- A common emission-control mask is eroded by two pixels. Every material uses this
  same interior mask; highlight pixels are retained. Nonfinite values and changed
  input/reference identities are rejected before error measurement.

The original `material-reference-5.1.1` area-light/HDRI fixture is preserved.
The supplemental frozen reference is `Tools/blender/fixtures/material-matched-5.1.1`.

## Comparison setup corrections

Two apparent rendering errors came from differing inputs:

1. Blender BOX reconstruction ignores its RNA width and always uses a one-pixel
   box. Native raster evaluates pixel centers. A 0.01-width Gaussian reference
   isolates point shading; it does not verify anti-aliasing equivalence.
2. Blender's unconnected anisotropy tangent is not the engine's imported UV tangent.
   The matched reference explicitly links the active UV tangent. Generated/default
   Blender tangent semantics are outside this particular comparison.

Source checks used the pinned Blender
[film synchronization](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/blender/sync.cpp),
[shader nodes](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/scene/shader_nodes.cpp)
and [closure setup](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/svm/closure.h).
The geometry diagnostic additionally captures normal/view fields from the native
lookup and emits Blender geometry normal/incoming fields independently.

## Measured results

Relative RMS is `sqrt(sum((native-reference)^2) / sum(reference^2))` over the common
interior RGB samples. It is not a frame-rate metric. Final results use
345 interior pixels per image. Native DX12 **Debug and Release each** completed
24 captures, 400,495 checks, zero GPU validation messages and normal shutdown.
All 393,216 RGBA components of the 24 images are byte-identical across configurations.

| Case | Directional relative RMS | Furnace relative RMS |
|---|---:|---:|
| Core base | 0.697% | 0.284% |
| Core metal | 0.269% | 0.365% |
| Core rough | 1.941% | 0.136% |
| Core geometry normal | 0.760% | 0.119% |
| Core emission | 0.045% | 0.104% |
| Coat | 0.182% | 0.147% |
| Sheen | 0.090% | 0.176% |
| Anisotropy, explicit UV tangent | 0.937% | 1.019% |
| Thin film, current Fourier LUT / F82 | **2.835%** | **1.352%** |
| Mixed layers | 0.071% | 0.120% |
| Emission-only control | **0.000%** | **0.000%** |
| Diffuse white control | 0.045% | 0.066% |

Small differences include the native BRDF/integration approximations, Monte Carlo
noise and half-float lighting quantization. They must not all be attributed to one
of those terms without an additional isolation test.

The historical RGB 650/550/450nm film baseline was **18.348% / 15.646%**.
The current LUT, dielectric diffuse weighting and substrate Fss corrections reduce
that difference to the table above. Non-film cases retain their previous values.
See [thin-film implementation, dense spectral reference and GPU cost](MAT9ThinFilmAndEnvironment.md)
for the separately measured contributions and approximation limits.

### Reference noise isolation

A second complete Blender render used seed **11** instead of **0**, retaining the
same mesh, inputs, 1,024 samples and render settings. Over the same comparison mask:

- Directional maximum seed-to-seed relative RMS: **0.05802%** (Core metal).
- Furnace maximum seed-to-seed relative RMS: **0.22359%** (thin film).
- Thin film seed-to-seed: **0.00636%** directional / **0.22359%** furnace, compared
  with current native/reference differences of 2.835% / 1.352% (historically
  18.348% / 15.646% for RGB 3 wavelengths).
- Core rough directional seed-to-seed: **0.00132%**, compared with 1.941% native
  difference. That remaining direct-light difference cannot be explained by the
  measured reference noise alone. Its BRDF/energy approximation needs separate judgment.

This is one independent seed pair, not a statistical confidence interval or a
universal convergence bound. It identifies the measured film difference as much
larger than this reference's sampling variation.

The current engine uses visible-spectrum Fourier sensitivity, three Airy orders
and an F82 model at the film/metal interface. The residual difference remains above
the measured seed variation. This is **not accepted rendered parity**; spectral
order truncation, RGB optical reconstruction and the remaining BRDF approximations
need material-specific acceptance bounds.
See the pinned [Cycles Fresnel implementation](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_util.h)
and [microfacet Fresnel](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_microfacet.h).

## Reproduction and evidence

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup `
  --python Tools/blender/material_matched_reference.py -- --output Build/Obj/Mat9NewReference
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label comparison
# Independent reference noise measurement:
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup `
  --python Tools/blender/material_matched_reference.py -- --output Build/Obj/Mat9Repeat --seed 11
& 'C:\Python313\python.exe' Tools/regression/compare-material-blender-images.py `
  Tools/blender/fixtures/material-matched-5.1.1 Build/Obj/Mat9Images-Release-comparison/Native `
  Build/Obj/Mat9Images-Release-comparison/comparison-with-noise.json --reference-repeat Build/Obj/Mat9Repeat
```

The script reports **measurement completion**, leaving material acceptance pending.
It rejects existing output directories, uses the native GPU validation layer,
restores its environment and never launches or modifies the user's live Editor.
Reference and captured native triangle bytes and parsed graph values are checked
against the manifest before comparison, in addition to the reference image hashes.

Local evidence:

- `Tools/blender/fixtures/material-matched-5.1.1`: frozen Blender source images and setup.
- `Build/Obj/Mat9Images-Release-frozen-final` and `Mat9Images-Debug-frozen-final`:
  historical RGB-3 native captures, inputs/geometry, build and validation logs.
- `Build/Obj/Mat9Images-Release-film-spectral-v4` and
  `Mat9Images-Debug-film-spectral-final`: current LUT captures and regression evidence.
- `Build/Obj/Mat9MatchedReference-repeat-11`: independent seed-11 reference.
- `Build/Obj/mat9-blender-comparison-frozen-final.json` and `.png`: metrics, reference
  noise measurements and contact sheet.
- Identity rejection checks: modified input value, triangle bytes, reference pixels
  and an added case were all rejected; frozen input files were preserved.
- Dashboard full parse: 435 entries, no malformed rows, all progress values finite.

## Next required work

1. Film acceptance: judge the current wavelength-integrated replacement's residual
   image differences, grazing-angle limits and measured cold/warm cost. The
   implementation and first cost measurement are complete; rendered acceptance remains.
2. Special transport and texture/factor/normal-map reference scenes, with shared
   supported geometry/background/transport conditions and explicit approximation limits.
3. Define material-specific acceptance bounds using repeated reference noise checks;
   numerical closure tests do not substitute for the rendered-image bounds.
4. Continue Deferred/Forward route parity and moving-camera/tier/cold-warm performance
   acceptance after the first comparison's outstanding fidelity cases are addressed.
