# MAT-9 fixed thin-film image sweep

Generated with Blender **5.1.1**, build `b70da489d7f4`, Cycles CPU, 1,024 samples,
seed 0, linear Rec.709, Raw transform. The manifest records all inputs and image
SHA-256 values. Geometry, camera, light, pixel filter and bounces follow the
existing `material-matched-5.1.1` contract. These images supplement the original
fixtures without replacing them.

Eight films each have an identical film-off control under both sun and uniform
white furnace (32 images), plus emission and diffuse-white controls (4 images).
Thickness includes 0.25, 100, 200, 550 and 1,000nm; film IOR 1.4, 1.5, 2 and 3;
metallic 0, 0.6 and 1; roughness 0.5 and 0.88; coat 0 and 0.7. This is a fixed
diagnostic sweep, not exhaustive parameter coverage. `thin_film_cases.py` fixes
the parameter pairs before capture.

Only scene-linear RGBA32F images, input values, shared triangle data and manifest
are retained. EXR exports are temporary intermediates. `debug-normal.f32` and
`debug-view.f32` identify camera/geometry drift. Both material renderers use the
same two-pixel-eroded common interior; this covers cosine >= approximately 0.5.
The script reports empty lower-angle image bands explicitly. Extreme grazing is
covered by the separate optical integration oracle, not by this image fixture.

Reproduce into a **new** directory:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background --factory-startup `
    --python Tools/blender/material_matched_reference.py -- --suite thin-film `
    --output Build/Obj/Mat9FilmReference --seed 0
# Independent noise check; identical settings, seed 11:
& 'C:/Program Files/Blender Foundation/Blender 5.1/blender.exe' --background --factory-startup `
    --python Tools/blender/material_matched_reference.py -- --suite thin-film `
    --output Build/Obj/Mat9FilmRepeat --seed 11
& Tools/regression/measure-material-blender-images.ps1 -Configuration Release -Label film-sweep `
    -Reference Tools/blender/fixtures/material-thin-film-5.1.1 -ReferenceRepeat Build/Obj/Mat9FilmRepeat
& C:/Python313/python.exe Tools/regression/assess-thin-film-images.py `
    Build/Obj/Mat9Images-Release-film-sweep/comparison.json Build/Obj/Mat9FilmAcceptance/acceptance.json
```

Assessment exit 0 means every film meets the declared scene-linear engineering
target; exit 2 means valid measurements found a fidelity/noise target failure.
Malformed inputs/identity mismatch raise an error. The target is not a perceptual
JND threshold, transport parity or full-scene performance acceptance.
