#include <Windows.h>
#include <cstdio>
#include <exception>
#include "HostAbi.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int);

extern "C" __declspec(dllexport) int __cdecl CreatorHostRunV1(int, wchar_t**, int show)
{
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&CreatorHostRunV1), &module)) return 126;
    try { return wWinMain(module, nullptr, GetCommandLineW(), show); }
    catch (const std::exception& error) { std::fprintf(stderr, "[Host] Native exception: %s\n", error.what()); return 70; }
    catch (...) { std::fputs("[Host] Unhandled native exception.\n", stderr); return 70; }
}
