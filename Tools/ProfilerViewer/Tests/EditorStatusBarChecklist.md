# Editor bottom-bar acceptance

Status: **UNEXECUTED**. The supplied screenshot was viewed as actual pixels and
the source was reviewed. No build, source-guard/test script, application launch,
runtime screenshot, input test or pixel-parity measurement was performed.

## Reference and scope

The reference has a compact charcoal strip, muted small labels/icons, thin dark
vertical dividers, flat tab backgrounds and an inset rectangular command field.
The Cmd icon/label and down-chevron precede the input rather than becoming a
single raised button. Appearance follows that reference; item order follows the
explicit request, not the screenshot's unrelated controls.

Left to right: Content Browser; Output Log containing info/warning/error icons
and totals; Cmd UI; active progress; Profiler; inactive Revision Control; existing
collider-debug gizmo toggle at the far right. No command interpreter, dispatch,
history, completion, backend connection or new Content Browser is introduced.

## Future Windows matrix

1. Compare the real status bar with the supplied reference at the same scale:
   compact height, charcoal background, muted type, flat states, thin separators,
   Cmd label/chevron and dark inset field. Verify legible icons and label baselines
   at 100/125/150/200% OS DPI and user scales 50/100/150/200/300%
2. Confirm the complete order and that debug is always the final item. Toggle
   debug repeatedly: only the original collider-gizmo collection state changes
3. Open/close Content Browser in each existing layout, including when its docked
   tab is hidden behind another panel. Confirm the established dockable panel
   opens and receives focus, without duplicate windows or a dead tab
4. Toggle Output Log from its label. Click each severity/count region: the
   existing open-log behavior is preserved, without a second toggle or new filter
   mutation. All three icons/counts remain inside the same visual tab; tooltips
   retain exact totals if labels clip. Inline totals above 999 use `999+` to keep
   each count cell bounded without changing its full tooltip value
5. Grow counts across digit boundaries, clear logs, and repeat with very large
   totals at full and narrow/high-DPI widths. Right-anchored controls and count
   screenshot-mask bounds should not move merely because the totals changed
6. Edit, paste and erase Cmd text; press Enter, Escape, Tab and arrow keys. Text
   editing must not enqueue or execute a command, trigger a backend request,
   populate history or provide completion. The disconnected state is clear
7. Show and finish each existing progress operation. Progress stays before
   Profiler, tooltips retain details, and entering/exiting progress does not
   overlap count/input/control hit rectangles
8. Profiler label, tooltip and unavailable modal use the new visible name.
   Repeated clicks preserve explicit launch/focus behavior and the stable
   `##StatusTrace` ID. Revision Control remains inactive with its explanatory
   tooltip. Combine viewer behavior with `ViewerLaunchChecklist.md`
9. Resize from wide to narrow and back, including high-scale minimum windows.
   Content Browser, log segments, Cmd and the right controls have disjoint hit
   rectangles and usable clipping. No negative size or hidden overlapping control
   may receive a click meant for a neighboring item

Actual Windows visual/input acceptance remains pending; source inspection does
not establish pixel-identical rendering or successful user interaction.
