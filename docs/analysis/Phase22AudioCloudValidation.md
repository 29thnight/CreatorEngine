# Phase 22 cloud audio validation

Date: 2026-10-05 UTC
Implementation branch: `phase22-fmod-retirement-20261005`
Base: `86f7efd30314de4dd19e3dbb30da9f820506e3dd`

## Result

**3,825 / 3,825 assertions passed in each of Debug, Release and ASan + UBSan.** There were no sanitizer findings. The unique assertion count is 3,825, not 11,475 independent scenarios: many assertions deliberately exercise malformed/truncated prefixes.

All production translation units were compiled directly, without source-copy adaptations, decoder/header stubs or a replacement YAML parser. The final tree is identified by the [576-input SHA-256 ledger](../../Tools/regression/audio/evidence/source-sha256.json), not just the pre-implementation base commit.

The verification sequence matters:

1. Clean output directories, no object reuse: all ten suites passed 3,817 assertions per configuration
2. A final UTF-8 filename fix changed only `SoundAssetCookProducer.h` and its dedicated probe
3. GCC `-MM`, using each of the 26 original object compile commands in each configuration, established that only `sound_cook_contract_probe.o` consumed either changed input
4. That object was rebuilt, relinked and rerun in all three configurations; its count rose from 30 to 38. The other 25 objects per configuration retain their verified clean-build identity

The [final results](../../Tools/regression/audio/evidence/results.json), [compile provenance](../../Tools/regression/audio/evidence/compile-provenance.json) and [targeted dependency proof](../../Tools/regression/audio/evidence/targeted-recheck.json) record this distinction. No unaffected binary is described as newly rebuilt after the final change.

## Coverage

| Suite | Assertions per configuration | Principal contracts |
|---|---:|---|
| Core | 133 | Generation exhaustion/stale handles, scopes, owners, per-play controls, graph compilation/evaluation, queued results/backpressure, voice policy, virtualization and failure retirement |
| Decoder | 11 | Actual WAV/MP3/FLAC decoder and PCM/metadata/hash checks |
| Offline render | 34 | Actual miniaudio mixing, gain, pause, completion, preset one-shot, reverb, blend, distance and handedness |
| Software device | 62 | Actual miniaudio software Null device, scheduling, malformed input, loops, diagnostics, restart and shutdown |
| CEAC/CEMF | 670 | SHA-256, bounded reads, tamper/overflow/path rejection, transactional parsing, ordering and truncated prefixes |
| Cooked playback | 114 | Resident and streaming codecs, seeks/loops/pause, ownership, hot replacement, virtual rehydration, asynchronous I/O failure and shutdown |
| Cook decoder | 20 | Valid, corrupt, oversized and tail-truncated WAV/MP3/FLAC |
| Cold device | 10 | Zero eligible device backends, retained silent graph, natural completion, retries and stable degraded Host identity |
| Sound assets | 2,733 | Typed YAML/CEDO, deterministic bytes, no runtime text parsing, cache immutability/no-clobber/path safety, GUID catalog, collisions and transactional stereo edits |
| Sound cook | 38 | Scoped scene/prefab migration, dependencies, unknown/malformed data, size limits and Korean/emoji aliases |

Bounded lifetime checks include 1,000 per-play recycles, 128 known-length logical Null one-shots beyond fixed capacity, 100 failed-device Host cycles, 20 software-device restarts preserving the same looping handle, four concurrent command producers and a 1,024-command/result bound. This is not a 30-minute hardware soak.

Three intentionally mutated source copies were compiled and detected by assertion failures with exit code 1: stale generation (three failed assertions), missing-clip guard (one) and graph voice bound (one). Production files remained unchanged. Compiler errors, crashes and timeouts would not count as detection. See [negative canaries](../../Tools/regression/audio/evidence/failure-canaries.json).

All 25 generated fixture files were byte-identical across two fresh generations: [fixture hashes](../../Tools/regression/audio/evidence/fixture-determinism.json).

## Measured PCM observations

These are captured float PCM from explicit no-device rendering, not measurements from speakers or hardware loopback.

- Near-source 2D and 3D energies: 23.019774 each; blend midpoint: 46.039544, matching correlated cosine/sine branches
- Beyond maximum distance: 3D energy 0; 2D energy remains 23.019774; midpoint energy 11.509886
- Engine +X source: right 20.462020, left 0.818481; -X reverses these; the rotated-listener case also maps correctly
- Dry impulse tail energy after the clip: 0; Hall reverb tail: 0.186364799
- Pause leaves two cached frames in the first block, below the asserted 256-frame bound; the following block is silent and its playhead remains fixed

## Problems the gate exposed

The frozen reruns verify fixes for offline startup, premature scheduled-voice retirement, full source-tail validation/diagnostic filenames, miniaudio attenuation mode bypassing branch gain, handedness, nonfinite cooked preset settings, invalid live stereo edits and backend failures being mistaken for permissible virtualization.

Several apparent failures instead clarified contracts: resident playback owns decoded PCM and need not retain encoded bytes; a pause may drain already mixed frames; invalid in-progress authoring reports failure while retaining the last good graph. Those assertions were corrected explicitly.

Early builds overlapped source edits, and one such build crashed. Those results were discarded as inconclusive. Before/after source checks reject drift. A subsequently discovered cache-provenance weakness was repaired before the clean certification builds: optional reuse now requires a successful per-object manifest with source/header/external-header/compiler/command/object hashes. A top-level ledger from an aborted build cannot authorize reuse. Fixture generation is also a checked subprocess.

## Reproduction

See the [portable gate README](../../Tools/regression/audio/README.md) for complete commands and pinned dependency setup. The toolchain was Linux x86_64, GCC 14.2.0, C++23. Runtime Debug uses `-O0 -g`, Release `-O2 -DNDEBUG`, and sanitizer `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer`. Authoring/dependency TUs additionally use `-DNDEBUG` to match their dependency build.

Official test-only dependency pins match vcpkg baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`: rapidyaml 0.16.0 (`f8ac8dd50f4f7916579d55a05ebf9c6488e52670`) and c4core 0.6.0 (`1885dca396de5aea84f176eccb92a00c8b85c5ff`). Both libraries were instrumented in the sanitizer run.

The final generated logs are under `Build/Validation/Phase22Final/<configuration>/`. Evidence is persisted in the repository; generated binaries and full working directories are not.

## Explicit exclusions

This evidence does **not** certify physical output, WASAPI, listening/loopback, hardware latency or underruns, a 30-minute hardware soak, the Windows MSVC wrapper, full Editor/Player builds, package/PE checks, native Windows cache/CNG paths, real Pak mounting/encryption/immutability, or C# product execution. The non-ASCII alias tests run on Linux; the Windows encoding fix still needs its native platform gate.

Logical Null backend tests, miniaudio software-device tests and explicit no-device PCM tests are separate boundaries. A separate scene-consumer probe uses Scene/Entity/logger stubs and is not product scene integration. LeakSanitizer is disabled under this host's ptrace environment; leak freedom is not verified. These exclusions remain Phase 22 acceptance work rather than inferred passes.
