#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <cstring>
#include <iomanip>
#include <iostream>

static LONGLONG frequency;
static bool observedSwitch = false;
static bool unsupportedSwitch = false;

static const GUID threadProvider = {0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};

static void WINAPI eventRecord(EVENT_RECORD* event)
{
    if (!IsEqualGUID(event->EventHeader.ProviderId, threadProvider)) return;

    const auto opcode = event->EventHeader.EventDescriptor.Opcode;
    const auto* data = static_cast<const unsigned char*>(event->UserData);
    const double us = static_cast<double>(event->EventHeader.TimeStamp.QuadPart) * 1000000.0 / frequency;
    ULONG first = 0, second = 0;

    if (opcode == 36 && event->UserDataLength >= 24)
    {
        // This decoder is validated against the captured Windows CSwitch v5 layout.
        // Reject other layouts rather than interpreting future payloads silently.
        if (event->EventHeader.EventDescriptor.Version != 5 || event->UserDataLength != 28)
        {
            unsupportedSwitch = true;
            return;
        }

        if (!observedSwitch)
        {
            observedSwitch = true;
            std::cerr << "CSwitch version=" << unsigned(event->EventHeader.EventDescriptor.Version) << " payloadBytes=" << event->UserDataLength << '\n';
        }
        std::memcpy(&first, data, 4);
        std::memcpy(&second, data + 4, 4);
        std::cout << "switch," << us << ',' << first << ',' << second << ',' << unsigned(data[14])
                  << ',' << unsigned(data[12]) << '\n';
    }
    else if (opcode == 50 && event->UserDataLength >= 8)
    {
        std::memcpy(&first, data, 4);
        std::cout << "ready," << us << ',' << first << ",0,0,0\n";
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;

    EVENT_TRACE_LOGFILEW log{};
    log.LogFileName = argv[1];
    log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback = eventRecord;
    auto trace = OpenTraceW(&log);
    if (trace == INVALID_PROCESSTRACE_HANDLE) return 3;

    frequency = log.LogfileHeader.PerfFreq.QuadPart;
    if (frequency <= 0 || log.LogfileHeader.ReservedFlags != 1) { CloseTrace(trace); return 4; }

    std::cout << std::setprecision(17);
    const auto status = ProcessTrace(&trace, 1, nullptr, nullptr);
    CloseTrace(trace);
    return status == ERROR_SUCCESS && observedSwitch && !unsupportedSwitch ? 0 : 5;
}
