# Native ProfilerViewer

Status: implementation and source review only. All build, execution, visual,
failure-injection and test steps below remain **UNEXECUTED**. No dependency was
installed or restored while implementing this change.

`ProfilerViewer.exe` is a separate ordinary-token Windows x64 application. Its
Win32 window, D3D11 device/context/swapchain, ImGui context and backend lifetimes
belong to the viewer. Hardware D3D11 failure can fall back to WARP. No engine,
scene, RHI, scripting runtime, Editor singleton or in-process analysis host is
initialized. Its frame presentation is the moved profiler UI, with the same
tabs, content, toolbar, Editor theme, Inter text, Material Symbols and fallback
alignment. This is not a second copy of the theme or profiler renderer.

The project uses the repository's pinned ImGui package and its existing
`win32-binding`, `dx11-binding`, `docking-experimental`, `wchar32` and `freetype`
features. No newer or separately vendored ImGui version is introduced.

## Layout and build ownership

- Application: `Bin/<platform>-<configuration>/Tools/ProfilerViewer/ProfilerViewer.exe`
- Application-local import closure: beside `ProfilerViewer.exe`
- Shared source fonts/licenses: `Resources/Editor/Fonts`, copied into the viewer's
  own `Resources/Fonts` directory by `deploy-viewer.ps1`
- Deployment inventory: `Tools/ProfilerViewer/deployment.json`, checked and copied
  by `BuildTool/EnginePublisher.cs`; publishing fails if the viewer is missing,
  stale, incomplete or version-mismatched
- The solution builds Debug/Release x64. A build-only CreatorEditor reference
  also produces the viewer for individual Editor builds
- The viewer has no native project references, no reflection injection, no
  engine launcher/runtime DLL, and no Shipping Player reference

Deployment reads the executable's import table; it never launches the viewer.
Only the selected vcpkg/VC runtime closure and fonts are copied. Engine host,
PhysX, shader compiler, scripting host and Editor-only DLL dependencies fail
deployment rather than silently expanding the viewer runtime.

## Launch and settings

Launch with no arguments for an empty viewer or `--open <path.ceprof|path.cedx>`
for offline capture analysis. A capture is opened/indexed by the viewer worker;
the UI thread does not parse capture bytes. Bare paths, mixed live/file modes,
unknown flags, duplicate flags, numeric overflow, device/network paths and
alternate data streams are rejected.

Live mode uses the Editor's complete explicit `--parent`, `--created`,
`--session`, `--nonce` identity tuple. Optional `--ui-scale-milli` accepts
500–3000 only with that tuple. The Editor passes its current user scale when
launching a new viewer; focusing the existing instance preserves viewer changes.
The client validates the peer before any
live control can be sent. A missing helper or failed connection is an error,
never a reason to fall back to an embedded viewer or request elevation.

Only `%LOCALAPPDATA%/CreatorEngine/ProfilerViewer/imgui.ini` and `window.ini` are
used for UI persistence. A missing/unwritable settings directory disables
persistence. Normal placement is clamped to an existing monitor's work area;
minimized startup is not restored. Ctrl + plus/minus adjusts viewer UI scale;
Ctrl + 0 resets it. DPI and user scale each apply once through the existing
shared theme and the ImGui 1.92 dynamic font atlas. Editor settings are never
read or overwritten by this host.
Concurrent viewers share these app-wide viewer settings; the last instance to
save wins for placement, scale and ImGui settings. No target's engine settings
are affected by that choice.

## Pending host verification checklist

All items are **UNEXECUTED**, including the source fixtures in
`Tests/ViewerArgumentsFixtures.cpp`. They are not a claim of Windows build or
runtime validation.

1. Build Debug and Release x64 using the repository's supported VS/vcpkg setup;
   confirm no SceneRuntime, RenderEngine, Editor, ProfileService or
   DxCaptureProcess implementation enters the viewer link closure
2. Compare the old profiler and separate viewer at identical window/content
   size and user/DPI scale: palette, fonts, icon baselines, left navigation,
   Frame Overview, timeline/flame graph, selected frame and every live page
3. Exercise no-argument offline startup, `.ceprof` v1/v2/v3 and `.cedx` Open,
   repeated Open while preparing, cancel, malformed/truncated/oversize captures,
   Save, and closing during analysis; the analysis queue stays bounded
4. Exercise valid live launch, focus-existing, target exit, wrong nonce/PID/
   creation time/session, malformed packets and reconnect after disconnection
5. Rename/remove the deployed helper and its runtime/font files separately:
   Editor launch reports missing-helper failure; missing text/icon fonts use the
   shared fallback path; no engine window or elevated prompt appears
6. Force Win32 registration/window failure; D3D hardware and WARP failure;
   Win32 backend failure; D3D backend/device-object failure; backbuffer/resize
   failure; device removal on Present. Each shows an actionable native error and
   cleans up every successfully initialized earlier stage
7. Resize repeatedly, minimize, restore, completely occlude, unocclude, and move
   across mixed-DPI monitors. No zero-sized backbuffer resize or busy minimized
   render loop occurs; Ctrl scaling and DPI do not multiply cumulatively
8. Close normally and via target exit while worker preparation is pending.
   Presenter publication cancels first, client disconnects, queued analysis is
   discarded outside each queue lock, and only each running operation finishes
   cooperatively before renderer/platform/context tear down, D3D releases and
   the native window closes. File-system calls can still block at the OS level;
   this is not a hard I/O shutdown deadline guarantee
9. Relaunch with a disconnected monitor, invalid window rectangle, saved
   maximized state and unwritable settings location. Window remains reachable;
   Editor workspace/settings files are unchanged
10. Launch from an elevated shell. The asInvoker manifest never elevates itself
    and the token check refuses inherited elevation with an ordinary-user error
11. Publish a development engine distribution. Verify the viewer manifest,
    app-local DLL closure, shared fonts and licenses survive relocation and the
    Editor uses the fixed sibling tool path. Publishing without a viewer build
    record must fail explicitly

## Analysis scheduling and process gate

File indexing/range preparation and live diagnostic decoding use separate bounded
viewer executors. A continuously updated reader processes one request per queue
turn; large file scans have cooperative cancellation at record/chunk boundaries.
This does not preempt an operating-system file call or claim a hard close deadline.

The updated `Tools/profiling-validation/Invoke-ProfilingValidation.ps1 -Action Window`
source checks authenticated viewer PID/present counts, explicit Close/reopen, engine
recording independence, and a nonempty engine capture without viewer UI markers.
It is **UNEXECUTED**. It does not replace visual parity, complete feature tests or
adversarial transport acceptance. `WarmupFrames` bounds observation time; unusually
slow startup needs a longer observation interval. Full matrix and protocol details:
`docs/design/ProfilerViewerProcess.md`.

## Engine deep-capture commands

The existing authenticated engine CLI/HTTP connection also exposes
`profile.deep.start`, `profile.deep.status` and `profile.deep.stop <session-id>`.
Start/status returns the ID as a decimal string and the engine-owned `.cedx`
artifact path. Start acceptance is separate from collector startup; poll the
reported state and file-finalized flag. Stop is exact-session and idempotent.
The viewer uses the same service and IPC v3, with no new buttons or elevation
options. Existing `profile.record/pause/save` and HTTP routes are unchanged.
These additional commands and their cross-CLI/viewer behavior are UNEXECUTED.
