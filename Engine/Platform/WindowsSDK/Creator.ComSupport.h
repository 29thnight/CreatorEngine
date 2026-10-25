#pragma once

// Textual C++/WinRT support for source-analysis and standalone tools only.
// Native engine consumers import winrt_base directly.
#ifndef WINRT_ENABLE_LEGACY_COM
#define WINRT_ENABLE_LEGACY_COM
#endif
#include <winrt/base.h>
