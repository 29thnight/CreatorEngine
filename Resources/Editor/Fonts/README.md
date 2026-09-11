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
