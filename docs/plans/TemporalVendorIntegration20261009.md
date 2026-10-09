# PHASE 4.5 vendor backend integration — 2026-10-09

## Scope and order

The term backend in this work means the DLSS, FSR and XeSS implementations of temporal upscaling and frame generation. DX12 and Vulkan are graphics APIs below that boundary.

1. Inspect and pin the actual SDK interfaces, feature queries, resource requirements and presentation/lifetime contracts.
2. Define independent, graphics-API-neutral upscaler and frame-generation contracts from those requirements.
3. Implement the vendor adapters and their explicit optional-build/unsupported states. Keep graphics API native types inside implementation boundaries.
4. Connect the required renderer inputs and presenter lifecycle in follow-on coherent slices. An adapter without valid live inputs is not a completed product feature.

Do not add a generic provider framework or global synchronization solely for anticipated use. Reuse the existing RHI, RenderGraph and Player presenter ownership boundaries. Capability depends on the selected SDK, API and runtime query, not vendor ID alone. Upscaler and frame-generation selection remain independent.

## Starting point

Inspected master: `117b729a60acf9663dd6f41fc9b71f4ce6f60e00`.

BASE-0 has existing provenance and mutation checks. Actual LX GBuffer has no velocity output; current transform and skin pose do not supply previous-frame motion. Render/output extents and jitter/history contracts need integration. Player presentation currently transfers a color texture and its host-fence lifetime, not the complete set of temporal inputs through provider final consumption.

Existing native presentation or a successful support query does not prove generated frames, pixel correctness or latency improvement. Editor viewport live_present is not the FG insertion point.

## Verification policy

This work is code and static review only. No builds, compiler or shader execution, tests, native engine runs or benchmarks are authorized in this slice. Separate declared support, adapter implementation, live product wiring and executed validation in every status report.

SDK versions and verified source links, build/package requirements, implemented operations and remaining input/lifetime gaps will be recorded with the implementation. Do not claim unsupported SDKs work, fabricate ABI declarations, silently load arbitrary DLLs, or accept new SDK legal agreements without authorization.

DX12 Debug/Release is the plan's execution acceptance target; graphics-API-neutral contracts remain required. Vulkan runtime parity belongs to PHASE 4.9.
