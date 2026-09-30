#pragma once

#include "PrincipledSurface.slang"

// The fixed glTF passes still use the old MRT/DFG bindings. A core graph cannot
// select them until MAT-7 supplies the IOR-aware packing and IBL lookup.
#if (LX_MATERIAL_FEATURE_MASK & 0x0040u) != 0
#error Fixed Standard route cannot bind Principled Specular/IOR; MAT-7 packing and IBL lookup are required.
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0
#error Fixed Standard route cannot bind Layered lobes; MAT-7 routing and per-lobe lookup are required.
#endif
#if (LX_MATERIAL_FEATURE_MASK & 0x3800u) != 0
#error Fixed Standard route cannot bind Special features; an explicit Special Forward consumer is required.
#endif
