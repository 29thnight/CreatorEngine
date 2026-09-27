#pragma once

#include <Windows.h>
#include <cstdint>

// Shared only by the crashed runtime and its launcher in --crash-reporter mode.
// The launcher checks size/version before reading any payload from the mapping.
namespace crash_report
{
    inline constexpr std::uint32_t kMagic = 0x43524345; // ECRC
    inline constexpr std::uint32_t kVersion = 1;
    inline constexpr std::size_t kPathCapacity = 8192;
    inline constexpr std::size_t kReasonCapacity = 512;

    struct alignas(16) Request
    {
        std::uint32_t magic{};
        std::uint32_t version{};
        std::uint32_t size{};
        DWORD targetProcessId{};
        DWORD faultThreadId{};
        DWORD dumpKind{}; // 1 = mini, 2 = full
        DWORD unattended{};
        DWORD hasException{};
        HANDLE dumpDoneEvent{};
        HANDLE reportDoneEvent{};
        volatile LONG reporterStarted{};
        volatile LONG dumpSucceeded{};
        DWORD dumpError{};
        char reason[kReasonCapacity]{};
        wchar_t dumpPath[kPathCapacity]{};
        EXCEPTION_RECORD exceptionRecord{};
        alignas(16) CONTEXT context{};
    };
}
