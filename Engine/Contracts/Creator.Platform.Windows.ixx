module;
#include <objbase.h>
#include <roapi.h>

export module Creator.Platform.Windows;

import std;
export import Creator.Core.Window;

#include "WindowsApartmentImplementation.h"
#include "WindowsWindowImplementation.h"
#include "AppWindowBackendDeclarations.h"

export namespace ce::platform::windows
{
    using ::ce::platform::windows::detail::apartment_model;
    using ::ce::platform::windows::detail::apartment_scope;
    using ::ce::platform::windows::detail::window_desc;
    using ::ce::platform::windows::detail::window_message_interceptor;
    using ::ce::platform::windows::detail::window_handle;
    using ::ce::platform::windows::detail::app_window_host;
}
