export module Creator.Core.Window;

import std;

#include "CoreWindowImplementation.h"

export namespace Core
{
    using ::Core::window_detail::WindowDesc;
    using ::Core::window_detail::NativeWindowKind;
    using ::Core::window_detail::NativeWindowHandle;
    using ::Core::window_detail::WindowPollResult;
    using ::Core::window_detail::IWindowBackend;
    using ::Core::window_detail::Window;
}
export using CoreWindow = ::Core::Window;
export using WindowDesc = ::Core::WindowDesc;
export using ::Core::window_detail::ScaledClientSizeRequest;
export using ::Core::window_detail::ScaledClientSize;
export using ::Core::window_detail::ScaleClientSizeToDpi;
