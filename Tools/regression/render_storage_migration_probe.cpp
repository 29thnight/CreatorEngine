#include "../../Engine/RenderEngine/Render/Graph/EnhancedGpuMeasurementHistory.h"
#include "../../Engine/RenderEngine/Render/Graph/EnhancedDrawSealLedger.h"
#include <cstdio>
#include <stdexcept>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace {
unsigned checks = 0;
void require(bool value, const char* name) {
    ++checks;
    if (!value) throw std::runtime_error(name);
}
GpuFrameToken token(uint64_t submission, uint64_t view, uint64_t tick) {
    GpuFrameToken t{};
    t.ringSlot = 0; t.submissionId = submission; t.renderViewId = view;
    t.engineFrameId = submission; t.cpuSubmitTick = tick;
    return t;
}
#if defined(_DEBUG)
bool tracking = false;
unsigned allocations = 0, frees = 0;
int allocation_hook(int kind, void*, size_t, int, long, const unsigned char*, int) {
    if (tracking) {
        if (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC) ++allocations;
        if (kind == _HOOK_FREE) ++frees;
    }
    return TRUE;
}
struct allocation_scope {
    _CRT_ALLOC_HOOK old;
    allocation_scope() : old(_CrtSetAllocHook(allocation_hook)) {
        allocations = frees = 0; tracking = true;
    }
    ~allocation_scope() { tracking = false; _CrtSetAllocHook(old); }
};
#endif
void history_contract() {
    EnhancedGpuMeasurementHistory history;
    auto signature = own::make_shared<const std::string>("retained-signature");
    own::weak_owner<const std::string> weak(signature);
    require(history.Store(RGMeasurementDomain::Normal,
        {signature, {uint64_t{0}, std::nullopt}, token(1, 1, 100)}), "initial sample");
    auto* address = history.Find(RGMeasurementDomain::Normal, 1);
    signature.reset();
    require(!weak.expired(), "history owns signature after caller reset");
    for (uint64_t view = 2; view <= 8; ++view)
        require(history.Store(RGMeasurementDomain::Normal,
            {own::make_shared<const std::string>("other"), {view}, token(view, view, 100 + view)}), "grow index");
    require(history.Find(RGMeasurementDomain::Normal, 1) == address &&
        address->nanoseconds[0] == 0 && !address->nanoseconds[1], "node address and measured zero survive rehash");
    const auto current = token(9, 1, 120);
    const auto owner = address->graphSignature;
    auto snapshot = *address;
    require(history.FindFresh(RGMeasurementDomain::Normal, current, 1000, owner) == address,
        "fresh exact signature lookup");
    require(!history.Store(RGMeasurementDomain::Normal,
        {own::make_shared<const std::string>("late"), {uint64_t{99}}, token(1, 1, 99)}) &&
        history.Find(RGMeasurementDomain::Normal, 1) == address, "late sample preserves node and value");
#if defined(_DEBUG)
    bool warm = true;
    unsigned observed = 0;
    {
        allocation_scope scope;
        for (unsigned i = 0; i < 1000; ++i) {
            warm &= history.Find(RGMeasurementDomain::Normal, 1) == address;
            warm &= history.FindFresh(RGMeasurementDomain::Normal, current, 1000, owner) == address;
            warm &= history.Find(RGMeasurementDomain::Capture, 1) == nullptr;
            warm &= history.Find(RGMeasurementDomain::Normal, 1000) == nullptr;
        }
        observed = allocations;
    }
    require(warm && observed == 0, "history warmed hit/miss/fresh queries allocate zero");
#endif
    history.Clear();
    require(!history.Find(RGMeasurementDomain::Normal, 1) &&
        snapshot.graphSignature && snapshot.nanoseconds[0] == 0, "copied snapshot owns data after clear");
    require(!weak.expired(), "external retained owner survives clear");
    snapshot.graphSignature.reset();
    require(owner.use_count() == 1, "clear and snapshot reset release their strong owners");
    // owner deliberately retains the signature until this scope ends.
}
void history_retirement() {
    EnhancedGpuMeasurementHistory history;
    own::weak_owner<const std::string> weak;
    {
        auto owner = own::make_shared<const std::string>("evicted");
        weak = own::weak_owner<const std::string>(owner);
        require(history.Store(RGMeasurementDomain::Capture,
            {owner, {uint64_t{1}}, token(1, 1, 100)}), "eviction owner admitted");
    }
    for (uint64_t view = 2; view <= 9; ++view)
        require(history.Store(RGMeasurementDomain::Capture,
            {own::make_shared<const std::string>("live"), {view}, token(view, view, 100 + view)}), "bounded views");
    require(!history.Find(RGMeasurementDomain::Capture, 1) && weak.expired(), "oldest view immediately releases owner");
    auto owner = own::make_shared<const std::string>("variant");
    weak = own::weak_owner<const std::string>(owner);
    require(history.Store(RGMeasurementDomain::Normal,
        {owner, {uint64_t{4}}, token(20, 4, 200)}), "normal admitted");
    owner.reset();
    history.Discard(RGMeasurementDomain::Normal, token(22, 4, 202));
    require(weak.expired() && !history.Find(RGMeasurementDomain::Normal, 4)->graphSignature,
        "discard destroys owner and retains watermark");
    require(!history.Store(RGMeasurementDomain::Normal,
        {own::make_shared<const std::string>("late"), {uint64_t{4}}, token(21, 4, 201)}), "late result cannot resurrect discarded sample");
    require(history.Find(RGMeasurementDomain::Capture, 9) != nullptr, "discard is domain local");
    EnhancedGpuMeasurementHistory tied;
    const auto tiedSignature = own::make_shared<const std::string>("tied-oldest");
    for (uint64_t view = 1; view <= 8; ++view)
        require(tied.Store(RGMeasurementDomain::Normal,
            {tiedSignature, {view}, token(view, view, view <= 2 ? 100 : 100 + view)}), "tied view admitted");
    require(tied.Store(RGMeasurementDomain::Normal,
        {tiedSignature, {uint64_t{9}}, token(9, 9, 200)}), "tied view eviction");
    bool newerSurvive = true;
    for (uint64_t view = 3; view <= 9; ++view)
        newerSurvive &= tied.Find(RGMeasurementDomain::Normal, view) != nullptr;
    require(newerSurvive && ((tied.Find(RGMeasurementDomain::Normal, 1) == nullptr) !=
        (tied.Find(RGMeasurementDomain::Normal, 2) == nullptr)), "eviction removes one minimal timestamp; ties have no iteration-order contract");
}
void ledger_contract() {
    EnhancedDrawSealLedger ledger;
    ledger.Begin(10, 2);
    const EnhancedDrawSealLedger::Binding initial{1, 2, 3, 4};
    require(ledger.Observe(1, initial), "first binding");
    auto* address = &ledger.bindings.find(uint64_t{1})->second;
    for (uint64_t i = 2; i < 40; ++i) require(ledger.Observe(i, initial), "grow binding index");
    require(&ledger.bindings.find(uint64_t{1})->second == address && *address == initial,
        "binding reference survives rehash");
    auto differentVersion = initial; differentVersion.descriptorVersion = 100;
    require(ledger.Observe(1, differentVersion), "descriptor version remains excluded from identity");
#if defined(_DEBUG)
    bool warm = true; unsigned observed = 0, erased = 0;
    {
        allocation_scope scope;
        for (unsigned i = 0; i < 1000; ++i) warm &= ledger.Observe(1, initial);
        observed = allocations;
    }
    require(warm && observed == 0, "ledger repeated binding allocates zero");
    {
        allocation_scope scope;
        ledger.bindings.erase(uint64_t{2});
        erased = frees;
    }
    require(erased == 1, "erase immediately frees one node without pool retention");
#endif
    auto conflict = initial; ++conflict.pipelineId;
    require(!ledger.Observe(1, conflict) && ledger.counters.pipelineConflict == 1, "pipeline conflict preserved");
    conflict = initial; ++conflict.textureDigest;
    require(!ledger.Observe(1, conflict) && ledger.counters.bindingConflict == 1, "binding conflict preserved");
    const auto oldSize = ledger.bindings.size();
#if defined(_DEBUG)
    unsigned cleared = 0;
    {
        allocation_scope scope;
        ledger.Begin(11, 3);
        cleared = frees;
    }
    require(cleared == oldSize, "frame reset immediately frees every node and retains only index capacity");
    unsigned inserted = 0;
    bool admitted = false;
    {
        allocation_scope scope;
        admitted = ledger.Observe(1, conflict);
        inserted = allocations;
    }
    require(admitted && inserted == 1, "next frame reuses index and allocates only its new node");
#else
    (void)oldSize;
    ledger.Begin(11, 3);
    const bool admitted = ledger.Observe(1, conflict);
#endif
    require(ledger.bindings.size() == 1 && ledger.Violations() == 0 && admitted,
        "frame reset releases bindings and resets conflict state");
}
}
int main() {
    try {
        history_contract(); history_retirement(); ledger_contract();
        std::printf("RENDER_STORAGE_MIGRATION_PASS checks=%u allocation_checks=%u\n", checks,
#if defined(_DEBUG)
            1u
#else
            0u
#endif
        );
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "RENDER_STORAGE_MIGRATION_FAIL %s\n", e.what()); return 1;
    }
}
