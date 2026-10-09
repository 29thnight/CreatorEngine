# GCCE integration

Status: staged draft, 2026-10-09. Dependency wiring is available; scene/entity graph
migration and its static review are still in progress. This is not a validated runtime
rollout or a replacement for the engine's deterministic teardown contracts.

## Dependency boundary

- Pin the unmodified [upstream GCCE](../../ThirdParty/GCCE/PROVENANCE.md) source at
  `aef79b3f57861fc16d0045e860e9082eeed8ec29`.
- Build `Engine/GCCE/GCCE.vcxproj` as one C++23 shared runtime, `gcce.dll`.
  Only it defines `GCCE_BUILDING`; all native consumers use `GCCE_SHARED` and
  identical configuration-level `GC_DEBUG_CHECKS` values (`1` Debug, `0` Release).
- `SceneGC.h` is the engine entry header. The include directory permits upstream's
  own `<gc/...>` includes without modifying vendored bytes.
- CreatorEditor and Player link the DLL import library. SceneRuntime has a build-order
  reference; it never incorporates another copy of the runtime implementation.
- GCCE opts out of reflection generation and vcpkg, and uses the matching shared CRT.
  Its headers and exported C++ types require matching toolchain/configuration across
  host modules; this is not a stable cross-version C ABI.

## Runtime packaging

The source DLL is `Bin/<platform>-<configuration>/Runtime/Common/gcce.dll` and the
import library is `Build/Lib/<platform>-<configuration>/gcce.lib`. GCCE is shipping-neutral:
the project locally forces `EngineShipping=false`, including its intermediates and PDB
path, so Editor and Shipping Player share identical bytes. Engine ASan policy still applies.

The existing launcher configures the common runtime search directory before loading
the host. `EngineHost.targets` supplies the exact GCCE path to deployment. Editor and
Player manifests include GCCE and its recursively discovered CRT imports; Shipping
copies the same DLL to its staging tree. Engine/game packaging already copies the
recorded, hashed manifest closure. Do not distribute app-local alternative GCCE copies.

## Graph and lifecycle contracts under review

1. Use a domain whose lifetime exceeds every root, weak reference, and managed object
   in its graph. Root registries/transfer storage must remain outside managed objects.
2. Use `gc::make`, traced strong edges, external roots, and weak references according
   to upstream's protocol. Raw observer pointers alone do not keep objects alive.
3. Keep mutations, root registration, allocation, and collection on the domain owner
   thread. Incremental tracing requires write barriers; changing container ownership
   without tracing/barriers is not a safe migration.
4. Keep deterministic engine destruction, script/physics/audio detachment, and cleanup
   obligations before reclamation. Quarantine reports unfinished cleanup; it does not
   replace that cleanup. Preserve scene transfers, DDOL, undo, and deferred callbacks.
5. Assets continue using `ownership_cpp`; renderer/GPU completion tokens and resource
   quarantine remain authoritative. CPU tracing is not GPU-completion evidence.
6. Drain managed objects and references before a domain dies or code-containing host
   module unloads. Review shutdown and failure/partial-construction paths explicitly.

## Staged work and validation

- Dependency slice: pinned source/hashes, shared runtime project, common definitions,
  solution/reference wiring, deployment closure, and provenance.
- In progress: Scene/Entity/component graph ownership, transfer roots, deterministic
  lifecycle integration, and call-site migration/static review.
- Pending: focused review of trace completeness, root release, exception safety,
  teardown order, and game/script boundary compatibility.
- Deferred until separately authorized: Debug/Release Windows builds and DLL import
  inspection; lifecycle/scene transfer/DDOL/undo/shutdown tests; ASan; packaged
  Editor/Player smoke runs and performance measurements.

Only static checks have been performed for this change. No engine/upstream build,
test, shader compile, NativeAOT publish, engine launch, or benchmark has been run.
Upstream has no supplied LICENSE at this pin; the owner's integration permission is
recorded without asserting a license or broader redistribution grant.
