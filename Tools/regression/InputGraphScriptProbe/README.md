# InputGraph script boundary fixture

This source-only fixture links the production typed input surface and explicit native POD mirrors. It covers layout/offset checks, press-release multiplicity, typed lookup, rebind-stable interface identity, stale accessors, duplicate batches, immediate unsubscribe, deferred subscribe, per-handler exception isolation, object/session/reload invalidation and owned snapshot retention.

`Native` is a deterministic test double, not a substitute for the engine's ClrHost or hardware adapter. The project opts into trimmed NativeAOT publication. Neither this fixture, NativeAOT publication, nor the real native/C# integration has been built or executed during this change. Actual Windows x64 NativeAOT and host integration remain acceptance gates.
