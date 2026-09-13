#pragma once
#include <cstdint>

// Borrowed arguments only. No engine objects, allocations or exceptions cross this ABI.
struct CreatorHostInfoV1
{
    std::uint32_t size;
    std::uint32_t hostAbi;
    std::uint32_t compiler;
    std::uint32_t iteratorDebugLevel;
    std::uint32_t debug;
    std::uint32_t shipping;
    std::uint32_t scriptApi;
    std::uint32_t pointerBits;
    std::uint32_t localDevelopment;
    // Static storage in the host, borrowed for this process lifetime.
    const char* productName;
    const char* featureRelease;
    const char* engineVersion;
};
using CreatorHostRun = int(__cdecl*)(int, wchar_t**, int);
using CreatorHostGetInfo = const CreatorHostInfoV1*(__cdecl*)();
