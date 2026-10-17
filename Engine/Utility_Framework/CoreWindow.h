#pragma once

#if defined(CE_CONTRACT_STANDALONE)
#include "StandardLibrary.h"
#include "../Contracts/CoreWindowImplementation.h"
namespace Core
{
    using ::Core::window_detail::WindowDesc;
    using ::Core::window_detail::NativeWindowKind;
    using ::Core::window_detail::NativeWindowHandle;
    using ::Core::window_detail::WindowPollResult;
    using ::Core::window_detail::IWindowBackend;
    using ::Core::window_detail::Window;
}
using CoreWindow = ::Core::Window;
using WindowDesc = ::Core::WindowDesc;
using ::Core::window_detail::ScaledClientSizeRequest;
using ::Core::window_detail::ScaledClientSize;
using ::Core::window_detail::ScaleClientSizeToDpi;
#else
import Creator.Core.Window;
#endif
