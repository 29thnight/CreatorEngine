# Separate profiler analysis and viewer process

Status: implementation/source review only. Builds, tests, Windows execution,
visual comparison, captures, benchmarks and failure injection are **UNEXECUTED**.
This stage is layered on the unpublished DX capture-process commit `605a204`.
It is based on master `94e2e911` and does not include unmerged profiler-integrity
PR #126 (`3cbdeacc`). No dependency was updated, installed or restored.

## Ownership

- Editor/engine owns instrumentation, collector, recent live retention, continuous
  `.ceprof` writer and scene-owned diagnostic collection/actions
- Existing headless `DxCaptureHelper` owns its ETW consumer; the engine supplies
  its actual process/queue submissions and writes `.cedx`. The viewer never
  becomes the target just because it has a graphics device
- `ProfilerViewer.exe` owns Win32, D3D11 device/context/swapchain, its ImGui context,
  UI state, immutable capture decoding, file indexing, range loading, aggregation,
  timeline indices and drawing. No Editor, scene, engine RHI or ProfilerService
  instance is initialized in the viewer
- Existing profiler presenter sources are moved rather than forked. Frame graph,
  timeline/flame graph, tables, recorded telemetry and the full Memory, Animation
  Budget and Rendering - Live pages retain their existing controls/IDs/layout
- The viewer compiles the same neutral EditorTheme/EditorFontResources/icon code.
  Font root is injected explicitly; the Editor retains its previous resource
  root. Viewer startup inherits user scale and applies monitor DPI separately

Trace opens/focuses one viewer owned by that engine process. Repeated Trace does
not launch a second instance. Existing profiler-window CLI open/focus/close routes
are forwarded to the process owner. Explicit close targets only that owned
viewer; closing a viewer does not stop engine recording. Target exit disables
controls while retaining the last immutable data for inspection. File-only mode
has no implicit target and never substitutes viewer-local counters or services.

## Ordinary-privilege boundary

The executable is the fixed sibling
`Bin/<platform>-<configuration>/Tools/ProfilerViewer/ProfilerViewer.exe`, alongside
`Editor/CreatorEditor.exe`. The launcher passes numeric identity/scale fields and
a nonce, not arbitrary executable paths or commands. Image/path components remain
pinned, the local pipe rejects remote clients, and both peers validate PID,
creation time, Windows session, user SID, logon SID and ordinary token. Unknown or
elevated tokens fail closed. There is no UAC/runas path.

This does not remove the prior ETW decoder safety gate. Already-elevated capture
parents/helpers remain blocked. The earlier collector's cooperative ETW shutdown
and crash-orphan cleanup still need their separate Windows acceptance work.

## Live protocol and flow control

The versioned, explicitly serialized protocol validates kind, exact lengths,
nonce, monotonic packet sequence and finite typed state. No native `std::string`,
`optional`, pointer or ABI structure is copied as a wire object. Commands are
bounded known actions, not code, shell strings or arbitrary reads/writes.

- Wire packet maximum is 64 KiB; ordinary command queue is bounded to 16
- Capture payload maximum remains 128 MiB, with cardinality preflight before
  allocation/decoding. Engine live retention stays 600 frames/128 MiB
- Capture and scene diagnostics have separate bounded encoding and transfer
  lanes. Diagnostics is prioritized and does not wait behind a large capture
- Each lane coalesces latest pending data, owns at most one active serialization
  job and one bounded outgoing buffer, and cancels superseded capture epochs
- Viewer capture decoding and scene-DTO decoding never run on the UI thread;
  scene-DTO decoding also has a separate bounded executor from full file scans
- UI/GT does not wait for IPC, viewer analysis, file indexing or range preparation

Serialization requires additional transient memory beyond the producer's
128 MiB retention. It is bounded but neither zero-copy nor cost-free. Oversized
live payloads increment an explicit skipped-window count and preserve the last
valid view; they do not truncate the continuous recording. Snapshot cardinality
limits are documented in `ProfileCaptureFile.cpp` and source fixtures. No
performance claim is made without measurement.

Target identity includes process lifetime, connection nonce and QPC frequency.
Capture publication returns immutable pointer+collector epoch under the same
capture lock, and commands carry the displayed target generation. A short new
recording cannot borrow the prior file: status/artifact handoff uses a distinct
writer identity which survives ring-only Clear. Clear completion is acknowledged
only after the collector applies its ticket. Old in-flight data cannot overwrite
a new epoch. The UI labels retained prior-epoch data explicitly.

