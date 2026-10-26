#if defined(CE_CONTRACT_STANDALONE)
#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    constexpr GUID threadProvider{0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
    bool unsupported{};
    std::uint64_t switches{};

    void WINAPI Event(EVENT_RECORD* event)
    {
        if (!IsEqualGUID(event->EventHeader.ProviderId, threadProvider))
        {
            return;
        }
        const auto opcode = event->EventHeader.EventDescriptor.Opcode;
        const auto* data = static_cast<const unsigned char*>(event->UserData);
        const auto tick = event->EventHeader.TimeStamp.QuadPart;
        const auto cpu = event->BufferContext.ProcessorIndex;
        if (opcode == 36)
        {
            if (event->EventHeader.EventDescriptor.Version != 5 || event->UserDataLength != 28)
            {
                unsupported = true;
                return;
            }
            ULONG incoming{}, outgoing{};
            std::memcpy(&incoming, data, 4);
            std::memcpy(&outgoing, data + 4, 4);
            std::cout << "S," << tick << ',' << cpu << ',' << incoming << ',' << outgoing << ',' << unsigned(data[14])
                      << ',' << unsigned(data[12]) << '\n';
            ++switches;
        }
        else if (opcode == 50 && event->UserDataLength >= 8)
        {
            ULONG thread{};
            std::memcpy(&thread, data, 4);
            std::cout << "R," << tick << ',' << cpu << ',' << thread << ",0,0,0\n";
        }
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }
    EVENT_TRACE_LOGFILEW log{};
    log.LogFileName = argv[1];
    log.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback = Event;
    auto trace = OpenTraceW(&log);
    if (trace == INVALID_PROCESSTRACE_HANDLE)
    {
        return 3;
    }
    const bool qpc = log.LogfileHeader.PerfFreq.QuadPart > 0 && log.LogfileHeader.ReservedFlags == 1;
    const auto result = ProcessTrace(&trace, 1, nullptr, nullptr);
    CloseTrace(trace);
    return result == ERROR_SUCCESS && qpc && switches && !unsupported ? 0 : 4;
}
