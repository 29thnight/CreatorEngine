# Engine-owned script bindings, Light migration

`Generate-ScriptBindings.ps1` is an offline PowerShell 7 consumer of the generic
`reflgen.declarations` version-1 manifest. It adds no runtime or tool project.
The SceneRuntime build owns invocation and serializes generation; do not invoke
the producer concurrently against one output directory.

```powershell
pwsh -NoProfile -File Tools/ScriptBindings/Generate-ScriptBindings.ps1 `
    -Declarations <SceneRuntime-declarations.json> `
    -NativeSource Engine/SceneRuntime/ClrHost.cpp `
    -ManagedSource ScriptCore/Native.cs `
    -OutputDirectory <configuration-specific-shared-directory>
```

This command is documentation, not evidence that generation or a build has run.
The initial implementation was prepared under a source-only restriction.

## Source of truth

The reflected C++ `LightComponent` carries `creator::script_component("Light")`.
Its reflected, inspector-hidden methods carry
`creator::script_export("Property", relativeSlot)`. The metadata's return and
parameter type records are the only method-signature source. The generator does
not recover component signatures from C++ text or maintain a second function
schema. The slot annotation owns ordering; slot 0 is the synthetic `Exists`, and
the migration allows six getter/setter pairs at slots 1 through 12.

The deliberately narrow engine policy permits:

- Public, non-static, non-overloaded, non-variadic, non-deleted methods with known
  qualifiers, no volatile qualifier and no reference qualifier
- Zero-argument getters, including ordinary `const` getters; non-const setters
  with one argument and `void` return
- `float`, by-value `math::color` or a const lvalue reference to it, `LightType`
  and `LightStatus`, with matching getter/setter value adapters
- `math::color` copied through `Float4`/`Color4`, never a borrowed native pointer
- The two native 16-bit enums copied through 32-bit ABI integers and typed public
  managed properties, with out-of-range writes rejected using native constants

Unknown `creator::script_*` annotations, unsupported owners, duplicate slots,
missing pairs, changed declaration versions, unknown types or qualifiers, and
non-x64 Windows MSVC targets fail before any output is written. Metadata not used
by this policy may gain compatible fields without rejection. Reflgen's optional
generic interop pass is not used by this engine consumer.

## Products and inclusion contract

| File | Consumer |
| --- | --- |
| `ScriptLightApi.g.h` | `ClrHost.cpp`, inside its anonymous namespace after boundary POD declarations; provides nested table, layout assertions, fingerprint |
| `ScriptLightApi.Thunks.g.inc` | Same namespace, after `ResolveScriptComponent<T>` and `ReportScriptBindingException` definitions |
| `ScriptLightApi.Fill.g.inc` | Native table initialization, assigns the 13 nested function pointers |
| `ScriptLightApi.g.cs` | Sequential managed nested table and `ScriptBindingsContract` |
| `Native.Light.g.cs` | Partial `Native`, preserving its existing helper names and `Entered()` plus null-pointer guards |
| `LightComponent.g.cs` | Partial public `LightComponent`, typed properties using the existing `OwnerHandle` |
| `ScriptBindings.contract.json` | Reviewable full ordered ABI slot list, adapter selections, layout scope and fingerprint input |

The native translation unit supplies `<cstddef>`, `<cstdint>`, `<type_traits>`,
the component and POD declarations, and the API version constant before the
generated header. The exception reporter is `noexcept`; a generated assertion
enforces that requirement. Every generated thunk is also `noexcept`, catches
exceptions and resolves the generational owner handle again for that call.
Setters invoke the declared component writer, preserving its dirty publication.
Missing owners/components and exceptions produce the existing fallbacks: white
color, zero numeric/enum values, false existence, and no-op writes.

