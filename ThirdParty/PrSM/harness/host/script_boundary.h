#pragma once
#include "prsm_abi.h"
namespace prsm::host {
// Active-stack guard, NOT an exception/panic recovery mechanism. Status-return
// fixtures must propagate failure themselves. Legacy ce_panic remains fatal.
CE_API bool InvokeScript(void (*thunk)(void*), void* context,
                         const PrsmModule* module = nullptr, bool trusted_cleanup = false);
}
