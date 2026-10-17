#pragma once
#include "WindowsApartment.h"
#include "CoreWindow.h"

#if defined(CE_CONTRACT_STANDALONE)
#include "StandardLibrary.h"
#include "../Contracts/WindowsWindowImplementation.h"
#include "../Contracts/AppWindowBackendDeclarations.h"
namespace ce::platform::windows
{
    using ::ce::platform::windows::detail::window_desc;
    using ::ce::platform::windows::detail::window_message_interceptor;
    using ::ce::platform::windows::detail::window_handle;
    using ::ce::platform::windows::detail::app_window_host;
}
#endif

using WindowsWindowDesc = ::ce::platform::windows::window_desc;