Client heartbeat counts successful non-occluded native Present calls. This gives
read-only `profile.stats.viewer` process/presentation observations without putting
viewer UI markers into the engine capture. Explicit authenticated goodbye and
owned close intent distinguish normal shutdown from broken transport.

## Live diagnostic pages

Engine-side collection and validated action application run at the existing
scene-lifetime-protected presentation boundary, before the hidden-UI early return.
Frozen values are serialized later on the transport worker. Renderer/scene
pointers never leave that boundary. The viewer uses target/session, scene and
snapshot-generation leases for requests and disables mutations when disconnected
or stale, while continuing to display last received values.

Memory retains eight viewer snapshots for A/B comparison. Transport caps are
3,840 object rows and 8,192 virtual regions. Exact source totals/category values
and omitted counts remain separate from the row subset. Full original names are
hashed with SHA-256 at engine capture; clipped labels are never identities.
Unavailable identities remain distinct, and partial/unidentified lists cannot
prove an absent object or produce fabricated unmatched-row deltas.

Animation transports up to 512 actors and 512 task rows with explicit original
counts. Selection uses stable animator ID, scene and generation, never a pointer.
Rendering preserves view selection, runtime/shadow/timing/validation surfaces and
its existing Open RenderPass structure action; that action is applied by the
Editor window owner. Bounded omitted rows/text are reported rather than read as
zero activity. Existing target address fields are inert labels only, with no
remote memory resolution, pointer traversal or persistence added.

The scene envelope is schema v2 and at most 2 MiB, including the complete bounded
rendering envelope. The conservative simultaneous cap budget is enforced in the
codec. These live pages are not silently fabricated from `.ceprof`/`.cedx` files;
file-only viewing reports the lack of a connected live diagnostic source.

## File/index/export ownership

`.ceprof` v1/v2/v3 and `.cedx` v1 meanings remain unchanged. Full `.ceprof` sessions
are not limited to the recent 600 frames: the existing bounded index/overview and
range window load the full file. No new arbitrary whole-session byte cap is
introduced; `.cedx` retains its existing 64 MiB/record limits.

A file handoff contains verified file identity and a pinned read-only path, not
an instruction to open an arbitrary device/network stream. Both ends retain
ordinary read-only handles, validate canonical paths and regular-file identity,
and reject replacement/reparse/device/ADS surprises. User-chosen file opening is
validated on a worker. A reader holds its source pin for the loaded recording.

Save exports the whole finalized recording from the matching source identity,
not the current 600-frame view. DX capture's selected destination stays in the
viewer: the engine writes a unique spool, hands over the completed/recoverable
artifact, and the viewer validates/copies it through a unique staging file before
replacement. The helper never receives an arbitrary output path.

Open/range/save cancellation checks record/chunk boundaries and suppresses late
publication. Reader work yields after one preparation request so live updates
cannot monopolize file operations. Queued work is dropped outside executor locks
at shutdown. OS file calls and bounded native snapshot codecs remain cooperative;
there is no hard guarantee on shutdown duration under a stuck filesystem/driver.

## Verification still required

1. Windows Debug/Release Editor and standalone viewer builds, Shipping exclusion,
   deployment manifest/font/license relocation and missing/stale helper failures
2. Same content size/DPI/style visual comparison for every moved page, plus native
   resize/minimize/occlusion/offscreen placement and device/backend failures
3. Real Record/Stop/Clear, short Record→Stop, repeated open/focus/close/relaunch,
   file-mode/live-mode transitions and engine/viewer crash/exit during operations
4. Full-session index/range/save, oversized live windows, recovered `.ceprof` and
   `.cedx` prefixes, corrupted metadata, canceled open/export and same-file errors
5. Peer spoof/replay, malformed frame/DTO, command generation races, slow peers,
   source replacement, Windows path/reparse/ACL cases and bounded memory pressure
6. Measure engine collector/serialization overhead and viewer memory/latency with
   populated 600-frame captures while all live pages and long-file indexing run

Source fixtures and checklists live in `Tools/ProfilerViewer/Tests`,
`Tools/regression/ProfilerViewer*`, `ProfilerLiveDiagnostics*` and
`ProfilerRenderingDiagnosticsTests.cpp`. The updated profiling Window gate also
remains unexecuted. Static review and whitespace inspection are not build, test,
visual or performance passes.
