#pragma once

#include "PrincipledSurface.slang"

// Numeric execution-route contract, consumed by MAT-6/MAT-7 generation.
// 1 = Deferred, 2 = Forward. This does not select a product pass by itself.
#if (LX_MATERIAL_FEATURE_MASK & 0x3800u) != 0
#ifndef LX_MATERIAL_ROUTE
#error Special material requires an explicit Forward route (LX_MATERIAL_ROUTE=2).
#elif LX_MATERIAL_ROUTE != 2
#error Special material cannot execute in Deferred; Forward transport resources are required.
#endif
#endif
