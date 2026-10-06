# Engine-owned script binding generation: build integration

The source checkout generates native and managed Light bindings before compilation.
Generated files are intermediate products, not checked-in source. Published engine
consumers continue using the shipped `ScriptCore.dll`; they do not run this pipeline.

## Prerequisites and paths

Use the repository's native build prerequisites: Windows x64, Visual Studio 18 with
the v145 C++ toolchain, the pinned vcpkg dependencies, .NET 10, and PowerShell 7.
Standalone `dotnet build ScriptCore/ScriptCore.csproj` also needs these prerequisites.
The .NET SDK's MSBuild cannot evaluate the native project, so the managed precompile
step locates Visual Studio's x64 MSBuild rather than trying to build a `.vcxproj`
inside SDK MSBuild.

- Native manifest: `Build/Obj/SceneRuntime/<platform>-<configuration>/reflgen/SceneRuntime.declarations.json`
- Shared native fragments and managed sources: `Build/Generated/ScriptBindings/<platform>-<configuration>/`
- `EngineShipping=true` adds `-Shipping` to both configuration keys
- `AnyCPU` managed builds select the engine's x64 platform

`EngineScriptBindingsOutputDirectory` can override the shared output directory.
Both native and managed builds must receive the same override. `ReflgenOutputDirectory`
remains the native reflection output override, and the declaration JSON stays inside it.

## Build graph

`SceneRuntime` owns the declaration manifest and the engine emitter. Its
`ReflgenGenerate` target takes a build-only cross-process file lease, runs the four
upstream reflection stages, runs the engine emitter when needed, records generated
outputs, then releases the lease. `ReflgenGetOwnInjection` uses that same target, so
native projects referencing SceneRuntime's reflection wait for the engine-generated
fragments as well. No engine runtime mutex or lock is added.

`ScriptCore` invokes `Invoke-ScriptBindingGeneration.ps1` before compiling. This calls
Visual Studio MSBuild on `SceneRuntime.vcxproj` with only
`/target:EngineGenerateScriptBindings`. It never requests `Build`, `ClCompile`, or
native project references, so managed generation cannot recurse into the editor's
managed prebuild step. The usual vcpkg prerequisite target can restore dependencies
when normal native-build settings permit it; generation-only does not mean
dependency-restore-free.

The managed launcher uses the active `VSINSTALLDIR` or the official `vswhere.exe`
installation catalogue. Set `EngineScriptBindingsMSBuild` to a specific x64
`MSBuild.exe` when needed. It forwards `Configuration`, `Platform`, `EngineShipping`,
`EngineAsan`, `EngineReflgenTargets`, `ReflgenExecutable`, `ReflgenOutputDirectory`,
`VcpkgInstalledDir`, and `VcpkgTriplet` rather than inheriting managed target-framework properties into the
native project. `EngineScriptBindingsPowerShell` can select the PowerShell 7 executable.

## Concurrency, incremental generation, and clean

The file lease is held across ownership-file reads, reflection generation, and engine
emission. This includes the independent MSBuild process launched by a managed build.
Waiting tasks yield their MSBuild scheduling slot. `OnError` releases the lease if
a generation dependency fails; each evaluated project has a distinct lease owner,
so a failed acquisition cannot release another project's lease. MSBuild also owns
the lease through `RegisteredTaskObjectLifetime.Build` and disposes it at teardown;
process termination closes the file handle. Waits fail after 300 seconds rather
than hanging indefinitely; `EngineScriptBindingsLockTimeoutSeconds` can override it.

The SceneRuntime target intentionally wraps the four upstream stages from pinned
reflgen commit `d04dd64672ff29bc56993e41a3148bd6f939ab76`. When updating that pin,
review the upstream `ReflgenGenerate` stage order along with this wrapper. Acquiring
the lease in `BeforeTargets="_ReflgenComputeInputs"` is too late because its
ownership-read dependency has already run.

The engine emission stamp tracks the manifest, the native and managed ABI sources,
the emitter script, and its MSBuild target file. Every declared output is checked for
existence even on incremental builds; deleting any one regenerates the set. The
emitter writes changed content only. SceneRuntime `Clean` removes exactly the seven
owned generated files and the emission stamp, including files created by a prior
generation-only invocation. It does not remove the output directory or lease file.
As with ordinary compiler outputs, do not run `Clean` concurrently with a build.

Design-time builds do not start native generation. They include any already-generated
managed sources, which become available after the first successful real build.

## Verification status

This integration has been source-reviewed only. No build, dependency restore,
generator execution, test, or analyzer has been run for this change. Required checks
after approval include clean Debug/Release generation, standalone ScriptCore build,
native-first and managed-first ordering, parallel native/managed invocation,
unchanged-input timestamps, single-output deletion recovery, and failure/retry cleanup.
