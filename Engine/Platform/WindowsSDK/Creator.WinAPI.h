#pragma once

// SDK text is compiled once as a C++ header unit. Imports retain SDK macros.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <roapi.h>
#include <combaseapi.h>
#include <Unknwnbase.h>
#include <wrl/client.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <psapi.h>
#include <DbgHelp.h>
#include <bcrypt.h>
#include <aclapi.h>
#include <sddl.h>
#include <TlHelp32.h>
#include <dwmapi.h>
#include <evntrace.h>
#include <evntcons.h>
#include <comdef.h>
