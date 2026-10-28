#pragma once

// The engine and its hosts share one compiled GCCE runtime. Assets use
// Ownership.h independently of this managed-object contract.
#include "../../../ThirdParty/GCCE/include/gc/gc.hpp"

#if defined(_MSC_VER)
#if GC_DEBUG_CHECKS
#pragma detect_mismatch("GCCE_DEBUG_CHECKS", "1")
#else
#pragma detect_mismatch("GCCE_DEBUG_CHECKS", "0")
#endif
#endif
