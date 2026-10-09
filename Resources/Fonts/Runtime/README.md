# Runtime text fonts

These unmodified static TrueType faces are runtime data for the SDF text path.
No system font, network download, conversion, or subsetting is needed at build
or runtime. Do not remove their copyright/license notices when redistributing
the engine or a game.

- `Inter-Regular.ttf`: the same Inter 4.1 regular face used by the editor;
  default Latin face. Source archive entry: `extras/ttf/Inter-Regular.ttf` in
  https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
  License and copyright: `LICENSE-Inter.txt` (SIL OFL 1.1)
- `NanumGothic-Regular.ttf`: static Nanum Gothic regular, distributed by the
  official Google Fonts repository; Korean fallback, including Hangul syllables
  and Latin text. Copyright (c) 2010 NHN Corporation. Full copyright, reserved
  font names, and SIL OFL 1.1 are in `LICENSE-NanumGothic.txt`.
  Pinned source:
  https://github.com/google/fonts/blob/16680f8688ffcd467d2eb2146a9ce0343404581d/ofl/nanumgothic/NanumGothic-Regular.ttf
  Matching license:
  https://github.com/google/fonts/blob/16680f8688ffcd467d2eb2146a9ce0343404581d/ofl/nanumgothic/OFL.txt

Exact source identifiers and SHA-256 digests are recorded in `provenance.json`.
The downloaded Nanum Gothic bytes match the upstream Git blob identity; the
font has not been generated, converted, renamed internally, or modified.

## Deployment contract

`Directory.Build.targets` copies this complete directory to the host-provided
engine resource root's `Fonts/Runtime` directory for both Editor and Player.
The text loader resolves the default through
`PathFinder::EngineResourcePath("Fonts/Runtime/Inter-Regular.ttf")` and its
Korean fallback through the corresponding `NanumGothic-Regular.ttf` path.
It does not resolve fonts relative to the current working directory.

`CreatorBuildTool` requires both font files, both licenses, and the provenance
documents before publishing an engine or packaging a game. Game packages copy
this closure beside Player under `Resources/Fonts/Runtime`, and include these
files in the runtime hash manifest. This closure is independent of the source
checkout and Editor-only icon fonts.

Explicit project `.ttf`/`.otf` assets remain unmodified source payloads in the
Assets mount. Their authoring `.meta` GUIDs become CEMF source identities;
Player resolves them without scanning authoring sidecars. Place the applicable
redistribution license beside any user-supplied font so it is included in the
package. The cooker rewrites project font paths in text components, font bundle
entries, and prefab overrides to these GUIDs, rejecting unresolved or wrong-kind
references before publication. Absolute paths outside the package input are
rejected; reselect the registered project font in the Editor to store its GUID.
The legacy serialized `SpriteFont` asset-type value remains 3, but the
old `.spritefont` bitmap format is unsupported and is diagnosed by the loader.

## Regression inputs

This tracked directory is also the canonical binary fixture source. Regression
work should copy these exact font bytes and their notices into its owned test
Assets directory, with fixture `.meta` GUIDs. Do not obtain a machine-specific
font or commit a second modified copy under a test directory.
`Tools/regression/FontCookContract.h` covers portable source references and
prefab/bundle rewriting through the `fontcook` case in the existing experiment
contract probe; `BuildTool/Tests` covers runtime closure and font/license copying.
