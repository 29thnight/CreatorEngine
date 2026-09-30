# MAT-4 Layered coefficient and numeric reference

`sheen-ltc.csv` contains Blender v5.1.1's unchanged 32x32 Sheen LTC coefficients.
Columns are a, b, directional albedo; X is NoV and Y is roughness. Each row index
is `Y*32+X`. Three upstream planes are interleaved into these triples.

Normalized UTF-8/LF SHA-256:
`AEB8870B010370633FF1EBF802F87A8045244CCD7E7DCF5D99BCCB2F5A511A14`.
The source data and generated `SheenLtc.slang` use Apache-2.0. See
`NOTICE.txt` and `LICENSE-Apache-2.0.txt`. The generator preserves the copyright
and license identifiers and rejects different coefficients.

To regenerate, download the exact tagged source into the ignored build folder,
then run the checked generator from the repository root:

```powershell
Invoke-WebRequest 'https://raw.githubusercontent.com/blender/blender/v5.1.1/intern/cycles/scene/shader.tables' -OutFile Build/Obj/blender-shader-5.1.1.tables
& Tools/blender/export-lx-sheen-table.ps1
```

`numeric-golden.csv` is the independent CPU evaluator's numeric material baseline
for 35 fixtures and eight views with all Layered features. It covers direct
light, emission, point Fresnel, white furnace, anisotropic distribution/visibility,
and Fresnel at grazing and normal incidence. It is scene-linear closure output,
not a Blender rendered golden. Inputs are named and fixed in
`Tools/regression/principled_layered_probe.cpp`; field numbers match its shader.
The manifest records selected fields and hashes. A gate compares a fresh CPU
reference against the pinned baseline, separately from the GPU/CPU comparison.

EEVEE 5.1.1 does not evaluate Principled anisotropy or thin film. Those feature
comparisons require a Cycles reference at MAT-9. The current thin film uses three
effective RGB wavelengths, not Cycles spectral/CIE integration; its visual
approximation is explicitly still awaiting that acceptance gate.

Contract and validation scope: [PrincipledLayeredSemantics.md](../../../../docs/design/PrincipledLayeredSemantics.md).
