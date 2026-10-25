#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

// The engine uses GameInput v3 from its vcpkg dependency, not the older SDK copy.
#if defined(CE_GAMEINPUT_HEADER)
#include CE_GAMEINPUT_HEADER
#else
#include <GameInput.h>
#endif
#if GAMEINPUT_API_VERSION != 3
#error "Creator requires the GameInput v3 header from its configured vcpkg dependency."
#endif
