# Explicit profiler launch acceptance

Status: **UNEXECUTED**. Source inspection is not a Windows build, test run,
visual/input check or process observation. No build, test script, application,
benchmark or installer was run for this revision. The companion
`ViewerLaunchSourceContracts.py` is also unexecuted.

## Source boundary

- `initialize_profiler_viewer` installs diagnostics only. The request mailbox
  starts empty and the presentation pump calls `open_or_focus` only after an
  explicit request
- The old `FrameProfiler` stable ID remains for command/workspace compatibility,
  but has no embedded body binding and does not persist open state. Startup,
  named/reloaded workspaces and preset application skip that visibility state
- Rendering & Debug no longer supplies a profiler open/dock override. A saved
  legacy ImGui section can be read without drawing or launching an empty panel
- Trace, Window > Frame Profiler, explicit Rendering - Live controls and CLI
  `editor.window ###Editor.FrameProfiler open|focus|rendering-live` retain their
  existing request paths. Closing uses the existing process close request
- Recording, transport identity, private job ownership, launch admission and
  standalone/offline behavior are unchanged. See `ViewerLifetimeChecklist.md`

## Future Windows behavior matrix

1. Cold Editor startup with a clean workspace: no ProfilerViewer process/window.
   Wait through multiple presentation frames, then Record/Stop through the
   existing engine controls/CLI without opening the viewer; recording still works
2. Startup with a pre-change active workspace whose FrameProfiler open bit is
   true and whose ImGui section is docked, floating or hidden behind another tab:
   no viewer and no empty embedded profiler panel
3. While the viewer is closed, reload that workspace, load a named copy, apply
   Rendering & Debug, reset and save/reload each layout: none launches a viewer.
   Repeating these actions while a viewer is already open neither closes nor
   duplicates it; layout changes are not viewer lifetime commands
4. Click Trace: exactly one ordinary-token owned viewer opens. Repeated Trace
   focuses the same process. Close the viewer, continue recording, then click
   Trace again: one fresh viewer opens; the old layout cannot reopen it by itself
5. Window > Frame Profiler opens/closes the viewer and reflects process status.
   Rendering - Live explicit buttons and menu items open/focus that page. Every
   case still works after workspace reload or preset application
6. CLI open/focus/rendering-live launches or focuses one viewer; close stops it.
   Repeated close is safe. Unrelated editor.window commands keep their behavior
7. Combine close/reopen and Editor shutdown/crash with the ownership matrix in
   `ViewerLifetimeChecklist.md`. An independently invoked no-argument or --open
   viewer remains independent; two Editors never close each other's viewers
8. With the deployed viewer missing, only an explicit opening action reports the
   missing-helper failure. Startup/workspace/preset does not attempt launch or
   emit that failure. No embedded/elevated fallback is introduced

## Branding and density

1. Inspect executable properties/Explorer, native small/large window icons,
   taskbar and Alt+Tab: the engine crown/blue icon plus bottom-right magnifier is
   used, with clean transparent edges. Move between 100/125/150/200% DPI monitors
2. The same artwork is first in the left rail, above every analysis page. Scroll
   the page rail in a short or high-scale window: the brand stays at the top
3. Compare all fifteen 26px rows/2px gaps in the 40px rail against the former
   36px rows/4px gaps in a 48px rail, at identical DPI/user scale. At 100% a
   typical 720px-high client should show every page without scrolling; measure
   the actual result. The shared 16px icons, tooltips, hover/selected states and
   keyboard focus remain legible; smaller windows retain independent scrolling
4. Keep the title-row 45-physical-pixel floor after scaling, menus to the right
   of the rail, right diagnostics and every page/control from PR #128. This
   revision does not change titlebar hit testing or those layout contracts
