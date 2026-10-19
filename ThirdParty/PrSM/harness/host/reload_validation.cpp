#include "reload_validation.h"
#include "gc_runtime.h"
#include "prsm_reload_bridge.h"

#include <utility>

// Generated component modules import these checked queries. They depend only
// on the GC runtime, so a host without a reload coordinator still provides them.
namespace {
thread_local void* restore_object = nullptr;
}

namespace prsm::host {
RestorePrivilege::RestorePrivilege(void* object) noexcept : previous_(std::exchange(restore_object, object)) {}
RestorePrivilege::~RestorePrivilege() { restore_object = previous_; }
}

extern "C" CE_API PrsmCallStatus prsm_host_reload_validate_object(void* object, const PrsmTypeDesc* type)
{
    try { return prsm::host::GcRuntime::Instance().MatchesActiveDescriptor(object, type) ? PRSM_CALL_OK : PRSM_CALL_FAILED; }
    catch (...) { return PRSM_CALL_FAILED; }
}
extern "C" CE_API PrsmCallStatus prsm_host_reload_validate_restore(void* object, const PrsmTypeDesc* type)
{
    return object && restore_object == object ? prsm_host_reload_validate_object(object, type) : PRSM_CALL_FAILED;
}
