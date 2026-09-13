# Editor fonts

`Inter-Regular.ttf` is the unmodified static regular face from Inter 4.1.
The editor uses it for body text and headings, including the smaller asset labels.
The same release's copyright notice and SIL Open Font License 1.1 are included
in `LICENSE-Inter.txt`.

- Project: https://rsms.me/inter/
- Release: https://github.com/rsms/inter/releases/tag/v4.1
- Archive: https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
- Archive entry: `extras/ttf/Inter-Regular.ttf`
- Retrieved: 2026-09-11
- Archive SHA-256: `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`
- Font SHA-256: `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82`
- License SHA-256: `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a`

The static TTF needs no variable-font axis support. Deploy this directory to the
engine resource root's `Fonts` directory, preserving the license. The loader uses
that host-supplied resource root instead of the current working directory.
If Inter is absent, it tries Windows font candidates and then ImGui's built-in
font. Korean text retains the separate system-font candidates.

The current ImGui editor has no monospace font consumer. A separate Consolas
and bundled monospace fallback chain will be added with such a consumer.

## Material Symbols

The editor uses Google Material Symbols Outlined for semantic UI/type icons.
`MaterialSymbolsOutlined-Editor.ttf` is a modified static subset: optical size 20,
weight 400, fill 0, grade 0. It contains 58 unique glyphs for 63 named roles (8,536 bytes).
The Scene toolbar adds Menu, World, Lit and Show to the existing semantic roles.
There are no ASCII glyphs or ligatures, so merging it cannot replace body text.

- Upstream: https://github.com/google/material-design-icons
- Documentation: https://developers.google.com/fonts/docs/material_symbols
- Pinned commit: `40a7a292a79d9394157e1ea24f83d52d5e17c556`
- License: Apache 2.0, included in `LICENSE-MaterialSymbols.txt`
- Exact URLs, input/output SHA-256, modification parameters: `MaterialSymbols.provenance.json`
- Roles: `MaterialSymbols.json`; generated C++ constants: `Editor/ImGuiHelper/EditorIcons.h`
- Regeneration: install `fonttools==4.59.0`, then run `python Tools/fonts/build-editor-symbol-font.py`
  from the repository. Downloads remain under `Artifacts/phase21-material-symbols/upstream`.
  Ordinary Editor builds use the checked-in output without downloading or generating fonts.

The deployment target requires both font and license. Missing runtime icon resources
leave the text font usable and are reported by `editor.theme`; FA is never merged as
an alternative because the two fonts reuse codepoints for different symbols.
Serialized legacy window IDs retain their bytes independently of the displayed icons.

The common merge path measures the loaded text face's capital-height center and
the symbols' design center, then sets a reference-size `GlyphOffset.y`. This aligns
icons in tabs, controls and smaller labels while preserving each symbol's shape.
The offset scales with ImGui user/DPI sizing, including an explicitly sized default
text-font fallback. See `EditorIconAlignment.h` and
`docs/analysis/EditorIconAlignmentValidation.md` for the raster and Editor checks.

Inter 4.1 also maps nine of the selected private-use codepoints. Text font configs
reserve the UI private-use area U+E000–U+F8FF with one persistent
`GlyphExcludeRanges` range (three values; ImGui 1.92 allows at most 64),
including system/default fallback configs. `editor.theme` verifies this source
policy as well as glyph availability, so a text alternate cannot masquerade as
Play, Pause, Stop, Add, Remove, Font, Profiler, or a folder icon.
