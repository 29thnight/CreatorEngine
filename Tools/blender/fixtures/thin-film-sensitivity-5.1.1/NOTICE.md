# Thin-film Fourier sensitivity data

`cie-fourier.csv` is extracted without numerical changes from
[Cycles 5.1.1 shader.tables](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/scene/shader.tables).
Copyright 2011-2025 Blender Foundation. Apache-2.0; see `LICENSE-Apache-2.0.txt`.

- Input SHA-256: `687e8ac2481fc52fe4bdc6c90eceba1ccb3794a5bda5a26d09b5fe543d0467d4`.
- 512 complex XYZ entries, Fourier domain covers 0 to 60 micrometers.
- Sampling coordinate: `2*pi*opticalPathDifferenceNm/60000`, clamped to [0,1].
- Offline generator: `Tools/blender/generate-thin-film-sensitivity.py`.
- Slang/C++ data: XYZ transformed to linear Rec.709, normalized per channel by
  the zero-frequency DC component. `--check` validates exact generated output.

The optical evaluator uses the Fourier Airy series of Belcour and Barla (2017).
Interface phase conventions/F82 response are adapted from
[Cycles bsdf_util.h](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/kernel/closure/bsdf_util.h),
Copyright 2009-2010 Sony Pictures Imageworks Inc., et al.; 2011-2022 Blender
Foundation. These portions are BSD-3-Clause; see `LICENSE-BSD-3-Clause.txt`.

`cie-1931.csv` contains the 473 CIE 1931 XYZ rows (359..831nm at 1nm) from
[Cycles precompute/thin_film_table.py](https://github.com/blender/blender/blob/v5.1.1/intern/cycles/doc/precompute/thin_film_table.py).
The enclosing Blender precompute source is Apache-2.0. These rows are used for
direct wavelength quadrature by `Tools/regression/measure-thin-film-spectrum.py`.
SHA-256: `cb709c73278d6d9d269ce2df87d4ea0eff4a103d260ba6fbf1ca1cb28d23ae79`.
