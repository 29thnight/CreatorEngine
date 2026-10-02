# Phase19 P3 v1 capture fixture

`profile-v1.ceprof` is a 1,028-byte, format-version-1 capture with one original
38-byte event. It was reduced from the Phase19 P2 Debug capture recorded before
the CPU ownership schema change, retaining its environment, marker and thread
chunks and the first non-empty frame's first event. Optional counter chunks were
removed; offsets and IEEE CRC-32 were recomputed. It contains no CPU ID payload.

`profile_core_probe.cpp::test_legacy_capture` loads this checked-in fixture and
requires all three new CPU ownership fields to be zero. It is independent of
the current writer, so a v2 writer and v2 reader cannot mask a v1 read regression.

`profile-counter-v1.ceprof` reuses the frozen legacy event/thread chunks above with an independently constructed counter chunk v1: RAM (id 2), value 512, and no CPU ownership bytes. The file envelope is v2; chunk versions decide decoding. This synthetic compatibility sample tests the old 10-byte counter wire layout, not a historical RAM measurement.
