# Editor image icons

Entity presets and Content Browser folder/file icons use the official **Microsoft
Fluent Emoji, 3D** PNG artwork. Default entities use **Package** (the cardboard box).
Functional toolbar glyphs use the separate Material Symbols font.

- Source: https://github.com/microsoft/fluentui-emoji
- License: MIT; the complete original notice is in `LICENSE-FluentEmoji-MIT.txt`
  and is copied into the editor distribution with these resources.
- `FluentEmoji.provenance.json` records every source path, immutable commit URL
  and SHA-256 checksum. PNG bytes are unchanged; the UI scales them when drawing.
- Run `Tools/icons/Fetch-FluentEmoji.ps1` to restore the pinned artwork and notice.
  No network or emoji font is needed at runtime.

Entity IDs remain stable so saved presets and Undo records survive artwork changes.
The Content Browser shares images between its tree, tile and list presentations.
File-type artwork is a placeholder; asynchronous asset thumbnail generation remains
the separate PHASE 21 W7 work item.

Engine.png and the scene light/camera gizmo textures are independent editor artwork.
The five `*Gizmo.png` files must stay 128x128 RGBA: the live gizmo icon pass samples
them without mipmaps, so larger masters alias when shrunk on screen and cost 6 MB+ each.
Downscale new artwork before committing; `verify-editor-icon-resources.ps1` enforces it.
