# InputGraph native contract probe

Status: **AUTHORED, NOT RUN**. No compiler, build, test binary, or repository test
script was executed while adding these sources. The commands below are a manual
recipe for subsequent authorized verification, not a record of a passing run.

`InputGraphContractTests.cpp` is a standalone native probe with no test framework.
It links the real `InputGraph.cpp`, `InputSession.cpp`, and
`InputSubscriptions.cpp`, with the repository's
`ThirdParty/ownership_cpp` headers. It does not replace the evaluator with a mock.
Checks remain enabled with `NDEBUG`; a failure identifies the scenario and exits
nonzero. A completed run prints `InputGraph contract PASS: <count> checks`.

## Manual build and run

Use the repository root in an x64 Visual Studio Developer PowerShell. The recipe
follows existing native probes' C++ latest, UTF-8, `/W4 /WX`, and separate Debug and
Release output conventions. `cl.exe` must already be available from the selected
installed Visual Studio toolchain; no hard-coded installation path is required.
The four sources intentionally need neither the full solution nor an Editor or
Player executable. No project files were changed to register this standalone probe.

```powershell
$ErrorActionPreference = 'Stop'
$repo = (Get-Location).Path
$sources = @(
    (Join-Path $repo 'Tools/regression/InputGraphContractTests.cpp'),
    (Join-Path $repo 'Engine/Utility_Framework/InputGraph.cpp'),
    (Join-Path $repo 'Engine/SceneRuntime/InputSession.cpp'),
    (Join-Path $repo 'Engine/SceneRuntime/InputSubscriptions.cpp')
)

foreach ($configuration in @('Debug', 'Release')) {
    $out = Join-Path $repo "Build/Obj/InputGraphContract/$configuration"
    New-Item -ItemType Directory -Force $out | Out-Null
    $exe = Join-Path $out 'InputGraphContractTests.exe'
    $flags = @('/MDd', '/Od', '/D_DEBUG')
    if ($configuration -eq 'Release') {
        $flags = @('/MD', '/O2', '/DNDEBUG')
    }
    $arguments = @('/nologo', '/EHsc', '/std:c++latest', '/Zc:__cplusplus',
        '/utf-8', '/W4', '/WX', "/Fo$out/", "/Fd$out/compiler.pdb", "/Fe$exe")
    & cl.exe @arguments @flags @sources *> (Join-Path $out 'build.log')
    if ($LASTEXITCODE -ne 0) {
        Get-Content (Join-Path $out 'build.log')
        throw "$configuration InputGraph contract compilation failed"
    }
    & $exe *> (Join-Path $out 'result.log')
    if ($LASTEXITCODE -ne 0) {
        Get-Content (Join-Path $out 'result.log')
        throw "$configuration InputGraph contract probe failed"
    }
    Get-Content (Join-Path $out 'result.log')
}
```

Record the exact source revision, toolchain, configuration, exit status, and logs
when these commands are actually run. This source-only delivery does not claim
that either configuration compiles or passes. Sanitizer and full product builds
are also **NOT RUN**.

## Contracts exercised

- Typed schema, stable IDs, duplicate/unknown IDs, unsupported versions, invalid
  source capabilities, processor finite values and bounds, bounded preparation,
  dependency cycles, and legal DAG ordering
- Stable interface and semantic hashes, display-only edits, deterministic
  authoring order, compatible processor changes, and atomic failed rebinds
- Exact `Started, Performed, Completed` order for two complete presses in one
  tick, final held/pressed/released state, repeated reads, typed event reads,
  wrong type/graph/interface/ABI, and OS repeat suppression
- One-tick Vector2 delta accumulation and no replay in catch-up ticks
- Ordered binding processors, distinct post-combine normalization, stable binding
  ID tie-breaks, and MostRecent combination
- `(begin, end]` boundaries, first-start inclusion, equal-time sequence ordering,
  future records retained by the caller, and lower-sequence future records after
  a higher-sequence earlier-time record has already been consumed
- Hold/Tap threshold ties, in-tick timer times, late-source/effective-time
  provenance, clamped deadlines, and zero-duration late press/release compression
- Simultaneous and sequential Chord order, inclusive windows, partial expiry,
  and duplicate participants
- Focus cancellation, no synthetic release/Tap success, digital neutral rearm,
  explicit persistent-axis snapshot resume, and analog neutral thresholds
- OnPress/OnPerformed/pass-through layer claims, no replay after failed claims,
  preservation of lower-layer events emitted before later consumption, and
  higher-layer timer priority when two Holds share a deadline
- Two users sharing one immutable definition while retaining separate timers,
  layers, rebinds, state, handles, and event provenance
- UI cursor advancing before game catch-up, explicit historical routed records,
  ownership commits, and neutral-gated handback without retroactive consumption
- Game-only pause and targeted cancellation, UI pumping without game evaluation,
  and resumed Hold timing without a paused event backlog
- Device disconnect, incarnation/assignment epochs, stale release rejection,
  held reconnect snapshots, and fresh neutral-then-press rearm
- Repeat timer deadlines, bounded event overflow with an explicit gap and one
  terminal cancellation, no overflow backlog, and saturated timestamp arithmetic
- Rejected duplicate/malformed batches and invalid boundaries without consuming
  the boundary, nonfinite data/computed-value safety gaps, and resync recovery
- Queued display/semantic reload, frame generation stamps, typed-ID compatibility,
  failed replacement atomicity, shutdown cancellation, invalidated handles, and
  retained immutable frames after reload and session shutdown
- Native typed subscriptions matching every event field, duplicate/reentrant/
  foreign-thread dispatch rejection, per-subscriber exception isolation, immediate
  self/other unsubscribe, deferred subscriber additions, session-generation stale
  batch rejection, target disable/destruction, registry invalidation, token moves,
  tokens outliving registry teardown, and registry destruction inside a callback

The repeat-overflow assertion reflects the current evaluator's 65,536 ordinary
event budget plus one cancellation for this one-signal fixture. A deliberate
budget change should update the assertion together with the runtime contract.
No assertion treats an empty event batch as evidence of successful input handling:
scenarios pair safety checks with positive valid-input/edge controls.

## Scope limits

This is a synthetic-record compiler/session contract probe. It does not establish
physical platform capture, ingress retention/clock mapping, central routing policy
publication, UI/game integration, generated-frame scheduling, LX archive round-trip,
AssetDepot/cook loading, script callback lifecycle, NativeAOT execution, full engine
linkage, hardware behavior, or performance budgets. Those need their dedicated
probes and product scenarios. Managed sources are in `InputGraphScriptProbe/` and
are intentionally separate from this target.

The fixture submits `Control`/`Resync` records with immutable recipient decisions.
For policy records, `recipients` is the targeted domain mask;
`OwnershipChanged.enabled` is the resulting ownership state. Future-record storage
belongs to the caller/ingress, so the test resubmits that same unconsumed record
when its domain boundary becomes due.
