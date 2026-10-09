# Profiler icon asset

`ProfilerIcon.png` is the dedicated CreatorEngine Profiler icon. It retains the
existing cyan rounded-square and white crowned chess-piece brand from
`EngineIcon.png`, with a white/navy magnifying glass at the bottom-right.
The engine's original assets are unchanged.

## Files

- `Resources/Editor/Branding/ProfilerIcon.png`: 1254 × 1254 RGBA master, with a
  transparent exterior, retained as the high-resolution artwork source
- `Resources/Editor/Icons/Profiler.png`: 256 × 256 RGBA derivative embedded for
  the viewer's fixed left navigation rail, reducing resource and texture size
- `Tools/ProfilerViewer/ProfilerViewer.ico`: native Windows application/window
  icon, containing 32-bit RGBA PNG frames at 16, 20, 24, 32, 40, 48, 64, 72, 96,
  128, and 256 pixels, matching the engine ICO's resolution set

The outputs were visually inspected, including the small sizes composited on
light and dark backgrounds. The magnifier remains inside the canvas. The PNG is
the selected generated result without compositing, painting, or alpha changes;
the rail PNG and ICO are only encoded, Lanczos-resized derivatives. The generated master has
near-opaque interior alpha and a few essentially invisible exterior pixels at
alpha 1/255; the exterior remains transparent in normal compositing.

## Generation provenance

Created on 2026-10-07 with the built-in image-generation editor, using
`EngineIcon.png` as the local edit target and requesting true transparency.
No external API script or additional image model was used.

### Initial edit prompt

Use case: precise-object-edit

Asset type: dedicated CreatorEngine profiler application icon, square PNG with
true alpha transparency

Input image: edit target is the supplied existing CreatorEngine app icon, a
bright cyan-blue rounded square with a white sculpted crowned chess queen. This
is the exact established brand.

Primary request: Keep the existing icon and its queen, color, material,
proportions, and composition unchanged. Add ONE clear magnifying-glass overlay
badge at the bottom-right. The queen must remain visually identical and fully
recognizable. The magnifier is a simple bold circular white/silver rim with a dark
navy inner outline and short thick dark navy handle extending diagonally toward
the bottom-right. Its round transparent/light-blue lens should show the
underlying icon. It occupies roughly the bottom-right 35% of the icon, with its
circle centered near 76% width and 74% height and handle ending near 94% width and
94% height. Make it unmistakably a magnifier at 16, 24 and 32 pixels, with chunky
silhouette and restrained 3D shading matching the existing icon.

Constraints: preserve the original rounded-square cyan background and white
crowned queen without redesign, preserve transparent corners/background outside
the icon, no extra backdrop, no text, no letters, no chart, no dots, no other
glyphs, no cropping. Keep all icon content and the magnifier safely inside the
square canvas with an approximately 3% transparent outer margin. High-fidelity
brand-preserving edit, crisp clean app-icon asset.

### Final refinement prompt

Use case: precise-object-edit

Asset type: production transparent application icon

Edit ONLY the transparency/outer edge of Image 1. Image 1 is the accepted
CreatorEngine profiler icon with white crowned chess queen and bottom-right
white/navy magnifying glass. Image 2 is the original clean engine icon and
supplies the clean rounded-square boundary.

Keep the artwork inside Image 1 exactly unchanged: the cyan blue rounded square,
white queen, and magnifier must retain the same appearance, location, size and
colors. REMOVE all stray cyan flecks, jagged transparency, noise, fragments or
halos beyond the rounded-square boundary. Match the smooth clean exterior
silhouette of Image 2 while preserving the magnifying-glass handle extension at
bottom-right. Fully opaque solid RGB artwork inside the icon (alpha 255); fully
transparent canvas outside (alpha 0), with only a narrow smooth anti-aliased
transition around the outer edge. Keep roughly 3% transparent padding, no outer
shadow, no clipped pixels, no added backdrop, no checkerboard, no redesign.

## Re-encoding the rail PNG and ICO

With Pillow available, run from the repository root. This only resizes and
encodes the committed master; it does not regenerate or redesign the artwork.

```python
from PIL import Image

sizes = [(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 72, 96, 128, 256)]
with Image.open("Resources/Editor/Branding/ProfilerIcon.png") as icon:
    rail = icon.resize((256, 256), Image.Resampling.LANCZOS)
    rail.save("Resources/Editor/Icons/Profiler.png")
    icon.save("Tools/ProfilerViewer/ProfilerViewer.ico", format="ICO", sizes=sizes)
```
