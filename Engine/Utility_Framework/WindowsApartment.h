#pragma once
// HRESULT and SDK macros remain textual dependencies of existing call sites.
#include <objbase.h>
#include <roapi.h>

// Standalone source probes do not consume the project's linker metadata.
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "runtimeobject.lib")

// The existing explicit adapter flag is also selected by reflgen's Clang parser.
// Native compilation consumes the named module and has no textual class body.
#if defined(CE_CONTRACT_STANDALONE)
#include "../Contracts/WindowsApartmentImplementation.h"
namespace ce::platform::windows
{
    using ::ce::platform::windows::detail::apartment_model;
    using ::ce::platform::windows::detail::apartment_scope;
}
#else
import Creator.Platform.Windows;
#endif
