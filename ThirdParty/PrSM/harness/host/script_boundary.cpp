#include "script_boundary.h"
#include "gc_runtime.h"
namespace prsm::host {
bool InvokeScript(void (*thunk)(void*), void* context, const PrsmModule* module, bool trusted_cleanup)
{
    ScriptScope active(module, trusted_cleanup);
    thunk(context);
    return true;
}
}
