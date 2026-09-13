#include <cstdio>
#include "HostAbi.h"

int wmain(int, wchar_t**);

extern "C" __declspec(dllexport) int __cdecl CreatorHostRunV1(int argc, wchar_t** argv, int)
{
    try { return wmain(argc, argv); }
    catch (...) { std::fputs("[Host] Unhandled native exception.\n", stderr); return 70; }
}
