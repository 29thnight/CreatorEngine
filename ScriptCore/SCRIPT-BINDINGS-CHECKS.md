# Generated binding source checks

These checks consume existing build products. They never invoke reflection,
engine binding generation, dependency installation, or a build. Missing or stale
products fail with the relevant path and generation guidance. Generation still
requires separate approval when execution is restricted.

## Inputs

The default configuration is x64 Debug:

- Generated set: `Build/Generated/ScriptBindings/x64-Debug/`
- Declaration manifest: `Build/Obj/SceneRuntime/x64-Debug/reflgen/SceneRuntime.declarations.json`

`-Configuration Release` selects x64 Release. `-EngineShipping true` adds the
`-Shipping` suffix. `-GeneratedDirectory <directory>` and `-Declarations <file>`
override the two paths independently; use both when both build paths were
customized. See `Tools/ScriptBindings/BUILD.md` for the producer build graph.

After authorized generation, the source gates are:

```powershell
pwsh ScriptCore/check-api-table.ps1 -Configuration Debug
pwsh ScriptCore/check-native-thread-guard.ps1 -Configuration Debug
```

The API gate requires all seven output files and checks SHA-256 of the declaration
manifest, the native and managed source inputs, the emitter script, and all six
emitted source files against the generated contract. It expands the nested Light
struct in place and requires all 187 pointer slots, with Light at slots 85–97
(byte offset 688), the fingerprint at byte 1504, and the ABI-v34 table envelope.
Headers, nested blocks, and unknown scalar fields cannot silently disappear from
the comparison. Native, managed, and generated-contract slot names must agree.
This is a source layout/order check; the emitter checks signatures and the product
contains native static assertions and managed runtime layout checks.

The thread gate first runs that read-only preflight, then examines both
`Native.cs` and `Native.Light.g.cs`. It handles their different class indentation,
requires the existing minimum of 100 handwritten static members/API uses, and
requires exactly 13 generated helpers. Each generated helper must match the
complete guarded source form: `Entered()` first, the same function pointer's null
check second, and only then the native call. `_bound` bypass detection and the
checks that `Entered` compares thread IDs and reports rejection remain active.
`Log` and its actual `PrintLog` transport are explicitly exempt to avoid recursive
off-thread diagnostics; generated Light helpers have no exemptions.

## Existing regression consumers

`Tools/regression/verify-physics-script-abi.ps1` now calls the shared API source
check for each selected configuration before its existing managed executable
probe. Its default remains `-Configuration All`; both generated configurations
must therefore exist first. It accepts the same `-GeneratedDirectory`,
`-Declarations`, and `-EngineShipping` overrides.

`Tools/regression/verify-audio-consumer-contract.py` independently expands and
checks the same complete table before its existing compiler probes. Its options
are `--configuration`, `--engine-shipping true|false`, `--generated-directory`, and
`--declarations`. Its isolated audio C++ extraction removes only the known
`ScriptLightApi.g.h` include; it does not pull generated Light/scene dependencies
into the audio stubs.

Both regression consumers still execute their existing probes when explicitly
run. They are not generation-only or read-only substitutes for the two source
gates above.

## Acceptance cases awaiting execution

No checker, generator, test, build, or analyzer was executed for this change.
The following cases describe required later validation, not recorded passes.
Use disposable copies for mutations; do not edit normal generated build output.

1. Generate the approved Debug/Release and Shipping combinations, then verify each
   selected source gate reports 187 slots and all 13 Light helpers
2. Remove any generated output or the declaration manifest: preflight must fail
   with a path and must not regenerate it
3. Change the manifest, native source, managed source, emitter, or any generated
   source without regeneration: freshness comparison must fail
4. Remove or reorder a nested field, move the Light block, duplicate a block, or
   move/remove the fingerprint tail: order/layout validation must fail
5. Supply matching but incomplete 174-slot tables: the full-count and nested-block
   requirements must reject them
6. Remove one generated helper, duplicate one, or move one back to `Native.cs`:
   helper coverage must fail even if total handwritten coverage stays above 100
7. Remove `Entered()`, remove the null guard, change `&&` to `||`, use a different
   pointer in the guard, add a separate unguarded call, or substitute `_bound`:
   the generated helper must fail. A commented-out guard is not a guard
8. Remove the thread-ID comparison, off-thread report, or false return from
   `Entered`; retain existing baseline-gate expectations that each fails
9. Run the existing physics/audio probes only after separately approving their
   execution. Confirm audio extraction remains independent of Light headers

Freshness errors may reject a mutation before the deeper source check. To inspect
an individual deeper diagnostic, a disposable fixture must carry hashes matching
its deliberately mutated source set; production contracts must never be hand-edited.
