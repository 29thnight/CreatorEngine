# Phase 22 exact-source audio regression

## Reproduce the complete portable gate

Prerequisites: Python 3.9+, Git, `ar`, and GCC 14 with C++23 support. The language level matches production's `std::span`, `std::variant`, defaulted comparisons and `std::lerp`; GCC 14 does not mean C++14. No downloaded audio assets, physical device, CMake or engine unity libraries are required.

Build the official pinned test-only YAML dependencies, then run all suites:

```sh
python3 Tools/regression/audio/build_test_dependencies.py --prefix Build/Validation/Phase22Deps --config release
python3 Tools/regression/audio/run_portable.py --config debug --ryml-prefix Build/Validation/Phase22Deps
python3 Tools/regression/audio/run_portable.py --config release --ryml-prefix Build/Validation/Phase22Deps
python3 Tools/regression/audio/build_test_dependencies.py --prefix Build/Validation/Phase22DepsSanitize --config sanitize
python3 Tools/regression/audio/run_portable.py --config sanitize --ryml-prefix Build/Validation/Phase22DepsSanitize
python3 Tools/regression/audio/run_failure_canaries.py
```

The sanitizer dependency build instruments rapidyaml/c4core as well as engine code. A simple `--config all --ryml-prefix Build/Validation/Phase22Deps` also runs all configurations, but that single unsanitized dependency archive leaves third-party library internals outside instrumentation. Neither command implies a Windows product or hardware pass.

Pinned official sources, matching vcpkg baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`:

- `biojppm/rapidyaml` tag `v0.16.0`, commit `f8ac8dd50f4f7916579d55a05ebf9c6488e52670`
- `biojppm/c4core` tag `v0.6.0`, commit `1885dca396de5aea84f176eccb92a00c8b85c5ff`

The setup helper verifies clean exact revisions before compilation, installs test headers into the requested prefix, and writes `dependency-pins.json`. `--rapidyaml-source` and `--c4core-source` accept existing clean pinned clones. Production dependency/project files are not changed. The vcpkg-style `ryml/` include aliases are tiny wrappers around unchanged upstream headers.

The runner refuses a full run without `--ryml-prefix`. `--core-only` explicitly selects limited core/decoder/render/software-device coverage; omitted asset tests are not reported as passing. `--output`, `--cxx`, `--config`, `--suites`, `--timeout` and `--build-only` support CI control.

## What actually runs

Production translation units and headers are compiled directly. There are no source-copy portability adaptations, replacement YAML parsers, decoder stubs, vendor-header stubs or replacement Pak implementations. WAV, MP3 and FLAC files are deterministic generated fixtures retained from commit `86f7efd30314de4dd19e3dbb30da9f820506e3dd`.

- `core`: VoiceTable, logical Null backend, AudioRuntime/AudioHost, typed SoundGraph, per-play service, owner/scope generation, command and completion queues, limits/steal/virtualization, and bounded lifetime soak
- `decode`: actual miniaudio decoder, exact PCM sample values and source metadata/hashes
- `render`: actual miniaudio decoder/engine/DSP/mixer with explicit no-device rendering. PCM assertions cover gain, pause, completion, preset one-shot override, blend, distance attenuation, handedness and reverb
- `software-device`: miniaudio software Null device, callback scheduling, decode/loop completion, malformed sources, diagnostic filenames, restart recovery and shutdown
- `cooked-contract`: exact CEAC/CEMF hash, path, overflow, tamper, prefix and transactional contracts, including real loose-file symlink checks
- `cooked-playback`: real resident and VFS-backed streaming decoders for three codecs; seeks, loops, pauses, mounted-source ownership, hot replacement/virtualization, read failure and shutdown
- `cook-decoder`: exact AssetCooker decoder validation across valid, corrupt, truncated and oversized sources
- `sound-assets`: actual rapidyaml + CEDO serialization, typed values, deterministic binary roundtrip, zero runtime text parses, import cache immutability/no-clobber/path safety, GUID catalog and legacy collision diagnostics
- `sound-cook`: production graph/preset dependency cooking and scoped scene/prefab migration; unrelated data remains untouched
- `run_failure_canaries.py`: isolated negative mutants must compile, then fail assertions with exit 1. A compiler failure, crash or timeout does not count as successful detection

The Windows MSVC entry point `verify-audio-voice-contract.ps1` compiles the core probe sources directly without the retired middleware. It is a limited core/decoder/render/software-device wrapper; it does not run the portable YAML/cooker auxiliaries, and was not executed in the Linux validation environment.

## Evidence and boundaries

Outputs live under ignored `Build/Validation/Phase22Portable`: source SHA-256 ledger, per-TU compile logs, per-suite logs, generated fixtures and JSON results. Both before/after compile and after-run source checks reject concurrent edits rather than certify a mixed build. Only fixtures beneath explicit generated work roots are removed or mutated.

Debug, Release and ASan+UBSan repeat the same assertions. Counts describe assertions and malformed-prefix checks, not independent end-to-end scenarios; configurations do not multiply unique coverage. ASan and UBSan are enabled; LeakSanitizer is disabled because this execution host uses ptrace, so leak freedom is not certified.

The engine-owned logical Null backend is a silent stateful fake. Miniaudio's software Null device is a separate real engine/decoder/mixer with simulated output timing. No-device PCM capture has no output device at all. These three test boundaries are reported separately.

No physical output/WASAPI/listening/loopback, hardware latency/underrun, 30-minute hardware soak, Windows package/PE scan, real Pak mount/encryption, native Windows import cache branch, full Editor/Player build or C# product execution is certified by this gate. A separate stubbed scene-consumer probe, if run, is not a substitute for product integration. The Phase 22 plan records those remaining gates.

### Interrupted-build safety

By default every object is compiled afresh. Optional `--resume` accepts an object only when a per-object provenance file from a successful compilation matches its SHA-256, exact compiler command and compiler binary, source SHA-256, complete repository header/dependency ledger and external dependency headers. Input hashes are checked before and after compilation; provenance is published atomically only afterward. An aborted build's top-level source ledger can never by itself authorize object reuse. Final checked-in evidence was produced from clean output directories without `--resume`.

Fixture generation is a checked subprocess using the same sanitizer environment as its suite; any failure aborts that configuration. To independently check fixture reproducibility:

```sh
python3 Tools/regression/audio/verify_fixture_determinism.py
```

The `cold-device` auxiliary compiles the unchanged miniaudio backend with zero eligible output backends and checks real initialization failure, a retained silent graph, natural completion, bounded retry, stable Host/service/voice identities and shutdown. It is an injected configuration failure, not a hardware-disconnect test.

The persisted final report includes a clean three-configuration baseline followed by a narrowly scoped UTF-8 cook change. `recheck_sound_cook.py` records the original-compiler `-MM` dependency proof and rebuilds only the dedicated cook probe for that historical two-file delta. It deliberately refuses any different change set; it is not a general substitute for a clean gate. The checked-in report clearly distinguishes those targeted reruns from unchanged clean-baseline results.
