# InputGraph authoring contract probe

Status: **AUTHORED, NOT RUN**. This source-only change has not been compiled or
executed. It complements `InputGraphContractTests.cpp`; it does not replace full
Editor, cook, device, or NativeAOT verification.

The probe uses the real LX registry, graph archive, domain compiler, and runtime
preparation. It covers:

- Existing LXG9 Vector3 enum and variant meanings
- InputGraph source identity and archive round-trip
- Appended Vector2 wire values, Undo, and Redo
- Layout/view edits preserving semantic and generated-interface identities
- Semantic binding edits changing execution identity without invalidating accessors
- Unknown definitions and unsupported source versions failing publication
- Typed connection and cycle rejection
- Alternative combination followed by one shared Normalize processor
- Rejection of processed subgroups that cannot be represented without changing order

## Later manual verification

From the repository root in an x64 Visual Studio Developer PowerShell, with
`ThirdParty/ownership_cpp` available, build a standalone Windows probe. Windows
SDK/Kernel32 is needed by existing LX file-atomicity helpers. The following is a
recipe, **not an executed validation result**:

```powershell
$repo = (Get-Location).Path
$out = Join-Path $repo 'Build/Obj/InputGraphAuthoring/Debug'
New-Item -ItemType Directory -Force $out | Out-Null
$sources = @(
    'Tools/regression/InputGraphAuthoringTests.cpp',
    'Lattice/Core/LXGraph.cpp',
    'Lattice/Core/LXNodeDefinition.cpp',
    'Lattice/Input/LXInputGraph.cpp',
    'Lattice/Input/LXInputCompiler.cpp',
    'Engine/Utility_Framework/InputGraph.cpp'
) | ForEach-Object { Join-Path $repo $_ }
& cl.exe /nologo /EHsc /std:c++latest /Zc:__cplusplus /utf-8 /DNOMINMAX `
    /MDd /Od /D_DEBUG "/Fo$out/" "/Fe$out/InputGraphAuthoringTests.exe" @sources
if ($LASTEXITCODE -ne 0) { throw 'Authoring probe compilation failed' }
& "$out/InputGraphAuthoringTests.exe"
if ($LASTEXITCODE -ne 0) { throw 'Authoring probe failed' }
```

Repeat with `/MD /O2 /DNDEBUG` in a separate Release output directory. Record the
actual revision, compiler, configuration, output, and exit status. Assertions do
not disappear under `NDEBUG`. The legacy fixture creates and removes one temporary
LXG file; all other fixtures are in memory. No Editor rendering, UI clicks, or
physical input is exercised by this probe.
