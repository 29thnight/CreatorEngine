# DX12 deep capture: separate ETW collector

## Scope and status

This is an optional Windows x64 development feature. The ordinary Editor keeps its renderer,
DX12 timestamp queries, profiler and UI. `CreatorDxCaptureHelper.exe` is a headless ETW consumer:
it creates no graphics device, command queue, swapchain, window or replay capture.

The first slice records DirectX submission and calibrated GPU-work intervals using Microsoft's
[DxTimingCaptureLibrary](https://github.com/microsoft/DxTimingCaptureLibrary) at commit
`05bf5ff1d9e1b1d63c7420a166e61ded2ca7c072`. The vendored source provenance, hashes and notices
are in `ThirdParty/DxTimingCaptureLibrary`. It is not WinPixTimingCapturer.dll.

The source was implemented and reviewed without building, running tests, launching the Editor
or helper, capturing ETW, installing an SDK, changing permissions, or triggering UAC. Runtime
correctness and security acceptance remain open. Do not present the source review as a Windows
build, working driver capture, measured overhead, or a security certification.

**Elevation is intentionally unavailable in this first slice.** Static review found unsafe
payload/index handling in the upstream decoder. Some local bounds guards are included, but the
complete parser has not passed adversarial Windows validation. The Editor never invokes `runas`,
and the helper refuses an elevated token, including a manual administrator launch. Administrator
launch is not a supported workaround. Ordinary-token collection works only where the account's
existing ETW permissions allow it; otherwise the feature reports unavailable.

Not included in this slice:

- PIX user marker text and Begin/End scope decoding. Upstream's decoder stub is deliberately
  used; the capability is explicitly unavailable. Raw API marker IDs are retained as opaque IDs
- PSO compilation, residency, DirectStorage, context-switch sampling or call stacks
- Vulkan live GPU timing or a standalone viewer application
- Installation, a persistent service, Performance Log Users membership changes or credentials

## Opt-in build and UI

`EngineDxDeepCapture` defaults to `false`. Existing profiling remains the default. Development
Debug/Release x64 builds can explicitly set `EngineDxDeepCapture=true` and
`DxTimingCaptureSdkInclude=<existing Microsoft.Direct3D.D3D12 1.619.5 build/native/include>`.
The helper project checks the package's retained nuspec version and required headers. It never
restores a NuGet package or installs an SDK. Agility headers precede the Windows SDK headers.
Shipping cannot build or link the collector. Offline `.cedx` viewing does not require the SDK.

The CreatorEditor project conditionally builds the helper without linking it into the Editor.
The helper uses a static CRT, its own asInvoker manifest, and system-only dependencies. Its fixed
location is `DxCaptureHelper/CreatorDxCaptureHelper.exe` below the Editor executable directory.
The helper is excluded from engine reflection and vcpkg generation paths.

In the Profiler's `DX12 Deep Capture` page:

1. Choose a new `.cedx` output path and click `Start DX12 Deep Capture`
2. The normal-token helper is tried. Insufficient ETW permission is reported; there is no UAC retry
3. Stop requests asynchronous draining; the Editor worker finalizes and opens the recorded file
4. `Open .cedx` reads a bounded file and prepares immutable links off the UI thread

Capture is bounded to 60 seconds, the file to 64 MiB and 262,144 records. Each transport/submission
queue has 8,192 records. ETW uses bounded real-time buffers. Caps, decode failures, queue drops,
source loss, missing finalization and unfinished executions are visible rather than filled with
zero timing data. The helper also bounds decoded input and process memory. These bounds may stop
a busy capture early; the costs and effect of system-wide provider traffic require measurement.

The existing `profile.stats`/recording payload includes `dxDeepCapture` status. There is no new
unattended command that triggers UAC. Capture controls are independent of `.ceprof` Record/Stop.

## Clocks and identities

ETW uses QPC input (`ClientContext=1`, `PROCESS_TRACE_MODE_RAW_TIMESTAMP`). The session stores
its original QPC start, frequency, target PID, process creation time and Windows session ID.
That tuple identifies a recorded session; the randomized IPC nonce is a separate security token.

The upstream converter contract is **nanoseconds**, even for raw-QPC input. Its implementation
adds GPU nanosecond deltas to converted CPU clocks. Returning raw QPC ticks from that converter
would silently mix units. The helper therefore uses checked integer QPC-to-ns conversion, keeps
the upstream callback ns values, and derives a QPC-domain view at the recording boundary.
For supported frequencies up to 1 GHz, the CPU submit inverse uses ceiling to recover the exact
original QPC tick. GPU intervals are calibrated/rounded QPC coordinates and retain original ns;
they are not native raw GPU hardware ticks. Captures do not use the viewing machine's clock.

Upstream API queue IDs are session-local callback IDs. Engine submission records carry opaque
native queue addresses. These two ID spaces are never equated by numerical value. A GPU execution
is attributed to an engine submission only when its PID, TID and exact CPU submit QPC lie in one
unique `ExecuteCommandLists` before/after interval. Zero candidates remain unmatched and multiple
candidates remain ambiguous. There is no nearest-frame or current-frame guess.

The renderer copies exact `GpuFrameToken` frame/submission/view values through the recorded batch.
Immediate submissions without that token retain unknown IDs. Submission ID zero can be a valid
first token, so validity is explicit. The prior query `cpuSubmitTick` remains the time the query
recording opened; it is not silently redefined as the actual API call.

Execution IDs join begin/work/end records; hardware queue is a work attribute. The upstream
`commandListIndexInExecution` value is retained as an opaque list token, not assumed to be a
dense index bounded by the engine's submitted-list count. Raw marker IDs have no decoded names.

## Artifact and viewer

`.cedx` v1 is separate from `.ceprof`. No v1/v2/v3 record type, flag, size, CRC rule or reader
meaning is changed. The file contains versioned headers, bounded fixed-size records, sequences,
checksums and a final summary. A recovered prefix cannot become a complete capture just because
the prefix parses. Original timing and identity fields survive save/open.

The viewer renders prepared work links with list clipping, gray submit-to-start intervals and
blue GPU-work intervals on one stored QPC axis. Tooltips expose source ns, QPC, ETW queue IDs,
engine queue identity and explicit association state. Intervals can overlap and must not be summed
as exclusive GPU busy time. Unmatched work remains visible. The helper never receives an output
path; only the ordinary Editor writes or reads the artifact.

## Privilege boundary

The protocol accepts fixed-size versioned messages and bounded record kinds, not shell commands,
DLLs, plugins, paths or arbitrary target processes. Local pipe endpoints bind a fresh nonce,
verified peer PID, process creation time, same user and Windows session. Remote clients, replay,
incorrect message sizes, unknown enums, timeout and parent exit terminate the connection.

Elevation is rejected unconditionally while decoder security validation remains incomplete.
Additional installation checks are retained for future reviewed work. Matching a filename or being beside the
Editor is insufficient: the implementation checks held no-reparse paths and protected owners/ACLs,
the helper's dedicated directory and PE dependency policy. Protected deployment must contain only
the intended helper executable in its dedicated folder; symbols and other files are kept elsewhere.
The linker uses system-only dependent-load flags; supported OS and emitted PE policy require
verification before elevation. System-only DLL search also applies after helper entry.

The blocked-elevation result affects only deep capture. Nothing changes the Editor token, existing profiling,
group membership, service installation or persistent permissions. The helper owns a fresh ETW
session handle and stops only that handle. A name collision never grants ownership of a session.
The parent lease, capture bound and shutdown path stop collection on disconnect or process exit.

## Relationship to profiler integrity work

This change starts from master `94e2e911`; it does not merge or reimplement PR #126. That PR's CPU
scope pairing, generation and GPU-query stop-tail fixes remain independent. Adding ETW does not
repair the existing `.ceprof` boundary behavior or claim that the two recordings have identical
start/stop coverage.

## Required Windows acceptance (not run)

- Build default/off and opt-in Debug/Release x64; inspect Shipping symbols and helper absence
- Verify static CRT/import table, PE dependent-load flags, and actual Windows loader resolution
- Normal permission success/denial, rejection of elevated Editor/helper and absence of UAC,
  wrong PID/user/session, replay and malformed pipe clients
- Pre-created pipe/session names, `.local`/manifest/DLL planting, reparse/rename races and ACL changes
- Immediate Stop, Stop/exit during startup, helper crash, parent death, repeated start,
  slow reader, ETW overflow, file cap, disk error and recovered/truncated artifact
- Real DX12 queue-to-work correlation, multiple views/lists/queues, clock frequency/rounding,
  delayed history buffers, missing PIX decoder, GPU/device loss and no-work captures
- Compare decoder results against Microsoft's reference consumer; measure disabled hot-path cost,
  helper memory/CPU, dropped events and Editor frame-time impact before claiming performance

`Tools/regression/DxCaptureFileTests.cpp` contains source fixtures for format, bounds, loss and
identity contracts. They were added for a future authorized test run, not executed here.

Helper teardown is cooperative: stopping its owned ETW session and closing the consumer normally
releases `ProcessTrace`, but an upstream callback that stalls can delay its thread join. The Editor
uses a bounded client shutdown and marks a nonterminal result incomplete. Cleanup of an ETW session
after a helper process crash has not been established; no code guesses or stops other sessions to
hide that residual. These are open acceptance items, not verified guarantees.
