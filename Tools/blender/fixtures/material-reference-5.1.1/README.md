# MAT-0 Blender 5.1.1 material reference

This directory is the fixed Blender side of the Phase 4.25 material comparison.
`material-grid.blend` contains one shared UV sphere mesh, two generated textures,
the packed HDRI, one area light, and one orthographic camera. The fifteen material
cases cover core, layered, and special Principled inputs. Their exact inputs,
positions, center-pixel probes, source hashes, and render settings are recorded
in `manifest.json`.

`material-grid-linear.exr` is the comparison artifact: 800 × 480, RGBA float32,
scene-linear OpenEXR saved with Blender's Raw view transform. It preserves values
above display white. `material-grid-preview.png` uses AgX only for visual review;
do not compare the engine's material pixels against that PNG. The checker and
normal PNGs are packed into the `.blend` and retained separately for inspection.

From the repository root, regenerate the reference with Blender 5.1.1:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python Tools/blender/material_reference.py -- --output Tools/blender/fixtures/material-reference-5.1.1
```

To verify a fresh render against the reference EXR without changing the fixture:

```powershell
& 'C:\Program Files\Blender Foundation\Blender 5.1\blender.exe' --background --factory-startup --python Tools/blender/material_reference.py -- --output Build/material-reference-repeat --compare-to Tools/blender/fixtures/material-reference-5.1.1/material-grid-linear.exr
```

The second command compares every RGBA float channel and rejects maximum
absolute drift above 0.002. The initial two 2026-09-28 renders had maximum
absolute drift 0.0. The fixture validates the Blender reference only. Matching
CreatorEngine pixels, feature semantics, routes, and performance are later MAT
gates. Shadows, AO, local probes, compositor, and post effects are outside this
comparison. The volume case exposes EEVEE's visible volume bounds in the
display preview; retain it as a separate special-material diagnostic when
reviewing engine parity.

MAT-4 clarified the feature boundary: Blender 5.1.1 EEVEE does not evaluate
Principled anisotropy or thin film. Their cases in this grid preserve authored
inputs, but do not prove those effects. Cycles feature references and acceptance
of the engine's three-wavelength thin-film approximation remain MAT-9 work.
The independently verified closure numeric baseline is in
`../principled-layered-5.1.1`; it does not replace this rendered EXR.