The handwritten `ScriptApiTable` on each side contains one `ScriptLightApi Light`
field at the previous Light block position. ABI version 34 appends a 64-bit
fingerprint after the existing 187 function slots. Native and managed binding
must compare version, size and fingerprint before publishing the table. Managed
`ScriptBindingsContract.ValidateLayout()` is a startup-only boolean check of
actual unmanaged stack-local storage, field addresses and handle/color sentinel
values, plus inferred generic primitive-type checks. It does not ask the runtime marshaler to interpret function-pointer
fields, and no local pointer escapes. No property uses reflection dispatch.

## What the fingerprint proves

The producer expands the nested block and reads every remaining manual native
and managed function-pointer declaration. It compares all 187 ordered names,
return types, parameter types and the Win64 calling-convention contract. An
explicit type-level alias map lowers existing spellings such as
`ScriptObjectHandle`/`ObjectHandle`, `Float4`/`Color4`/`Quaternion`, UTF-8 pointers,
and existing physics/audio ABI record names. It is not a per-slot function list.
Unqualified managed `unmanaged` and explicit `Stdcall` are equivalent only on the
supported Windows x64 target; other explicit conventions are rejected.

Canonical UTF-8 LF-terminated text records these slots, Light adapter choices,
and reviewed sizes, field offsets and enum mappings for:

- `ScriptObjectHandle`/`ObjectHandle`
- `Float4`/`Color4`, plus the copy-only `math::color` adapter
- Native and managed Light enum widths and enumerator values
- The nested Light table and outer table header, slot area and fingerprint tail

SHA-256 hashes that text; the first 64 digest bits, interpreted big-endian, form
the native/managed fingerprint constants. The JSON also records the complete
SHA-256 and canonical input. This detects accidental mismatched builds; it is
not an authentication mechanism.

Separate `input_hashes` record SHA-256 of the exact declaration-manifest, native
table, managed table and generator file bytes. `output_hashes` record the six
generated source files. These freshness hashes do not enter the ABI fingerprint,
and the contract JSON does not recursively hash itself.

Legacy physics, audio, vector and other POD **field layouts are not migrated or
proven by this fingerprint**. Their symbolic types and function signatures are
compared, while their existing layout contracts remain separately owned. A
future migration must explicitly add reviewed layout descriptors/assertions.
There is no new runtime reflection dispatcher or new AOT backend in this slice.

## Determinism and failure behavior

All inputs are validated and all seven outputs are rendered in memory before
creating the output directory. Outputs cannot replace the supplied inputs,
generator, or manifest-listed source headers. Files use UTF-8 without BOM and
LF newlines. Unchanged content is not rewritten. Changed files are written to
same-directory temporary files and atomically replaced. The build-level producer
lock and target dependency order prevent consumers from reading a partly
updated set; replacement is per-file, not a seven-file transaction. On failure,
consumers must not continue with stale products.

## Source-only review cases for later authorized validation

No fixture or command here has been executed as part of source implementation.
`Fixtures/LightMigrationBaseline.json` records the previous API's ordered names,
typed properties and offsets for later acceptance checks. It is not read by the
producer and does not supply production method signatures.
When execution is authorized, exercise normal generation and no-change timestamps
first. Cases 1 through 6 must fail before changing existing outputs; cases 7 and 8
exercise runtime rejection and preserved behavior:

1. Version/module/target mismatch and unknown `creator::script_*` annotation
2. Duplicate/missing slot, mismatched property names or value adapters, or a
   component export on a field/unselected owner
3. Private/static/overloaded/volatile/ref-qualified/variadic/deleted method,
   unknown qualifiers, extra getter argument, or non-void setter
4. Mutable color reference, pointer, unknown record, or changed scalar/enum size
5. A single manual native/managed slot name, parameter, return type, or convention
   mismatch, including entries after the nested Light block
6. Missing, repeated or moved nested block; added function slot; wrong header or
   fingerprint tail
7. Runtime binding with stale fingerprint, bad nested/POD offsets or enum values
8. Destroyed/reused owner handle, missing Light component, off-thread access,
   enum bounds rejection, thrown native method and retained setter dirty behavior
