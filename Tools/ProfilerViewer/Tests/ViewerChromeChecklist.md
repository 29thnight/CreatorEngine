# Profiler shell and titlebar acceptance

Status: **UNEXECUTED**. Source and independent static reviews do not establish a
Windows build, a successful test, actual pixel dimensions or working input.
No application, build, test, benchmark or installer was run for this revision.

## Reference interpretation

The seven user-provided images were inspected as actual pixels before editing.
The first two show the regression: a white native caption and a tall diagnostic
block precede the chart and push the left navigation down the page. The five
references consistently use a continuous left navigation rail from the upper
corner, with the header and content beginning to its right. Their dense controls
and auxiliary information occupy bounded regions instead of consuming the
entire page above the visualization. The implementation keeps the repository's
existing dark palette, Inter and Material Symbols, rather than copying another
product's artwork or colors.

## Source-level contracts

- The fixed outer shell has zero padding, rounding and scrolling. The left rail
  occupies the whole client height; the titlebar starts after its right edge
- The dedicated engine-plus-magnifier icon is fixed at the rail's very top.
  The pages below it use a 40px rail, 26px button heights and 2px gaps, retaining
  the shared 16px glyphs and full tooltips. Only the page list scrolls; icon
  resources and explicit launch acceptance are in `ViewerLaunchChecklist.md`
- Header menus, record control, native buttons and drag geometry use one actual
  row. `TitleBarFramePaddingY` reserves at least 45 final framebuffer pixels
  without multiplying that floor by user scale or OS DPI again
- File and View move existing operations without changing their recording,
  connection, file-busy, transition and clear gates. The record glyph derives
  from acknowledged state; pending operations do not pretend to be complete
- The presenter polls once before controls. Captured/file-owned diagnostics,
  source epochs, loss counts, writer errors and stop details are retained in
  independently scrollable information sections, including file cancellation
- The workspace and information strip stay bounded. At narrow widths the
  information panel is a dismissible popup instead of squeezing the chart away
- Win32 retains the normal resizable window style. Custom nonclient hit testing
  revives the upper resize edge/corners and uses only empty caption space for
  dragging. Active ImGui controls and popups do not become a native drag region
- Editor-owned process lifetime is covered separately in
  `ViewerLifetimeChecklist.md`; independent offline viewers remain independent

## Future Windows visual and input matrix

1. Build supported Debug/Release configurations. Check source/project and
   existing ImGui 1.92 package compatibility before calling any row passed
2. Measure titlebar pixels at user scales 50/80/100/150/200/300% and OS DPI
   100/125/150/200%, including fractional combinations. Verify the 45px floor,
   larger-font allowance, no cumulative scaling and matching native hit regions
3. Compare the seven reference images to real viewer screenshots: no white OS
   titlebar, continuous upper-left rail, menus beside it, record icon immediately
   before native controls, and chart visible without scrolling past diagnostics
4. Exercise all fifteen rail pages, keyboard navigation, small windows, maximized
   windows and high user scale. Rail, page and information scroll independently;
   page changes and long integrity lists cannot move the titlebar or either rail
5. Open File/View menus in full and compact modes. Dismiss by clicking the empty
   titlebar or chart; confirm dismissal does not start a native window drag.
   Navigate menus/buttons by keyboard, including disabled recording actions
6. Exercise record, pending acknowledgment, starting, recording, pausing/draining,
   finalized, clear pending, file busy and disconnected target states. Confirm
   glyphs/tooltips match the state and repeated clicks cannot bypass admission
7. Open `.ceprof` and `.cedx`, cancel an open/export/range operation, reopen while
   preparation finishes, open the entire session and toggle Live Follow. Verify
   stale/cancelled results cannot replace newer choices and all errors are visible
8. Expand/collapse all four information sections at wide/narrow widths. Verify
   long paths, full-width session IDs, open scopes, GPU errors, dropped samples
   and writer losses remain readable without escaping the pane. Resize while the
   compact inspector is open, close it by button/outside click/Escape and reopen
9. Drag titlebar empty space, double-click maximize/restore, use all three native
   controls, top/side/bottom/corner resizing, caption right-click and Alt+Space
   system menu, Alt+F4, Snap and multi-monitor mixed-DPI movement. Menu, rail,
   record and native buttons must not act as caption drag targets
10. Close during an active popup or file preparation, minimize/restore/occlude,
    relaunch with saved placement, and remove a monitor. Confirm window reachability
    and settings ownership. Combine Editor exit/crash with the lifetime checklist
