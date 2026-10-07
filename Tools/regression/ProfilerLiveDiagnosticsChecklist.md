# Live diagnostics migration verification

Status: UNEXECUTED. The task explicitly prohibits builds, tests and application
execution. These are source fixtures and a future manual regression checklist,
not evidence that the implementation compiles or passes runtime checks.

## Neutral codec fixtures

- `ProfilerLiveDiagnosticsTests.cpp`: strict envelope/schema, complete memory and
  animation DTO round trips, byte totals despite partial rows, every truncated
  prefix, trailing bytes, malformed booleans/enums, finite values, explicit caps,
  request validation, and simultaneous maximum-size memory/animation/rendering
- Duplicate long asset-name prefixes remain separate through the SHA-256 identity
  codec and pair correctly across A/B; failed identities never merge with siblings
  or the other snapshot and do not display fabricated CPU deltas
- `ProfilerRenderingDiagnosticsTests.cpp`: rendering schema, all three views,
  pass/message bounds and malformed input without modifying the caller's value
- Compile each fixture separately with only the required neutral codec sources;
  do not link or initialize Editor, SceneRuntime or RenderEngine

## Original UI parity

- Memory: capture twice; switch A/B and no comparison; verify four Korean tabs,
  summary tiles, category colors/spacing/legends, complete byte totals, object
  search/sort/comparison, aliased pixels tooltip, region filter/treemap selection,
  and embedded capture memory trend. Retain no more than eight snapshots
- Animation: wait for owner capture, inspect all stage bars, choose an animator,
  observe actual execution order/dependencies/clip/reach/buffer-owner rows and
  pose-storage target-address labels. No remote-address read API exists
- Rendering: Scene/Game/Material Preview selection, matching GPU submission and
  age, runtime labels, shadow cascades, timings/order toggle, relative bars,
  errors/validation messages, and Open RenderPass structure owner-thread request
- Compare labels, icons, padding, fonts, colors, window/tab IDs and DPI scaling
  against the original editor window; record any visual differences

## Lifetime, failure and responsiveness

- Disconnect/terminate the engine: retain the last immutable DTO and memory A/B
  history; display disconnected/stale status and disable engine commands
- Opening a file without a live target reports that live diagnostics are absent;
  no viewer-local profiler/memory/animation/renderer service is initialized
- Reconnect to a new nonce/session or target: invalidate in-flight decode and old
  selections/history. An old result cannot publish into the new target
- Reload the active scene while selecting/capturing: reject mismatched scene IDs,
  future/expired generations and removed animator selections before mutation
- Force a slow transport/decode: at most one in-flight plus latest pending DTO;
  maintain window interaction while byte encoding/decoding occurs on workers
- Flood commands: bounded transport queue, memory/render action rate limits,
  visible request failures, no repeated memory capture while one is pending
- Close during decode or after rejected scheduler admission: callbacks own only
  immutable bytes/results and canceled operations cannot publish stale data
- Exercise lists above all advertised caps and overlong UTF-8 labels: truncation
  warnings/counts remain truthful; summary byte totals and category bars stay exact
  (3,840 objects, 8,192 regions, 512 animators and 512 tasks per bounded publication)
- Force the BCrypt provider/hash to fail during a memory capture: object rows are
  still visible, identity failure is disclosed, and only those A/B deltas are unavailable
- Validate PID/creation-time/nonce/connection-generation mismatch and malformed
  packet rejection using the transport checklist before live command acceptance
