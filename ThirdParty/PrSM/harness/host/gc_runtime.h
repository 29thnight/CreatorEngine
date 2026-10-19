#pragma once
#include "floating_point_scope.h"

#include "prsm_abi.h"
#include <gc/domain.hpp>

#include <cstddef>
#include <memory>

namespace prsm::host
{
// All methods, including diagnostics and root release, belong to the thread
// that constructed this runtime. GC storage and its metadata are host-owned;
// script callback pointers remain borrowed until DrainModule succeeds.
class CE_API GcRuntime
{
public:
    static GcRuntime& Instance();
    GcRuntime();
    ~GcRuntime();
    GcRuntime(const GcRuntime&) = delete;
    GcRuntime& operator=(const GcRuntime&) = delete;

    // These never collect. Allocation pressure only requests a later cycle.
    // Invalid descriptors/references and wrong-thread calls throw logic_error;
    // memory exhaustion / the allocation cap return nullptr.
    void* Allocate(const PrsmTypeDesc* desc);
    void* AllocateBytes(std::size_t size);
    void Store(void** slot, void* value);
    void Regray(void* frame);
    CeRoot RootCreate(void* object);
    void RootDestroy(CeRoot root);
    void TraceRef(void* tracer, void* object);

    // Active script calls (including nested host -> script reentry) refuse
    // collection, leaving requests pending. Trace callbacks are not script
    // entry points and may only call TraceRef, never mutate or allocate.
    gc::step_result CollectStep(const gc::step_budget& budget = {});
    gc::collect_result CollectFull();
    void RequestCollection();
    void SetAllocationThreshold(std::size_t bytes);
    void SetAllocationLimit(std::size_t bytes); // zero disables the hard cap
    [[nodiscard]] gc::statistics Stats() const;
    [[nodiscard]] std::size_t ActiveDepth() const;
    [[nodiscard]] bool AtSafePoint() const;
    void SetModuleDispatchEnabled(const PrsmModule& module, bool enabled);
    [[nodiscard]] bool ModuleDispatchEnabled(const PrsmModule& module) const;
    [[nodiscard]] bool Contains(const void* object) const;
    [[nodiscard]] bool MatchesActiveDescriptor(const void* object, const PrsmTypeDesc* type) const;
    [[nodiscard]] std::size_t LiveObjects(const PrsmModule* module = nullptr) const;

    // Register after ABI validation, before invoking module code. The global
    // trace hook is represented by a genuine GCCE root. Unregister removes
    // only this root; it does NOT grant permission to unload the library.
    void RegisterModule(const PrsmModule& module);
    void UnregisterModule(const PrsmModule& module);
    // Owners of borrowed module code (for example a scheduler's task hooks)
    // hold a lease even when they currently own no GC frames. Check leases
    // BEFORE invoking on_unload: no unload work may begin while code is held.
    void AcquireModuleLease(const PrsmModule& module);
    void ReleaseModuleLease(const PrsmModule& module);
    [[nodiscard]] bool ModuleRegistered(const PrsmModule& module) const;
    [[nodiscard]] bool HasModuleLeases(const PrsmModule& module) const;
    // First finish lifecycle/cancellation and on_unload while code is loaded,
    // then drop host roots and call DrainModule. False means references still
    // retain module objects: leave the DLL loaded, release them, and retry.
    // Success guarantees no descriptor/global callback can run after unload.
    bool DrainModule(const PrsmModule& module);

private:
    friend class ScriptScope;
    const PrsmModule* EnterScript(const PrsmModule* module, bool trusted_cleanup);
    void LeaveScript(const PrsmModule* previous) noexcept;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Guard EVERY host -> script call, including constructors, lifecycle hooks,
// resume/cancel, predicate and unload hooks. Nested guards retain the module
// owner when omitted, and prevent collection until the outermost returns.
class CE_API ScriptScope
{
public:
    explicit ScriptScope(const PrsmModule* module = nullptr, bool trusted_cleanup = false);
    explicit ScriptScope(GcRuntime& runtime, const PrsmModule* module = nullptr, bool trusted_cleanup = false);
    ~ScriptScope();
    ScriptScope(const ScriptScope&) = delete;
    ScriptScope& operator=(const ScriptScope&) = delete;

private:
    FloatingPointScope floating_point_;
    GcRuntime& runtime_;
    const PrsmModule* previous_;
};
} // namespace prsm::host
