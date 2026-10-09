#pragma once

// The engine and its hosts share the compiled GCCE runtime. This is the only
// engine-facing include; assets continue to use Ownership.h independently.
#include "../../ThirdParty/GCCE/include/gc/gc.hpp"

#if defined(_MSC_VER)
#if GC_DEBUG_CHECKS
#pragma detect_mismatch("GCCE_DEBUG_CHECKS", "1")
#else
#pragma detect_mismatch("GCCE_DEBUG_CHECKS", "0")
#endif
#endif
