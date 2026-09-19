# Product AO and emission fixture

Six separate quads use constant materials; regenerate with `python make_lighting.py`.
No external asset or library is required. Local centers are listed in the generator.

Top row: linear AO texture R=64/255 at strength 1, the same texture at strength 0,
and an absent AO texture. Expected GBuffer AO is 64/255, 1, 1. All three use
linear base color 0.6, roughness 1, metallic 0.

Bottom row: black base color with constant emission (0.5, 1, 2), the same
emission multiplied by sRGB-decoded (128, 64, 32)/255, and emission disabled.
The constant patch must emit without an emissive texture. The disabled patch
is the product lighting control. Pixel checks project known patch centers through
the captured camera and sample an interior region, so metadata alone cannot pass.

The baseline renders this model through the actual importer, material snapshot,
GBuffer, Deferred, screen-space effects and post chain. Numerical pass tests
separately verify AO affects indirect lighting while preserving direct lighting.
